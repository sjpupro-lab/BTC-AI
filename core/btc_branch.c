/*
 * btc_branch.c — Branch-of-Branch 시나리오 엔진 구현
 * 다중 미래 시나리오 분기 → 합의 추론
 * DK-1: float/double 0건. 정수 전용.
 * Phase 8.
 */
#include "btc_branch.h"
#include "btc_encoding.h"
#include <string.h>

/* ── 시나리오 바이트 배열 ────────────────────── */
static const uint8_t SCENARIO_BYTES[BRANCH_SCENARIOS] = {
    SCENARIO_STRONG_UP,
    SCENARIO_UP,
    SCENARIO_NEUTRAL,
    SCENARIO_DOWN,
    SCENARIO_STRONG_DOWN
};

/* ── 브랜치 테이블 초기화 ────────────────────── */

void btc_branch_table_init(BtcBranchTable *bt) {
    memset(bt, 0, sizeof(BtcBranchTable));
}

/* ── 브랜치 생성 ─────────────────────────────── */

uint32_t btc_branch_create(BtcBranchTable *bt,
                           uint32_t parent_id,
                           uint8_t assumed_byte,
                           uint8_t depth) {
    if (bt->count >= BRANCH_MAX) return BRANCH_NONE;

    uint32_t id = bt->count + 1;
    BtcBranch *b = &bt->branches[bt->count];
    b->branch_id = id;
    b->parent_id = parent_id;
    b->assumed_byte = assumed_byte;
    b->depth = depth;
    memset(&b->prediction, 0, sizeof(MvPrediction));

    bt->count++;
    return id;
}

/* ── 시나리오 분기 + 합의 계산 ───────────────── */

void btc_branch_compute(BranchConsensus *out,
                        BtcCanvasBrain *brain,
                        BtcTimeframe tf,
                        const BtcCandle *candles,
                        uint32_t count) {
    if (!out || !brain || !candles) {
        if (out) memset(out, 0, sizeof(BranchConsensus));
        return;
    }
    memset(out, 0, sizeof(BranchConsensus));

    if (count < 2) return;

    /* 1. 캔들 → 바이트 스트림 인코딩 */
    uint32_t use_count = count;
    if (use_count > MV_MAX_DEPTH + 1) use_count = MV_MAX_DEPTH + 1;

    uint8_t stream_buf[(MV_MAX_DEPTH + MV_MAX_HOPS) * 4];
    uint32_t stream_len = 0;

    uint32_t vol_sum = 0;
    for (uint32_t i = 0; i < use_count; i++) {
        vol_sum += candles[i].volume_x10;
    }
    uint32_t avg_vol = BTC_DIV_SAFE(vol_sum, use_count);
    if (avg_vol == 0) avg_vol = 800;

    const BtcCandle *start = candles + (count - use_count);
    for (uint32_t i = 1; i < use_count; i++) {
        BtcCandleBytes cb;
        btc_candle_encode(&cb, &start[i], &start[i - 1], avg_vol);
        if (stream_len + 4 <= sizeof(stream_buf) - 16) {
            stream_buf[stream_len + 0] = cb.price;
            stream_buf[stream_len + 1] = cb.volume;
            stream_buf[stream_len + 2] = cb.body;
            stream_buf[stream_len + 3] = cb.upper_wick;
            stream_len += 4;
        }
    }

    if (stream_len == 0) return;

    /* 2. 캔버스 접근 */
    CabCanvas *canvas = cab_brain_canvas(&brain->brain);

    /* 3. 브랜치 테이블 초기화 */
    BtcBranchTable bt;
    btc_branch_table_init(&bt);

    /* 4. 각 시나리오에 대해 분기 생성 + 멀티버스 예측 */
    uint64_t up_energy = 0;
    uint64_t down_energy = 0;
    uint64_t hold_energy = 0;

    for (uint32_t s = 0; s < BRANCH_SCENARIOS; s++) {
        uint8_t scenario_byte = SCENARIO_BYTES[s];

        uint32_t bid = btc_branch_create(&bt, 0, scenario_byte, 1);
        if (bid == BRANCH_NONE) break;

        /* 스트림 확장: 시나리오 바이트 4개 추가 (price, vol=127, body=127, wick=0) */
        uint8_t ext_stream[MV_MAX_DEPTH * 4 + 16];
        memcpy(ext_stream, stream_buf, stream_len);
        uint32_t ext_len = stream_len;
        ext_stream[ext_len++] = scenario_byte;  /* price */
        ext_stream[ext_len++] = 127;            /* volume = 평균 */
        ext_stream[ext_len++] = 127;            /* body = 중간 */
        ext_stream[ext_len++] = 0;              /* wick = 없음 */

        /* 이 확장 스트림으로 멀티버스 예측 실행 */
        MvSky sky;
        btc_mv_sky_init(&sky);
        btc_mv_seed(&sky, canvas, ext_stream, ext_len);
        btc_mv_propagate(&sky, canvas, ext_stream, ext_len);
        btc_mv_cross_boost(&sky);

        uint64_t brightness[256];
        uint8_t pred = btc_mv_trace(&sky, brightness);

        /* 분기 예측 결과 저장 */
        BtcBranch *branch = &bt.branches[s];
        branch->prediction.predicted_byte = pred;
        branch->prediction.energy = brightness[pred];
        branch->prediction.active_depths = sky.active_depths;

        if (pred > 140) {
            branch->prediction.direction = SIGNAL_LONG;
            out->up_count++;
            up_energy += brightness[pred];
        } else if (pred < 114) {
            branch->prediction.direction = SIGNAL_SHORT;
            out->down_count++;
            down_energy += brightness[pred];
        } else {
            branch->prediction.direction = SIGNAL_HOLD;
            out->hold_count++;
            hold_energy += brightness[pred];
        }

        out->consensus_energy += brightness[pred];
    }

    out->branch_count = bt.count;

    /* 5. 합의 방향 결정 */
    if (out->up_count > out->down_count && out->up_count > out->hold_count) {
        out->consensus_dir = SIGNAL_LONG;
        out->strongest_dir = 1;
        out->strongest_energy = up_energy;
    } else if (out->down_count > out->up_count && out->down_count > out->hold_count) {
        out->consensus_dir = SIGNAL_SHORT;
        out->strongest_dir = 2;
        out->strongest_energy = down_energy;
    } else {
        out->consensus_dir = SIGNAL_HOLD;
        out->strongest_dir = 0;
        out->strongest_energy = hold_energy;
    }

    /* 6. 합의율: 최다 방향 비율 */
    uint32_t majority = out->up_count;
    if (out->down_count > majority) majority = out->down_count;
    if (out->hold_count > majority) majority = out->hold_count;

    if (out->branch_count > 0) {
        out->agreement_pct = (uint8_t)(majority * 100 / out->branch_count);
    }

    (void)tf; /* 향후 TF별 분리 시 사용 */
}
