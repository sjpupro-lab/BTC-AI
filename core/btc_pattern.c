/*
 * btc_pattern.c — Canvas AI 패턴 유사도 엔진 구현
 * Gear Hash 기반 캔들 패턴 지문 생성 + 유사도 검색
 * DK-1: float/double 0건. 정수 전용.
 * Phase 6.
 */
#include "btc_pattern.h"
#include "btc_encoding.h"
#include <string.h>

/* ── 내부 상수 ─────────────────────────────────── */

/* 가격 인코딩 중립점 (v6f: 127 = 변화 없음) */
#define PRICE_NEUTRAL   127

/* up/down 판정 임계값 */
#define PRICE_UP_THRESH   140   /* > 140 = 가격 상승 패턴 */
#define PRICE_DOWN_THRESH 114   /* < 114 = 가격 하락 패턴 */

/* 최대 프로빙 깊이 */
#define PAT_MAX_DEPTH   32

/* 평균 볼륨 기본값 (데이터 없을 때) */
#define PAT_DEFAULT_AVG_VOL  800

/* ── 계층별 가중치 (깊이 기반, 정수) ──────────── */
/*
 * 짧은 깊이(최근 패턴)에 높은 가중치를 부여.
 * d=1~4: weight=8, d=5~8: weight=6,
 * d=9~16: weight=4, d=17~32: weight=2
 */
static inline uint32_t pat_layer_weight(uint32_t d) {
    if (d <= 4)  return 8;
    if (d <= 8)  return 6;
    if (d <= 16) return 4;
    return 2;
}

/* ── 내부: 스트림에서 패턴 점수 수집 ──────────── */

typedef struct {
    uint32_t total_score;   /* 전체 가중 점수 합 */
    uint32_t up_score;      /* 상승 후속 점수 */
    uint32_t down_score;    /* 하락 후속 점수 */
    uint32_t gear;          /* 대표 gear hash */
} PatCandidate;

/*
 * 스트림 데이터에서 gear hash 기반으로 캔버스를 프로빙하여
 * 후속 바이트(candidate) 점수를 수집한다.
 *
 * 각 깊이 d에서:
 *   gear = cab_gear(stream_end - d, d)
 *   후보 바이트 0~255를 cab_probe_read()로 조회
 *   G > 0 이면 해당 후보는 학습된 후속 패턴
 */
static void pat_collect_scores(PatCandidate *out,
                               const CabCanvas *canvas,
                               const uint8_t *stream_data,
                               uint32_t stream_len) {
    memset(out, 0, sizeof(PatCandidate));

    if (stream_len == 0) return;

    uint32_t max_d = stream_len;
    if (max_d > PAT_MAX_DEPTH) max_d = PAT_MAX_DEPTH;

    /* 대표 gear: 최대 깊이에서의 gear hash */
    out->gear = cab_gear(stream_data + stream_len - max_d, (uint8_t)max_d);

    for (uint32_t d = 1; d <= max_d; d++) {
        const uint8_t *ctx = stream_data + stream_len - d;
        uint32_t gear = cab_gear(ctx, (uint8_t)d);
        uint32_t w = pat_layer_weight(d);

        /* sem_B 인코딩: 학습 시 저장된 형식과 일치해야 함 */
        uint8_t cluster = (uint8_t)(cab_encode_A(gear, ctx, (uint8_t)d) >> 24);
        uint8_t sem_B = cab_brain_encode_B((uint8_t)d, cluster);

        /* 후보 바이트 0~255 프로빙 */
        for (uint32_t cand = 0; cand < 256; cand++) {
            uint32_t idx = cab_probe_read(canvas, gear, sem_B, (uint8_t)cand);
            if (idx == UINT32_MAX) continue;

            uint16_t g_val = canvas->cells[idx].G;
            if (g_val == 0) continue;

            uint32_t weighted = (uint32_t)g_val * w;
            out->total_score += weighted;

            /*
             * 캔들 바이트 스트림: 4바이트/캔들 [price, vol, body, wick]
             * 후보 cand가 가격 바이트 위치에 해당하는지는
             * 깊이로 판단: d mod 4 == 0 이면 다음 캔들의 첫 바이트(price)
             * 간략화: 모든 후보에서 가격 해석
             */
            if (cand > PRICE_UP_THRESH) {
                out->up_score += weighted;
            } else if (cand < PRICE_DOWN_THRESH) {
                out->down_score += weighted;
            }
        }
    }
}

/* ── btc_pattern_search 구현 ──────────────────── */

