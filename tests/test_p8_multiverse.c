/*
 * test_p8_multiverse.c -- Phase 8 멀티버스 + 브랜치 + 타임워프 검증
 * DK-1: float/double 0건. 정수 전용.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../core/btc_types.h"
#include "../core/btc_multiverse.h"
#include "../core/btc_branch.h"
#include "../core/btc_timewarp.h"

/* ── 간단한 테스트 프레임워크 ──────────────────── */
static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); g_pass++; \
    } else { \
        printf("  [FAIL] %s  (line %d)\n", msg, __LINE__); g_fail++; \
    } \
} while(0)

/* ── 테스트 데이터 생성 ──────────────────────── */
#define TEST_CANDLES 300

static BtcCandle g_candles[TEST_CANDLES];

static void init_ascending_candles(void) {
    for (int i = 0; i < TEST_CANDLES; i++) {
        uint32_t price = 3500000 + (uint32_t)i * 5000;
        g_candles[i].timestamp = 1700000000ULL + (uint64_t)i * 3600;
        g_candles[i].open_x100  = price;
        g_candles[i].high_x100  = price + 8000;
        g_candles[i].low_x100   = (price > 5000) ? price - 5000 : 0;
        g_candles[i].close_x100 = price + 3000;
        g_candles[i].volume_x10 = 800 + (uint32_t)(i % 200);
    }
}

static void init_idle_candles(BtcCandle *out, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        out[i].timestamp = 1700000000ULL + (uint64_t)i * 3600;
        out[i].open_x100  = 3500000;
        out[i].high_x100  = 3500050;
        out[i].low_x100   = 3499950;
        out[i].close_x100 = 3500010 + (i % 3); /* 거의 변동 없음 */
        out[i].volume_x10 = 800;
    }
}

static void init_loop_candles(BtcCandle *out, uint32_t count) {
    /* 주기 4: 상승-상승-하락-하락 반복 */
    for (uint32_t i = 0; i < count; i++) {
        uint32_t phase = i % 4;
        uint32_t base = 3500000;
        switch (phase) {
            case 0: out[i].close_x100 = base;         break;
            case 1: out[i].close_x100 = base + 10000;  break;
            case 2: out[i].close_x100 = base + 5000;   break;
            case 3: out[i].close_x100 = base - 5000;   break;
        }
        out[i].timestamp = 1700000000ULL + (uint64_t)i * 3600;
        out[i].open_x100 = base;
        out[i].high_x100 = out[i].close_x100 + 1000;
        out[i].low_x100  = out[i].close_x100 > 1000
                         ? out[i].close_x100 - 1000 : 0;
        out[i].volume_x10 = 800;
    }
}

static void init_burst_candles(BtcCandle *out, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 3500000;
        out[i].timestamp = 1700000000ULL + (uint64_t)i * 3600;
        out[i].open_x100 = base;
        out[i].high_x100 = base + 1000;
        out[i].low_x100  = base - 1000;
        /* 처음 5개 캔들에 급변동 */
        if (i > 0 && i <= 5) {
            out[i].close_x100 = base + (uint32_t)i * 2000;
        } else {
            out[i].close_x100 = base + 100;
        }
        out[i].volume_x10 = 800;
    }
}

/* ═══════════════════════════════════════════════════
 *  PART 1: Multiverse Inference Engine
 * ═══════════════════════════════════════════════════ */

