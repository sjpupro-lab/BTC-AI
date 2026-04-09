/*
 * test_p3_canvas.c — Phase 3 Canvas AI BTC Core 검증
 * DK-1: float/double 0건.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../core/btc_canvas_brain.h"

/* ── 간단한 테스트 프레임워크 ──────────────────── */
static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); g_pass++; \
    } else { \
        printf("  [FAIL] %s  (line %d)\n", msg, __LINE__); g_fail++; \
    } \
} while(0)

/* ── 테스트 데이터: 상승 캔들 시리즈 ──────────── */
static void make_up_candles(BtcCandle *out, uint32_t n, uint32_t base) {
    for (uint32_t i = 0; i < n; i++) {
        uint32_t price = base + i * 5000;  /* $50/candle → ~17% over 100 candles */
        out[i].timestamp = 1700000000ULL + i * 3600;
        out[i].open_x100  = price;
        out[i].high_x100  = price + 5000;
        out[i].low_x100   = (price > 3000) ? price - 3000 : 0;
        out[i].close_x100 = price + 2000;
        out[i].volume_x10 = 800;
    }
}

/* ── P3-T1: Init / Free ──────────────────────── */
static void test_init_free(void) {
    printf("[T1] Init / Free\n");

    BtcCanvasBrain b;
    int rc = btc_brain_init(&b);
    ASSERT(rc == BTC_OK, "btc_brain_init returns BTC_OK");
    ASSERT(b.brain.canvas.capacity > 0, "canvas capacity > 0");
    ASSERT(b.brain.canvas.capacity == BTC_CANVAS_CELLS,
           "canvas capacity == BTC_CANVAS_CELLS");
    ASSERT(b.regime == REGIME_UNKNOWN, "regime == REGIME_UNKNOWN");
    ASSERT(b.candles_trained == 0, "candles_trained == 0");

    for (int i = 0; i < TF_COUNT; i++) {
        ASSERT(b.stream[i].data != NULL, "stream[i].data != NULL");
        ASSERT(b.stream[i].cap == BTC_STREAM_CAP, "stream[i].cap == BTC_STREAM_CAP");
    }

    btc_brain_free(&b);
    printf("\n");
}

/* ── P3-T2: Train 200 + Predict ──────────────── */
static void test_train_predict(void) {
    printf("[T2] Train 200 candles + Predict\n");

    BtcCanvasBrain b;
    int rc = btc_brain_init(&b);
    ASSERT(rc == BTC_OK, "init OK");

    BtcCandle candles[201];
    make_up_candles(candles, 201, 3000000); /* $30,000 base */

    uint32_t avg_vol_x10 = 800;
    for (uint32_t i = 1; i <= 200; i++) {
        btc_brain_train_candle(&b, TF_1H, &candles[i], &candles[i - 1],
                               avg_vol_x10);
    }

    ASSERT(b.candles_trained == 200, "candles_trained == 200");

    BtcPrediction pred;
    btc_brain_predict(&b, TF_1H, &pred);
    ASSERT(pred.direction != SIGNAL_NONE, "direction != SIGNAL_NONE");
    printf("    direction=%d confidence=%u top_byte=%u\n",
           pred.direction, pred.confidence, pred.top3_bytes[0]);

    btc_brain_free(&b);
    printf("\n");
}

/* ── P3-T3: Regime Detection ─────────────────── */
static void test_regime_detection(void) {
    printf("[T3] Regime detection with ascending candles\n");

    BtcCandle candles[100];
    make_up_candles(candles, 100, 3000000); /* Ascending → BULL */

    MarketRegime regime = btc_detect_regime(candles, 100);
    ASSERT(regime == REGIME_BULL, "ascending candles → REGIME_BULL");

    /* Not enough candles → UNKNOWN */
    MarketRegime regime_short = btc_detect_regime(candles, 10);
    ASSERT(regime_short == REGIME_UNKNOWN, "10 candles → REGIME_UNKNOWN");

    /* NULL → UNKNOWN */
    MarketRegime regime_null = btc_detect_regime(NULL, 100);
    ASSERT(regime_null == REGIME_UNKNOWN, "NULL candles → REGIME_UNKNOWN");

    printf("\n");
}

