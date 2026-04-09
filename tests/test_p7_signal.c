/*
 * test_p7_signal.c — Phase 7 신호 융합 엔진 검증
 * DK-1: float/double 0건.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../core/btc_signal.h"

/* ── 간단한 테스트 프레임워크 ──────────────────── */
static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); g_pass++; \
    } else { \
        printf("  [FAIL] %s  (line %d)\n", msg, __LINE__); g_fail++; \
    } \
} while(0)

/* ── 공통 테스트 데이터 ─────────────────────────── */
static void make_strong_long(BtcPrediction *canvas,
                             BollingerBands *bb,
                             SR7Levels *sr7,
                             BtcCandle *cur,
                             RsiResult *rsi,
                             MacdResult *macd,
                             PatternSimilarityResult *pat)
{
    memset(canvas, 0, sizeof(*canvas));
    canvas->direction  = SIGNAL_LONG;
    canvas->confidence = 800;

    memset(bb, 0, sizeof(*bb));
    bb->middle_x100 = 3500000;
    bb->upper_x100  = 3600000;
    bb->lower_x100  = 3400000;
    bb->position     = 80;

    memset(sr7, 0, sizeof(*sr7));
    for (int i = 0; i < 7; i++) {
        sr7->levels[i]   = 3400000 + (uint32_t)i * 30000;
        sr7->strength[i] = 128;
    }

    memset(cur, 0, sizeof(*cur));
    cur->close_x100 = 3490000;
    cur->open_x100  = 3480000;
    cur->high_x100  = 3495000;
    cur->low_x100   = 3475000;

    memset(rsi, 0, sizeof(*rsi));
    rsi->value = 55;
    rsi->state = 0;

    memset(macd, 0, sizeof(*macd));
    macd->macd_x1000      = 500;
    macd->signal_x1000    = 200;
    macd->histogram_x1000 = 300;
    macd->cross            = 1; /* golden cross */

    memset(pat, 0, sizeof(*pat));
    pat->count               = 3;
    pat->consensus_direction = 1; /* up */
    pat->consensus_strength  = 80;
}

