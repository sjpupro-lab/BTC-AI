/*
 * btc_full_ensemble.c — BTC-AI + SJ-CANVAOS 완전 통합 앙상블 구현
 * =================================================================
 *
 * 앙상블 가중치:
 *   canvas_ai_score : BTC-AI Canvas Brain       (0~60)
 *   bt_branch_score : BTC-AI BranchTuring       (0~20) ← 항상
 *                   + SJ-CANVAOS BtCanvas        (0~20) ← CANVAOS_ENABLED 시
 *   total_score     : 0~100
 *
 * SJ-CANVAOS 확률 변환 (DK-1/DK-2, float 없음):
 *   btc_predict_lane() → Q16.16 uint32_t (0~65536)
 *   score = p_diff × 20 / 65536   (0~20, 정수 나눗셈)
 *   conf  = p_diff × 200 / 65536  (0~200, 정수 나눗셈)
 *
 * WH (White Hole):
 *   - 매 틱: btc_logs/wh_events_tf{N}.csv 에 이벤트 추가
 *   - WH_SAVE_INTERVAL 틱마다: bt_stream_save() 스냅샷 저장
 *
 * BH (Black Hole):
 *   - BH_COMPRESS_INTERVAL 틱마다: btc_compress() 캔버스 압축
 *
 * DK-1: float/double 0건. 정수 전용.
 */
#include "btc_full_ensemble.h"
#include "core/btc_branch.h"
#include "core/btc_timewarp.h"
#include "core/btc_encoding.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>        /* mktime: time_t = 정수형, DK-1 안전 */
#include <sys/stat.h>    /* mkdir */
#include <errno.h>       /* EEXIST */

/* ── 상수 ──────────────────────────────────────── */
#define WH_LOG_DIR            "btc_logs"
#define WH_SAVE_INTERVAL      100u   /* N 틱마다 WH 스트림 스냅샷 */
#define BH_COMPRESS_INTERVAL   20u   /* N 틱마다 BH 압축 */
#define BH_MIN_FREQ             2u   /* 압축 시 보존 최소 빈도 */

/*
 * SJ-CANVAOS 방향 판별용 v6f 바이트 (btc_branch.h 의 시나리오와 일치):
 *   SCENARIO_UP   = 150  (v6f ≈ +1.8%)
 *   SCENARIO_DOWN = 104  (v6f ≈ -1.8%)
 */
#define SJ_BYTE_UP   150u
#define SJ_BYTE_DOWN 104u

/* ── 전역 상태 ──────────────────────────────────── */
static BtcCanvasBrain  g_brain;
static uint64_t        g_tf_last_ts[TF_COUNT];
static uint32_t        g_wh_tick_count[TF_COUNT];
static int             g_initialized = 0;

#ifdef BTC_CANVAOS_ENABLED
/*
 * BtCanvas: 4096×4096×8B = 128 MB — 반드시 힙 할당.
 * BtcLaneCtx: TF별 독립 슬라이딩 윈도우 (lane_id = BtcTimeframe 값)
 */
static BtCanvas    *g_btc = NULL;
static BtcLaneCtx   g_lane_ctx[TF_COUNT];
#endif

/* ── 내부 유틸 ──────────────────────────────────── */
static int32_t signal_to_int(SignalDir dir)
{
    if (dir == SIGNAL_LONG)  return  1;
    if (dir == SIGNAL_SHORT) return -1;
    return 0;
}