/* ── P3-T4: Save / Load Roundtrip ────────────── */
static void test_save_load(void) {
    printf("[T4] Save / Load roundtrip\n");

    BtcCanvasBrain b1;
    int rc = btc_brain_init(&b1);
    ASSERT(rc == BTC_OK, "init b1 OK");

    /* Train some data */
    BtcCandle candles[51];
    make_up_candles(candles, 51, 4000000);
    uint32_t avg_vol_x10 = 800;
    for (uint32_t i = 1; i <= 50; i++) {
        btc_brain_train_candle(&b1, TF_15M, &candles[i], &candles[i - 1],
                               avg_vol_x10);
    }

    /* Get prediction from b1 */
    BtcPrediction pred1;
    btc_brain_predict(&b1, TF_15M, &pred1);

    /* Save */
    const char *path = "/tmp/btc_brain_test_p3.cab";
    rc = btc_brain_save(&b1, path);
    ASSERT(rc == 0, "save returns 0");

    /* Load into b2 */
    BtcCanvasBrain b2;
    rc = btc_brain_init(&b2);
    ASSERT(rc == BTC_OK, "init b2 OK");

    rc = btc_brain_load(&b2, path);
    ASSERT(rc == 0, "load returns 0");

    /* Push same window into b2 for prediction */
    BtcByteStream *s1 = &b1.stream[TF_15M];
    for (uint32_t i = 0; i < s1->len; i++) {
        btc_stream_append(&b2.stream[TF_15M],
                          (BtcCandleBytes[]){
                              {s1->data[i], 0, 0, 0}
                          });
    }
    /* Actually, copy the stream data directly for a fair comparison */
    btc_stream_reset(&b2.stream[TF_15M]);
    if (s1->len <= b2.stream[TF_15M].cap) {
        memcpy(b2.stream[TF_15M].data, s1->data, s1->len);
        b2.stream[TF_15M].len = s1->len;
    }

    BtcPrediction pred2;
    btc_brain_predict(&b2, TF_15M, &pred2);

    ASSERT(pred1.direction == pred2.direction,
           "prediction direction matches after load");
    ASSERT(pred1.top3_bytes[0] == pred2.top3_bytes[0],
           "top byte matches after load");

    btc_brain_free(&b1);
    btc_brain_free(&b2);
    printf("\n");
}

/* ── P3-T5: Confidence increases with training ── */
static void test_confidence_growth(void) {
    printf("[T5] Confidence increases with more training\n");

    BtcCanvasBrain b;
    int rc = btc_brain_init(&b);
    ASSERT(rc == BTC_OK, "init OK");

    BtcCandle candles[301];
    make_up_candles(candles, 301, 3000000);
    uint32_t avg_vol_x10 = 800;

    /* Train 50 candles */
    for (uint32_t i = 1; i <= 50; i++) {
        btc_brain_train_candle(&b, TF_1H, &candles[i], &candles[i - 1],
                               avg_vol_x10);
    }

    BtcPrediction pred_early;
    btc_brain_predict(&b, TF_1H, &pred_early);
    uint32_t conf_early = pred_early.confidence;
    printf("    confidence after  50 candles: %u\n", conf_early);

    /* Train 200 more candles */
    for (uint32_t i = 51; i <= 250; i++) {
        btc_brain_train_candle(&b, TF_1H, &candles[i], &candles[i - 1],
                               avg_vol_x10);
    }

    BtcPrediction pred_late;
    btc_brain_predict(&b, TF_1H, &pred_late);
    uint32_t conf_late = pred_late.confidence;
    printf("    confidence after 250 candles: %u\n", conf_late);

    ASSERT(conf_late >= conf_early,
           "confidence after 250 >= confidence after 50");

    btc_brain_free(&b);
    printf("\n");
}

/* ── P3-T6: Determinism ──────────────────────── */
static void test_determinism(void) {
    printf("[T6] Determinism: same training → same prediction\n");

    BtcPrediction preds[2];

    for (int run = 0; run < 2; run++) {
        BtcCanvasBrain b;
        int rc = btc_brain_init(&b);
        ASSERT(rc == BTC_OK, "init OK");

        BtcCandle candles[101];
        make_up_candles(candles, 101, 5000000);
        uint32_t avg_vol_x10 = 800;

        for (uint32_t i = 1; i <= 100; i++) {
            btc_brain_train_candle(&b, TF_5M, &candles[i], &candles[i - 1],
                                   avg_vol_x10);
        }

        btc_brain_predict(&b, TF_5M, &preds[run]);
        btc_brain_free(&b);
    }

    ASSERT(preds[0].direction == preds[1].direction,
           "direction identical across runs");
    ASSERT(preds[0].confidence == preds[1].confidence,
           "confidence identical across runs");
    ASSERT(preds[0].top3_bytes[0] == preds[1].top3_bytes[0],
           "top byte identical across runs");
    ASSERT(preds[0].top3_bytes[1] == preds[1].top3_bytes[1],
           "2nd byte identical across runs");
    ASSERT(preds[0].top3_bytes[2] == preds[1].top3_bytes[2],
           "3rd byte identical across runs");
    ASSERT(preds[0].top3_scores[0] == preds[1].top3_scores[0],
           "top score identical across runs");

    printf("\n");
}

/* ── main ────────────────────────────────────── */
int main(void) {
    printf("=== Phase 3: Canvas AI BTC Core ===\n\n");

    test_init_free();
    test_train_predict();
    test_regime_detection();
    test_save_load();
    test_confidence_growth();
    test_determinism();

    /* ── 결과 요약 ── */
    printf("============================\n");
    printf("Phase 3 결과: %d PASS, %d FAIL\n", g_pass, g_fail);
    printf("============================\n");

    return (g_fail == 0) ? 0 : 1;
}