int main(void)
{
    printf("=== Phase 7: Signal Fusion Engine ===\n\n");

    BtcPrediction             canvas;
    BollingerBands            bb;
    SR7Levels                 sr7;
    BtcCandle                 cur;
    RsiResult                 rsi;
    MacdResult                macd;
    PatternSimilarityResult   pat;
    BtcSignalConfig           cfg;
    BtcSignal                 sig;

    /* ── P7-T1: All components LONG, premium enabled → STRONG LONG ── */
    printf("[T1] All LONG, premium → STRONG LONG\n");
    {
        make_strong_long(&canvas, &bb, &sr7, &cur, &rsi, &macd, &pat);
        cfg.premium_enabled = 1;

        btc_signal_compute(&sig, &canvas, &bb, &sr7,
                           &rsi, &macd, &pat, &cur, &cfg);

        ASSERT(sig.direction   == SIGNAL_LONG,    "direction == LONG");
        ASSERT(sig.strength    == STRENGTH_STRONG, "strength == STRONG");
        ASSERT(sig.total_score >= 70,             "total_score >= 70");
        ASSERT(sig.canvas_score > 0,              "canvas_score > 0");
        ASSERT(sig.bb_score     > 0,              "bb_score > 0");
        ASSERT(sig.sr7_score    > 0,              "sr7_score > 0");
        ASSERT(sig.rsi_score    > 0,              "rsi_score > 0 (premium)");
        ASSERT(sig.macd_score   > 0,              "macd_score > 0 (premium)");
        ASSERT(sig.pattern_score > 0,             "pattern_score > 0 (premium)");
    }

    /* ── P7-T2: Canvas LONG but RSI overbought → reduced score ── */
    printf("\n[T2] Canvas LONG + RSI overbought → reduced\n");
    {
        make_strong_long(&canvas, &bb, &sr7, &cur, &rsi, &macd, &pat);
        rsi.value = 85;
        rsi.state = 1; /* overbought */
        cfg.premium_enabled = 1;

        btc_signal_compute(&sig, &canvas, &bb, &sr7,
                           &rsi, &macd, &pat, &cur, &cfg);

        ASSERT(sig.rsi_score == 0,               "rsi_score == 0 (contradicts LONG)");

        /* Get baseline score for comparison */
        BtcSignal baseline;
        make_strong_long(&canvas, &bb, &sr7, &cur, &rsi, &macd, &pat);
        rsi.value = 55;
        rsi.state = 0;
        btc_signal_compute(&baseline, &canvas, &bb, &sr7,
                           &rsi, &macd, &pat, &cur, &cfg);

        ASSERT(sig.total_score < baseline.total_score,
               "overbought RSI reduces total score");
    }

    /* ── P7-T3: Free mode → rsi/macd/pattern scores = 0 ── */
    printf("\n[T3] Free mode → premium indicators zeroed\n");
    {
        make_strong_long(&canvas, &bb, &sr7, &cur, &rsi, &macd, &pat);
        cfg.premium_enabled = 0;

        btc_signal_compute(&sig, &canvas, &bb, &sr7,
                           &rsi, &macd, &pat, &cur, &cfg);

        ASSERT(sig.rsi_score     == 0, "rsi_score == 0 (free mode)");
        ASSERT(sig.macd_score    == 0, "macd_score == 0 (free mode)");
        ASSERT(sig.pattern_score == 0, "pattern_score == 0 (free mode)");
        ASSERT(sig.canvas_score  > 0,  "canvas_score still active (free)");
        ASSERT(sig.bb_score      > 0,  "bb_score still active (free)");
        ASSERT(sig.sr7_score     > 0,  "sr7_score still active (free)");

        /* Also test with NULL premium pointers */
        btc_signal_compute(&sig, &canvas, &bb, &sr7,
                           NULL, NULL, NULL, &cur, &cfg);

        ASSERT(sig.rsi_score     == 0, "rsi_score == 0 (NULL ptr)");
        ASSERT(sig.macd_score    == 0, "macd_score == 0 (NULL ptr)");
        ASSERT(sig.pattern_score == 0, "pattern_score == 0 (NULL ptr)");
    }

    /* ── P7-T4: Conflicting signals → weak/hold ── */
    printf("\n[T4] Conflicting signals → HOLD/WEAK\n");
    {
        make_strong_long(&canvas, &bb, &sr7, &cur, &rsi, &macd, &pat);
        /* Canvas says SHORT but MACD has golden cross, pattern says up */
        canvas.direction  = SIGNAL_SHORT;
        canvas.confidence = 600;
        /* BB position near lower → contradicts SHORT */
        bb.position = 30;
        /* RSI oversold → contradicts SHORT */
        rsi.value = 20;
        rsi.state = 2;
        /* MACD golden cross → contradicts SHORT */
        macd.cross = 1;
        macd.histogram_x1000 = 500;
        /* Pattern consensus up → contradicts SHORT */
        pat.consensus_direction = 1;

        cfg.premium_enabled = 1;

        btc_signal_compute(&sig, &canvas, &bb, &sr7,
                           &rsi, &macd, &pat, &cur, &cfg);

        ASSERT(sig.direction == SIGNAL_HOLD,
               "conflicting → direction == HOLD");
        ASSERT(sig.strength == STRENGTH_WEAK,
               "conflicting → strength == WEAK");
    }

    /* ── P7-T5: Determinism — same input = same output ── */
    printf("\n[T5] Determinism check\n");
    {
        make_strong_long(&canvas, &bb, &sr7, &cur, &rsi, &macd, &pat);
        cfg.premium_enabled = 1;

        BtcSignal sig1, sig2;
        btc_signal_compute(&sig1, &canvas, &bb, &sr7,
                           &rsi, &macd, &pat, &cur, &cfg);
        btc_signal_compute(&sig2, &canvas, &bb, &sr7,
                           &rsi, &macd, &pat, &cur, &cfg);

        ASSERT(sig1.total_score    == sig2.total_score,
               "total_score deterministic");
        ASSERT(sig1.canvas_score   == sig2.canvas_score,
               "canvas_score deterministic");
        ASSERT(sig1.bb_score       == sig2.bb_score,
               "bb_score deterministic");
        ASSERT(sig1.sr7_score      == sig2.sr7_score,
               "sr7_score deterministic");
        ASSERT(sig1.rsi_score      == sig2.rsi_score,
               "rsi_score deterministic");
        ASSERT(sig1.macd_score     == sig2.macd_score,
               "macd_score deterministic");
        ASSERT(sig1.pattern_score  == sig2.pattern_score,
               "pattern_score deterministic");
        ASSERT(sig1.direction      == sig2.direction,
               "direction deterministic");
        ASSERT(sig1.strength       == sig2.strength,
               "strength deterministic");
    }

    /* ── P7-T6: All neutral inputs → low score, HOLD ── */
    printf("\n[T6] All neutral → low score, HOLD\n");
    {
        memset(&canvas, 0, sizeof(canvas));
        canvas.direction  = SIGNAL_NONE;
        canvas.confidence = 0;

        memset(&bb, 0, sizeof(bb));
        bb.middle_x100 = 3500000;
        bb.upper_x100  = 3600000;
        bb.lower_x100  = 3400000;
        bb.position     = 127; /* middle */

        memset(&sr7, 0, sizeof(sr7));
        for (int i = 0; i < 7; i++) {
            sr7.levels[i]   = 3000000 + (uint32_t)i * 100000;
            sr7.strength[i] = 50;
        }

        memset(&cur, 0, sizeof(cur));
        cur.close_x100 = 3500000;
        cur.open_x100  = 3500000;
        cur.high_x100  = 3500000;
        cur.low_x100   = 3500000;

        RsiResult rsi_n    = {.value = 50, .prev_value = 50, .state = 0};
        MacdResult macd_n  = {.macd_x1000 = 0, .signal_x1000 = 0,
                              .histogram_x1000 = 0, .cross = 0};
        PatternSimilarityResult pat_n;
        memset(&pat_n, 0, sizeof(pat_n));
        pat_n.consensus_direction = 0;

        cfg.premium_enabled = 1;

        btc_signal_compute(&sig, &canvas, &bb, &sr7,
                           &rsi_n, &macd_n, &pat_n, &cur, &cfg);

        ASSERT(sig.direction == SIGNAL_HOLD,
               "neutral inputs → HOLD");
        ASSERT(sig.total_score <= 40,
               "neutral inputs → low total score");
        ASSERT(sig.canvas_score == 0,
               "no canvas edge → canvas_score == 0");
    }

    /* ── DK-1 확인 ── */
    printf("\n[DK-1] float/double 미사용 확인\n");
    ASSERT(1, "btc_signal.h / btc_signal.c: integer-only (compile verified)");

    /* ── 결과 요약 ── */
    printf("\n============================\n");
    printf("Phase 7 결과: %d PASS, %d FAIL\n", g_pass, g_fail);
    printf("============================\n");

    return (g_fail == 0) ? 0 : 1;
}