/* ── btc_full_ensemble_init ─────────────────────── */
void btc_full_ensemble_init(void)
{
    memset(g_tf_last_ts,     0, sizeof(g_tf_last_ts));
    memset(g_wh_tick_count,  0, sizeof(g_wh_tick_count));

    /* BTC-AI Canvas Brain 초기화 */
    if (btc_brain_init(&g_brain) != BTC_OK) {
        fprintf(stderr, "[ENSEMBLE] ERROR: Canvas Brain init 실패\n");
        return;
    }

    /* WH 로그 디렉토리 생성 */
    if (mkdir(WH_LOG_DIR, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[ENSEMBLE] WARN: %s 생성 실패\n", WH_LOG_DIR);
    }

#ifdef BTC_CANVAOS_ENABLED
    /* SJ-CANVAOS BtCanvas 힙 할당 + 초기화 */
    g_btc = (BtCanvas *)malloc(sizeof(BtCanvas));
    if (!g_btc) {
        fprintf(stderr, "[ENSEMBLE] ERROR: BtCanvas malloc 실패 (%zu MB)\n",
                sizeof(BtCanvas) / (1024u * 1024u));
        return;
    }
    btc_init(g_btc);
    memset(g_lane_ctx, 0, sizeof(g_lane_ctx));

    printf("🚀 ENSEMBLE INIT: Canvas AI + BT-BranchTuring + SJ-CANVAOS BtCanvas + WH/BH\n");
    printf("   SJ-CANVAOS BtCanvas: %zu MB (힙)\n",
           sizeof(BtCanvas) / (1024u * 1024u));
#else
    printf("🚀 ENSEMBLE INIT: Canvas AI + BT-BranchTuring + WH/BH\n");
    printf("   SJ-CANVAOS: -DBTC_CANVAOS_ENABLED + submodule 로 활성화 가능\n");
#endif

    g_initialized = 1;
}

/* ── btc_full_ensemble_predict ──────────────────── */
FullEnsembleResult btc_full_ensemble_predict(
    const BtcCandle *candles, uint32_t count, BtcTimeframe tf)
{
    FullEnsembleResult res;
    memset(&res, 0, sizeof(res));
    if (!candles || count < 2u || !g_initialized) return res;

    /* ── 1. 평균 볼륨 계산 (최근 20 캔들) ── */
    uint32_t vol_n   = (count > 20u) ? 20u : count;
    uint32_t vol_sum = 0u;
    for (uint32_t i = count - vol_n; i < count; i++) {
        vol_sum += candles[i].volume_x10;
    }
    uint32_t avg_vol = (vol_n > 0u) ? (vol_sum / vol_n) : 1u;

    /* ── 2. 증분 학습 (새 캔들만) ── */
    uint32_t train_start = 1u;
    if (g_tf_last_ts[tf] != 0u) {
        /* 뒤에서 순방향 탐색: 마지막 학습 이후 캔들 찾기 */
        for (uint32_t i = count; i-- > 1u; ) {
            if (candles[i - 1u].timestamp <= g_tf_last_ts[tf]) {
                train_start = i;
                break;
            }
        }
    }

    for (uint32_t i = train_start; i < count; i++) {
        /* BTC-AI Canvas Brain 학습 */
        btc_brain_train_candle(&g_brain, tf,
                               &candles[i], &candles[i - 1u], avg_vol);

#ifdef BTC_CANVAOS_ENABLED
        /* SJ-CANVAOS BtCanvas: v6f 인코딩 후 레인별 학습
         * lane_id = BtcTimeframe 값 (0~6, BTC_MAX_LANES=8 이내)
         */
        if (g_btc) {
            BtcCandleBytes cb;
            btc_candle_encode(&cb, &candles[i], &candles[i - 1u], avg_vol);
            btc_train_lane(g_btc, cb.price,
                           (uint8_t)tf, &g_lane_ctx[tf]);
        }
#endif
    }

    if (count > 0u) g_tf_last_ts[tf] = candles[count - 1u].timestamp;

    /* ── 3. BTC-AI Canvas AI 예측 ── */
    BtcPrediction canvas_pred;
    memset(&canvas_pred, 0, sizeof(canvas_pred));
    btc_brain_predict(&g_brain, tf, &canvas_pred);
    /* confidence 0~1000 → score 0~60 (정수 나눗셈) */
    int32_t c_score = (int32_t)(canvas_pred.confidence * 60u / 1000u);

    /* ── 4. BTC-AI BranchTuring 예측 ── */
    BranchConsensus consensus;
    memset(&consensus, 0, sizeof(consensus));
    btc_branch_compute(&consensus, &g_brain, tf, candles, count);
    /* agreement_pct 0~100 → score 0~20 */
    int32_t branch_score = (int32_t)(consensus.agreement_pct * 20u / 100u);

    /* ── 5. SJ-CANVAOS BtCanvas 예측 ── */
#ifdef BTC_CANVAOS_ENABLED
    int32_t  sj_score    = 0;
    int32_t  sj_dir      = 0;
    uint32_t sj_conf_add = 0u;

    if (g_btc) {
        /*
         * P(SJ_BYTE_UP | context)  — Q16.16 (0~65536)
         * P(SJ_BYTE_DOWN | context) — Q16.16 (0~65536)
         * btc_predict_lane() 은 윈도우를 수정하지 않음 (순수 조회)
         */
        uint32_t p_up   = btc_predict_lane(g_btc, (uint8_t)SJ_BYTE_UP,
                                            (uint8_t)tf, &g_lane_ctx[tf]);
        uint32_t p_down = btc_predict_lane(g_btc, (uint8_t)SJ_BYTE_DOWN,
                                            (uint8_t)tf, &g_lane_ctx[tf]);
        uint32_t p_diff = (p_up > p_down) ? (p_up - p_down)
                                           : (p_down - p_up);

        /* Q16.16 → 0~20 점수 (정수 나눗셈, DK-2) */
        sj_score    = (int32_t)(p_diff * 20u / 65536u);
        sj_dir      = (p_up > p_down) ? 1 : (p_down > p_up) ? -1 : 0;
        /* Q16.16 → 0~200 신뢰도 기여 */
        sj_conf_add = p_diff * 200u / 65536u;
    }
#else
    int32_t sj_score = 0;
    int32_t sj_dir   = 0;
#endif

    /* ── 6. 앙상블 결합 ── */
    res.canvas_ai_score = c_score;
    res.bt_branch_score = branch_score + sj_score;          /* 0~40 */
    res.total_score     = c_score + branch_score + sj_score; /* 0~100 */

    /* 방향: Canvas×3 + Branch×1 + SJ×1 가중 합산 */
    int32_t dir_weighted = signal_to_int(canvas_pred.direction) * 3
                         + signal_to_int(consensus.consensus_dir)
                         + sj_dir;
    res.direction = (dir_weighted > 0) ? 1 : (dir_weighted < 0) ? -1 : 0;

    /* 신뢰도 0~1000 (정수 전용):
     *   Canvas:  confidence × 6/10    → 0~600
     *   Branch:  agreement_pct × 2    → 0~200  (CANVAOS_ENABLED 시)
     *            agreement_pct × 4    → 0~400  (비활성 시)
     *   SJ:      sj_conf_add          → 0~200  (CANVAOS_ENABLED 시)
     */
#ifdef BTC_CANVAOS_ENABLED
    res.confidence_pct = canvas_pred.confidence * 6u / 10u   /* 0~600 */
                       + (uint32_t)consensus.agreement_pct * 2u /* 0~200 */
                       + sj_conf_add;                         /* 0~200 */
#else
    res.confidence_pct = canvas_pred.confidence * 6u / 10u   /* 0~600 */
                       + (uint32_t)consensus.agreement_pct * 4u; /* 0~400 */
#endif

    snprintf(res.reason, sizeof(res.reason),
             "Canvas(%d)+Branch(%d)+SJ(%d)",
             c_score, branch_score, sj_score);

    return res;
}

/* ── btc_wh_bh_log_tick ─────────────────────────── */
void btc_wh_bh_log_tick(
    const BtcCandle *latest, BtcTimeframe tf,
    const FullEnsembleResult *result)
{
    if (!latest || !result || !g_initialized) return;

    g_wh_tick_count[tf]++;
    uint32_t tick = g_wh_tick_count[tf];

    /* ── WH: 이벤트 CSV 로깅 (매 틱) ── */
    char csv_path[128];
    snprintf(csv_path, sizeof(csv_path),
             "%s/wh_events_tf%d.csv", WH_LOG_DIR, (int)tf);
    FILE *f = fopen(csv_path, "a");
    if (f) {
        /* 포맷: timestamp,tick,score,direction */
        fprintf(f, "%llu,%u,%d,%d\n",
                (unsigned long long)latest->timestamp,
                tick, result->total_score, result->direction);
        fclose(f);
    }

#ifdef BTC_CANVAOS_ENABLED
    /* ── WH: 주기적 BtCanvas 스냅샷 (bt_stream_save) ── */
    if (g_btc && tick % WH_SAVE_INTERVAL == 0u) {
        char stream_path[128];
        snprintf(stream_path, sizeof(stream_path),
                 "%s/wh_tf%d.stream", WH_LOG_DIR, (int)tf);
        if (bt_stream_save(g_btc, stream_path) == BTS_OK) {
            printf("💾 WH STREAM SAVED: %s (tick=%u, 활성셀=%u)\n",
                   stream_path, tick, g_btc->used);
        }
    }

    /* ── BH: 주기적 캔버스 압축 (btc_compress) ── */
    if (g_btc && tick % BH_COMPRESS_INTERVAL == 0u) {
        BtcCompressStats cs = btc_compress(g_btc, BH_MIN_FREQ);
        /* ratio_x100 = cells_before × 100 / cells_after (역비율) */
        uint32_t reduction = 0u;
        if (cs.cells_before > 0u && cs.cells_after < cs.cells_before) {
            reduction = (cs.cells_before - cs.cells_after) * 100u
                      / cs.cells_before;
        }
        printf("⚫ BH COMPRESS (TF=%d, tick=%u): %u→%u cells, -%u%%\n",
               (int)tf, tick,
               cs.cells_before, cs.cells_after, reduction);
    }
#endif

    printf("📼 WH LOG (ts=%llu, TF=%d, score=%d, dir=%d, tick=%u)\n",
           (unsigned long long)latest->timestamp,
           (int)tf, result->total_score, result->direction, tick);
}

/* ── btc_replay_from_log ────────────────────────── */
int btc_replay_from_log(const char *date_str, BtcTimeframe tf)
{
    if (!date_str || !g_initialized) return BTC_ERR_PARAM;

    /* "YYYY-MM-DD" → Unix timestamp (time_t = 정수, DK-1) */
    int year = 0, mon = 0, day = 0;
    if (sscanf(date_str, "%d-%d-%d", &year, &mon, &day) != 3) {
        return BTC_ERR_PARAM;
    }
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year  = year - 1900;
    t.tm_mon   = mon - 1;
    t.tm_mday  = day;
    t.tm_isdst = -1;
    time_t target_ts = mktime(&t);
    if (target_ts < 0) return BTC_ERR_PARAM;

    /* WH 이벤트 CSV 탐색: target_ts 이하 최근 항목 찾기 */
    char csv_path[128];
    snprintf(csv_path, sizeof(csv_path),
             "%s/wh_events_tf%d.csv", WH_LOG_DIR, (int)tf);
    FILE *f = fopen(csv_path, "r");
    if (!f) {
        printf("[TIMEWARP] WH 로그 없음: %s\n", csv_path);
        return BTC_ERR_IO;
    }

    uint64_t best_ts   = 0u;
    uint32_t best_tick = 0u;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long ts;
        unsigned int tick;
        if (sscanf(line, "%llu,%u", &ts, &tick) == 2) {
            if ((uint64_t)target_ts >= ts) {
                best_ts   = (uint64_t)ts;
                best_tick = (uint32_t)tick;
            }
        }
    }
    fclose(f);

    if (best_tick == 0u) {
        printf("[TIMEWARP] %s (TF=%d) 에 해당하는 WH 항목 없음\n",
               date_str, (int)tf);
        return BTC_ERR_IO;
    }

#ifdef BTC_CANVAOS_ENABLED
    /* SJ-CANVAOS BtCanvas 상태 복원 (가장 최근 WH 스냅샷) */
    if (g_btc) {
        char stream_path[128];
        snprintf(stream_path, sizeof(stream_path),
                 "%s/wh_tf%d.stream", WH_LOG_DIR, (int)tf);
        if (bt_stream_load(g_btc, stream_path) == BTS_OK) {
            printf("⏪ SJ-CANVAOS 모델 복원: %s\n", stream_path);
        } else {
            printf("[TIMEWARP] SJ-CANVAOS 스냅샷 로드 실패: %s\n", stream_path);
        }
    }
#endif

    /* BTC-AI Timewarp */
    BtcTimeWarp tw;
    btc_timewarp_init(&tw);
    int rc = btc_timewarp_goto(&tw, best_tick, g_wh_tick_count[tf]);

    printf("⏪ TIMEWARP to %s (TF=%d) → tick=%u, ts=%llu, rc=%d\n",
           date_str, (int)tf, best_tick,
           (unsigned long long)best_ts, rc);
    return rc;
}