void btc_pattern_search(PatternSimilarityResult *out,
                        BtcCanvasBrain *brain,
                        BtcTimeframe tf,
                        const BtcCandle *recent,
                        uint32_t count) {
    if (!out || !brain || !recent) {
        if (out) memset(out, 0, sizeof(PatternSimilarityResult));
        return;
    }
    memset(out, 0, sizeof(PatternSimilarityResult));

    /* 최소 2개 캔들 필요 (인코딩에 prev 필요) */
    if (count < 2) return;

    /* 사용할 캔들 수 제한 */
    uint32_t use_count = count;
    if (use_count > PAT_WINDOW + 1) use_count = PAT_WINDOW + 1;

    /* 1. 최근 캔들을 BtcCandleBytes로 인코딩 → 바이트 스트림 구성 */
    uint8_t stream_buf[PAT_WINDOW * 4]; /* 최대 40바이트 */
    uint32_t stream_len = 0;

    /* 평균 볼륨 계산 (정수) */
    uint32_t vol_sum = 0;
    for (uint32_t i = 0; i < use_count; i++) {
        vol_sum += recent[i].volume_x10;
    }
    uint32_t avg_vol = BTC_DIV_SAFE(vol_sum, use_count);
    if (avg_vol == 0) avg_vol = PAT_DEFAULT_AVG_VOL;

    /* 캔들 인코딩: recent[0]=가장 오래된, recent[use_count-1]=최신 */
    const BtcCandle *start = recent + (count - use_count);
    for (uint32_t i = 1; i < use_count; i++) {
        BtcCandleBytes cb;
        btc_candle_encode(&cb, &start[i], &start[i - 1], avg_vol);
        if (stream_len + 4 <= sizeof(stream_buf)) {
            stream_buf[stream_len + 0] = cb.price;
            stream_buf[stream_len + 1] = cb.volume;
            stream_buf[stream_len + 2] = cb.body;
            stream_buf[stream_len + 3] = cb.upper_wick;
            stream_len += 4;
        }
    }

    if (stream_len == 0) return;

    /* 2. 캔버스에서 패턴 점수 수집 */
    CabCanvas *canvas = cab_brain_canvas(&brain->brain);

    PatCandidate cand;
    pat_collect_scores(&cand, canvas, stream_buf, stream_len);

    /* 점수가 0이면 매칭 없음 */
    if (cand.total_score == 0) return;

    /*
     * 3. 슬라이딩 윈도우: 여러 시작 오프셋에서 점수 수집
     *    각 오프셋은 하나의 PatternMatch 후보
     */
    PatCandidate candidates[PAT_WINDOW];
    uint32_t num_cands = 0;

    for (uint32_t off = 0; off < stream_len && num_cands < PAT_WINDOW; off += 4) {
        uint32_t sub_len = stream_len - off;
        if (sub_len < 4) break;

        PatCandidate pc;
        pat_collect_scores(&pc, canvas, stream_buf + off, sub_len);
        if (pc.total_score > 0) {
            candidates[num_cands++] = pc;
        }
    }

    if (num_cands == 0) return;

    /* 4. 최대 점수 찾기 (정규화용) */
    uint32_t max_score = 0;
    for (uint32_t i = 0; i < num_cands; i++) {
        if (candidates[i].total_score > max_score)
            max_score = candidates[i].total_score;
    }

    /* 5. 점수 기준 정렬 (단순 선택 정렬, 최대 PAT_WINDOW=10) */
    for (uint32_t i = 0; i < num_cands; i++) {
        for (uint32_t j = i + 1; j < num_cands; j++) {
            if (candidates[j].total_score > candidates[i].total_score) {
                PatCandidate tmp = candidates[i];
                candidates[i] = candidates[j];
                candidates[j] = tmp;
            }
        }
    }

    /* 6. 상위 PAT_TOP_K개를 결과에 채우기 */
    uint32_t fill = (num_cands < PAT_TOP_K) ? num_cands : PAT_TOP_K;
    uint32_t up_majority = 0;
    uint32_t down_majority = 0;
    uint32_t sim_sum = 0;

    for (uint32_t i = 0; i < fill; i++) {
        PatternMatch *m = &out->matches[i];
        PatCandidate *c = &candidates[i];

        m->gear = c->gear;
        /* similarity = score * 10000 / (max_possible + 1) */
        m->similarity_x100 = c->total_score * 10000 / (max_score + 1);
        m->up_score   = c->up_score;
        m->down_score = c->down_score;

        /* historical_up_pct = up * 100 / (up + down + 1) */
        m->historical_up_pct = (uint8_t)(c->up_score * 100
                                / (c->up_score + c->down_score + 1));

        sim_sum += m->similarity_x100;

        if (m->historical_up_pct > 55) up_majority++;
        else if (m->historical_up_pct < 45) down_majority++;
    }

    out->count = (uint8_t)fill;

    /* 7. 합의 방향 결정 */
    if (up_majority > fill / 2) {
        out->consensus_direction = 1; /* up */
    } else if (down_majority > fill / 2) {
        out->consensus_direction = 2; /* down */
    } else {
        out->consensus_direction = 0; /* unknown */
    }

    /* 8. 합의 강도 = 평균 similarity / 100 (0~100 범위) */
    uint32_t avg_sim = BTC_DIV_SAFE(sim_sum, fill);
    out->consensus_strength = (uint8_t)BTC_CLAMP(avg_sim / 100, 0, 100);
}

/* ── btc_pattern_jaccard_x100 구현 ────────────── */

uint32_t btc_pattern_jaccard_x100(const uint32_t *act_a,
                                   const uint32_t *act_b,
                                   uint32_t len) {
    if (!act_a || !act_b || len == 0) return 0;

    uint32_t intersection = 0;
    uint32_t union_count  = 0;

    for (uint32_t i = 0; i < len; i++) {
        uint32_t a_present = (act_a[i] > 0) ? 1 : 0;
        uint32_t b_present = (act_b[i] > 0) ? 1 : 0;

        if (a_present && b_present) {
            intersection++;
            union_count++;
        } else if (a_present || b_present) {
            union_count++;
        }
    }

    return intersection * 10000 / (union_count + 1);
}