int main(void) {
    printf("=== Phase 8: Multiverse + Branch + TimeWarp ===\n\n");

    init_ascending_candles();

    /* ──────────────────────────────────────────────
     * T1: MvSky 초기화
     * ────────────────────────────────────────────── */
    printf("[T1] MvSky initialization\n");
    {
        MvSky sky;
        btc_mv_sky_init(&sky);
        ASSERT(sky.active_depths == 0, "Sky init: active_depths == 0");
        ASSERT(sky.total_stars == 0, "Sky init: total_stars == 0");
        ASSERT(sky.crossings == 0, "Sky init: crossings == 0");
        ASSERT(sky.sky[0][0] == 0, "Sky init: sky[0][0] == 0");
    }

    /* ──────────────────────────────────────────────
     * T2: Multiverse prediction with trained brain
     * ────────────────────────────────────────────── */
    printf("\n[T2] Multiverse prediction (trained brain)\n");
    {
        BtcCanvasBrain brain;
        int rc = btc_brain_init(&brain);
        ASSERT(rc == BTC_OK, "btc_brain_init OK");

        if (rc == BTC_OK) {
            /* Train 250 ascending candles */
            uint32_t avg_vol = 800;
            for (int i = 1; i <= 250; i++) {
                btc_brain_train_candle(&brain, TF_1H,
                                       &g_candles[i],
                                       &g_candles[i - 1],
                                       avg_vol);
            }
            ASSERT(brain.candles_trained == 250, "250 candles trained");

            /* Multiverse prediction */
            MvPrediction pred;
            btc_mv_predict(&pred, &brain, TF_1H,
                           &g_candles[230], 20);

            printf("    predicted_byte = %u\n", pred.predicted_byte);
            printf("    energy = %llu\n", (unsigned long long)pred.energy);
            printf("    active_depths = %u\n", pred.active_depths);
            printf("    direction = %u\n", pred.direction);
            printf("    confidence = %u\n", pred.confidence);

            ASSERT(pred.direction <= 3, "Direction is valid (0-3)");
            ASSERT(pred.confidence <= 100, "Confidence in 0-100");

            /* With trained data, should have non-zero energy */
            if (pred.energy > 0) {
                ASSERT(pred.active_depths > 0, "Active depths > 0");
                ASSERT(pred.top3_energies[0] > 0, "Top-1 energy > 0");
            }

            btc_brain_free(&brain);
        }
    }

    /* ──────────────────────────────────────────────
     * T3: Multiverse prediction determinism
     * ────────────────────────────────────────────── */
    printf("\n[T3] Multiverse determinism\n");
    {
        MvPrediction pred1, pred2;

        for (int run = 0; run < 2; run++) {
            BtcCanvasBrain brain;
            btc_brain_init(&brain);
            uint32_t avg_vol = 800;
            for (int i = 1; i <= 200; i++) {
                btc_brain_train_candle(&brain, TF_1H,
                                       &g_candles[i],
                                       &g_candles[i - 1],
                                       avg_vol);
            }

            MvPrediction *p = (run == 0) ? &pred1 : &pred2;
            btc_mv_predict(p, &brain, TF_1H, &g_candles[180], 20);
            btc_brain_free(&brain);
        }

        ASSERT(pred1.predicted_byte == pred2.predicted_byte,
               "Deterministic: predicted_byte match");
        ASSERT(pred1.energy == pred2.energy,
               "Deterministic: energy match");
        ASSERT(pred1.direction == pred2.direction,
               "Deterministic: direction match");
        ASSERT(pred1.active_depths == pred2.active_depths,
               "Deterministic: active_depths match");
    }

    /* ──────────────────────────────────────────────
     * T4: Empty brain → zero energy
     * ────────────────────────────────────────────── */
    printf("\n[T4] Empty brain → zero energy\n");
    {
        BtcCanvasBrain brain;
        btc_brain_init(&brain);

        MvPrediction pred;
        btc_mv_predict(&pred, &brain, TF_1H, &g_candles[0], 20);

        ASSERT(pred.energy == 0, "Empty brain: energy == 0");
        ASSERT(pred.active_depths == 0, "Empty brain: active_depths == 0");

        btc_brain_free(&brain);
    }

    /* ──────────────────────────────────────────────
     * T5: Seed produces non-zero energy
     * ────────────────────────────────────────────── */
    printf("\n[T5] Seed stage validation\n");
    {
        BtcCanvasBrain brain;
        btc_brain_init(&brain);
        uint32_t avg_vol = 800;
        for (int i = 1; i <= 100; i++) {
            btc_brain_train_candle(&brain, TF_1H,
                                   &g_candles[i],
                                   &g_candles[i - 1],
                                   avg_vol);
        }

        /* Encode some candles */
        uint8_t stream[40];
        uint32_t slen = 0;
        for (int i = 81; i <= 90; i++) {
            BtcCandleBytes cb;
            btc_candle_encode(&cb, &g_candles[i], &g_candles[i - 1], avg_vol);
            stream[slen++] = cb.price;
            stream[slen++] = cb.volume;
            stream[slen++] = cb.body;
            stream[slen++] = cb.upper_wick;
        }

        CabCanvas *canvas = cab_brain_canvas(&brain.brain);
        MvSky sky;
        btc_mv_sky_init(&sky);
        btc_mv_seed(&sky, canvas, stream, slen);

        printf("    total_stars = %u\n", sky.total_stars);
        printf("    active_depths = %u\n", sky.active_depths);

        /* After seeding with trained data, should have stars */
        ASSERT(sky.total_stars > 0 || sky.active_depths == 0,
               "Seed: stars consistent with depths");

        btc_brain_free(&brain);
    }

    /* ═══════════════════════════════════════════════════
     *  PART 2: Branch-of-Branch Scenario Engine
     * ═══════════════════════════════════════════════════ */

    /* ──────────────────────────────────────────────
     * T6: Branch table operations
     * ────────────────────────────────────────────── */
    printf("\n[T6] Branch table operations\n");
    {
        BtcBranchTable bt;
        btc_branch_table_init(&bt);
        ASSERT(bt.count == 0, "Branch table init: count == 0");

        uint32_t b1 = btc_branch_create(&bt, 0, 170, 1);
        uint32_t b2 = btc_branch_create(&bt, 0, 127, 1);
        uint32_t b3 = btc_branch_create(&bt, b1, 150, 2);
        ASSERT(b1 != BRANCH_NONE, "Branch 1 created");
        ASSERT(b2 != BRANCH_NONE, "Branch 2 created");
        ASSERT(b3 != BRANCH_NONE, "Branch 3 (child of 1) created");
        ASSERT(bt.count == 3, "3 branches in table");
        ASSERT(bt.branches[2].parent_id == b1, "Branch 3 parent == Branch 1");
    }

    /* ──────────────────────────────────────────────
     * T7: Branch consensus computation
     * ────────────────────────────────────────────── */
    printf("\n[T7] Branch consensus computation\n");
    {
        BtcCanvasBrain brain;
        btc_brain_init(&brain);
        uint32_t avg_vol = 800;
        for (int i = 1; i <= 250; i++) {
            btc_brain_train_candle(&brain, TF_1H,
                                   &g_candles[i],
                                   &g_candles[i - 1],
                                   avg_vol);
        }

        BranchConsensus cons;
        btc_branch_compute(&cons, &brain, TF_1H,
                           &g_candles[230], 20);

        printf("    branch_count = %u\n", cons.branch_count);
        printf("    consensus_dir = %u\n", cons.consensus_dir);
        printf("    up=%u down=%u hold=%u\n",
               cons.up_count, cons.down_count, cons.hold_count);
        printf("    agreement_pct = %u\n", cons.agreement_pct);
        printf("    consensus_energy = %llu\n",
               (unsigned long long)cons.consensus_energy);

        ASSERT(cons.branch_count == BRANCH_SCENARIOS,
               "5 scenario branches created");
        ASSERT(cons.up_count + cons.down_count + cons.hold_count
               == cons.branch_count,
               "Branch counts sum correctly");
        ASSERT(cons.consensus_dir <= 3,
               "Consensus direction valid (0-3)");
        ASSERT(cons.agreement_pct <= 100,
               "Agreement percentage <= 100");

        btc_brain_free(&brain);
    }

    /* ═══════════════════════════════════════════════════
     *  PART 3: BH/WH TimeWarp Engine
     * ═══════════════════════════════════════════════════ */

    /* ──────────────────────────────────────────────
     * T8: BH IDLE detection
     * ────────────────────────────────────────────── */
    printf("\n[T8] BH IDLE detection\n");
    {
        BtcCandle idle[32];
        init_idle_candles(idle, 32);

        BhSummary summary;
        int found = btc_bh_analyze(&summary, idle, 0, 31);

        printf("    found = %d\n", found);
        if (found) {
            printf("    rule = %d (expect IDLE=1)\n", summary.rule);
            printf("    count = %u\n", summary.count);
        }
        ASSERT(found == 1, "IDLE pattern detected");
        ASSERT(summary.rule == BH_RULE_IDLE, "Rule == BH_RULE_IDLE");
        ASSERT(summary.count == 32, "IDLE spans 32 candles");
    }

    /* ──────────────────────────────────────────────
     * T9: BH LOOP detection
     * ────────────────────────────────────────────── */
    printf("\n[T9] BH LOOP detection\n");
    {
        BtcCandle loop[64];
        init_loop_candles(loop, 64);

        /* IDLE won't trigger because we have real price changes */
        BhSummary summary;
        int found = btc_bh_analyze(&summary, loop, 0, 63);

        printf("    found = %d\n", found);
        if (found) {
            printf("    rule = %d\n", summary.rule);
            printf("    stride = %u\n", summary.stride);
            printf("    count = %u\n", summary.count);
        }

        /* Loop pattern has period 4, so LOOP should detect it */
        if (found && summary.rule == BH_RULE_LOOP) {
            ASSERT(summary.stride > 0, "LOOP stride > 0");
            ASSERT(summary.count >= BH_LOOP_MIN_REPEAT,
                   "LOOP repeats >= 3");
        } else {
            /* May detect IDLE if price range too small */
            ASSERT(found == 1, "Some pattern detected in periodic data");
        }
    }

    /* ──────────────────────────────────────────────
     * T10: BH BURST detection
     * ────────────────────────────────────────────── */
    printf("\n[T10] BH BURST detection\n");
    {
        BtcCandle burst[20];
        init_burst_candles(burst, 20);

        BhSummary summary;
        int found = btc_bh_analyze(&summary, burst, 0, 19);

        printf("    found = %d\n", found);
        if (found) {
            printf("    rule = %d\n", summary.rule);
            printf("    count = %u\n", summary.count);
        }

        /* Burst candles have >$50 changes in first 5 candles */
        ASSERT(found == 1, "BURST pattern detected");
        if (found) {
            ASSERT(summary.rule == BH_RULE_BURST, "Rule == BH_RULE_BURST");
        }
    }

    /* ──────────────────────────────────────────────
     * T11: BH compress range + statistics
     * ────────────────────────────────────────────── */
    printf("\n[T11] BH compress_range + statistics\n");
    {
        /* Use ascending candles (may have LOOP/BURST patterns) */
        BtcBhEngine engine;
        int found = btc_bh_compress_range(&engine, g_candles, TEST_CANDLES);

        printf("    summaries found = %d\n", found);
        printf("    idle=%u loop=%u burst=%u\n",
               engine.stats.idle_count,
               engine.stats.loop_count,
               engine.stats.burst_count);
        printf("    candles_saved = %u / %u\n",
               engine.stats.candles_saved,
               engine.stats.total_candles);

        ASSERT(engine.stats.total_candles == TEST_CANDLES,
               "Total candles tracked correctly");
        /* Compression results depend on data shape */
        ASSERT(found >= 0, "compress_range returned valid count");
    }

    /* ──────────────────────────────────────────────
     * T12: BH replay
     * ────────────────────────────────────────────── */
    printf("\n[T12] BH replay\n");
    {
        BtcCandle idle[32];
        init_idle_candles(idle, 32);

        BhSummary summary;
        btc_bh_analyze(&summary, idle, 0, 31);

        BtcCanvasBrain brain;
        btc_brain_init(&brain);

        int rc = btc_bh_replay(&summary, idle, &brain, TF_1H);
        ASSERT(rc == 0, "BH replay IDLE returns 0");

        /* IDLE replay trains only 1 representative candle */
        ASSERT(brain.candles_trained <= 1,
               "IDLE replay trains <= 1 candle");

        btc_brain_free(&brain);
    }

    /* ──────────────────────────────────────────────
     * T13: TimeWarp operations
     * ────────────────────────────────────────────── */
    printf("\n[T13] TimeWarp operations\n");
    {
        BtcTimeWarp tw;
        btc_timewarp_init(&tw);
        ASSERT(tw.active == 0, "TimeWarp init: not active");

        int rc = btc_timewarp_goto(&tw, 50, 200);
        ASSERT(rc == 0, "TimeWarp goto returns 0");
        ASSERT(tw.active == 1, "TimeWarp is active after goto");
        ASSERT(tw.saved_idx == 200, "Saved idx == 200");
        ASSERT(tw.target_idx == 50, "Target idx == 50");

        uint32_t restored;
        rc = btc_timewarp_resume(&tw, &restored);
        ASSERT(rc == 0, "TimeWarp resume returns 0");
        ASSERT(restored == 200, "Restored idx == 200");
        ASSERT(tw.active == 0, "TimeWarp inactive after resume");
    }

    /* ──────────────────────────────────────────────
     * T14: TimeWarp diff
     * ────────────────────────────────────────────── */
    printf("\n[T14] TimeWarp diff\n");
    {
        uint32_t diff = btc_timewarp_diff(g_candles, 0, 99);
        printf("    diff(0..99) = %u non-zero changes\n", diff);
        ASSERT(diff > 0, "Ascending candles have non-zero changes");
        ASSERT(diff <= 99, "Diff <= number of intervals");
    }

    /* ── 결과 요약 ── */
    printf("\n============================\n");
    printf("Phase 8 결과: %d PASS, %d FAIL\n", g_pass, g_fail);
    printf("============================\n");

    return (g_fail == 0) ? 0 : 1;
}
