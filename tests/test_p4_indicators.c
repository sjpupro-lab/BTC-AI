/*
 * test_p4_indicators.c — Phase 4 기술 지표 엔진 테스트
 * DK-1: float/double 0건. 정수 전용.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../core/btc_types.h"
#include "../core/btc_indicators.h"

/* ── 간단한 테스트 프레임워크 ──────────────────── */
static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); g_pass++; \
    } else { \
        printf("  [FAIL] %s  (line %d)\n", msg, __LINE__); g_fail++; \
    } \
} while(0)

/* ── 테스트 데이터 (상승 추세) ─────────────────── */
static BtcCandle test_candles[100];

static void init_test_candles(void) {
    for (int i = 0; i < 100; i++) {
        uint32_t price = 3500000 + (uint32_t)i * 10000;
        test_candles[i].close_x100 = price;
        test_candles[i].open_x100  = price - 5000;
        test_candles[i].high_x100  = price + 8000;
        test_candles[i].low_x100   = price - 8000;
        test_candles[i].volume_x10 = 1000 + (uint32_t)(i % 20) * 50;
    }
}

/* ── 테스트 데이터 (하락 후 상승, MACD 크로스 검출용) ── */
static BtcCandle macd_candles[100];

static void init_macd_candles(void) {
    /* 99 candles: steady downtrend to deeply embed bearish MACD */
    for (int i = 0; i < 99; i++) {
        uint32_t price = 5000000 - (uint32_t)i * 5000;
        macd_candles[i].close_x100 = price;
        macd_candles[i].open_x100  = price + 3000;
        macd_candles[i].high_x100  = price + 5000;
        macd_candles[i].low_x100   = price - 5000;
        macd_candles[i].volume_x10 = 1000 + (uint32_t)(i % 20) * 50;
    }
    /* Single final candle: massive spike to flip histogram on last bar */
    macd_candles[99].close_x100 = 6000000;
    macd_candles[99].open_x100  = 4505000;
    macd_candles[99].high_x100  = 6100000;
    macd_candles[99].low_x100   = 4500000;
    macd_candles[99].volume_x10 = 10000;
}

int main(void) {
    printf("=== Phase 4: Indicators Engine ===\n\n");

    init_test_candles();
    init_macd_candles();

    /* ── P4-T1: 볼린저 밴드 ──────────────────────── */
    printf("[P4-T1] Bollinger Bands\n");
    {
        BollingerBands bb;
        btc_calc_bb(&bb, test_candles, 100, 20);

        ASSERT(bb.middle_x100 > 0, "BB middle > 0");
        ASSERT(bb.upper_x100 > bb.middle_x100, "BB upper > middle");
        ASSERT(bb.middle_x100 > bb.lower_x100, "BB middle > lower");
        ASSERT(bb.upper_x100 > bb.lower_x100, "BB upper > lower");
        /* In an uptrend, latest close is near top of band -> position > 127 */
        ASSERT(bb.position > 127, "BB position > 127 in uptrend");
        ASSERT(bb.bandwidth_x100 > 0, "BB bandwidth > 0");

        printf("    middle=%u upper=%u lower=%u bw=%u pos=%u\n",
               bb.middle_x100, bb.upper_x100, bb.lower_x100,
               bb.bandwidth_x100, bb.position);
    }

    /* ── P4-T2: RSI (상승 추세 → 과매수) ─────────── */
    printf("\n[P4-T2] RSI (uptrend overbought)\n");
    {
        RsiResult rsi;
        btc_calc_rsi(&rsi, test_candles, 100, 14);

        ASSERT(rsi.value <= 100, "RSI value <= 100");
        ASSERT(rsi.value >= 0,   "RSI value >= 0");
        /* Pure uptrend should give RSI > 70 */
        ASSERT(rsi.value > 70,   "RSI > 70 in pure uptrend");
        ASSERT(rsi.state == 1,   "RSI state == 1 (overbought)");

        printf("    RSI=%u prev=%u state=%u\n",
               rsi.value, rsi.prev_value, rsi.state);
    }

    /* ── P4-T3: MACD (하락→상승 크로스 검출) ─────── */
    printf("\n[P4-T3] MACD (cross detection)\n");
    {
        MacdResult macd;
        btc_calc_macd(&macd, macd_candles, 100, 12, 26, 9);

        /* After downtrend then uptrend, should detect a golden cross */
        ASSERT(macd.cross == 1 || macd.cross == 2 || macd.cross == 0,
               "MACD cross is valid (0, 1, or 2)");
        /* The reversal from down to up should produce a golden cross */
        ASSERT(macd.cross == 1, "MACD golden cross after down->up reversal");
        /* histogram = macd - signal */
        ASSERT(macd.histogram_x1000 == macd.macd_x1000 - macd.signal_x1000,
               "MACD histogram == macd - signal");

        printf("    macd=%d signal=%d hist=%d cross=%u\n",
               macd.macd_x1000, macd.signal_x1000,
               macd.histogram_x1000, macd.cross);
    }

    /* ── P4-T4: 피보나치 (레벨 오름차순) ──────────── */
    printf("\n[P4-T4] Fibonacci (ascending levels)\n");
    {
        FibResult fib;
        btc_calc_fib(&fib, test_candles, 100, 100);

        ASSERT(fib.swing_high_x100 > fib.swing_low_x100,
               "Fib swing_high > swing_low");
        /* Verify all 7 levels are ascending */
        int ascending = 1;
        for (int i = 0; i < 6; i++) {
            if (fib.fib_levels[i] >= fib.fib_levels[i + 1]) {
                ascending = 0;
                break;
            }
        }
        ASSERT(ascending, "Fib levels are strictly ascending");
        ASSERT(fib.fib_levels[0] == fib.swing_low_x100,
               "Fib level[0] == swing_low");
        ASSERT(fib.fib_levels[6] == fib.swing_high_x100,
               "Fib level[6] == swing_high");
        ASSERT(fib.current_zone <= 6, "Fib current_zone <= 6");

        printf("    low=%u high=%u zone=%u\n",
               fib.swing_low_x100, fib.swing_high_x100, fib.current_zone);
        for (int i = 0; i < 7; i++) {
            printf("    fib[%d]=%u\n", i, fib.fib_levels[i]);
        }
    }

    /* ── P4-T5: Volume Profile ────────────────────── */
    printf("\n[P4-T5] Volume Profile\n");
    {
        VolumeProfile vp;
        btc_calc_vp(&vp, test_candles, 100);

        /* POC should be within price range */
        uint32_t min_p = test_candles[0].low_x100;
        uint32_t max_p = test_candles[99].high_x100;
        ASSERT(vp.poc_x100 >= min_p && vp.poc_x100 <= max_p,
               "VP POC within price range");
        ASSERT(vp.vah_x100 >= vp.val_x100, "VP VAH >= VAL");
        ASSERT(vp.poc_x100 > 0, "VP POC > 0");

        printf("    POC=%u VAL=%u VAH=%u\n",
               vp.poc_x100, vp.val_x100, vp.vah_x100);
    }

    /* ── P4-T6: 이치모쿠 ─────────────────────────── */
    printf("\n[P4-T6] Ichimoku\n");
    {
        IchimokuResult ich;
        btc_calc_ichimoku(&ich, test_candles, 100);

        ASSERT(ich.tenkan_x100 > 0, "Ichimoku tenkan > 0");
        ASSERT(ich.kijun_x100 > 0,  "Ichimoku kijun > 0");
        ASSERT(ich.span_a_x100 > 0, "Ichimoku span_a > 0");
        ASSERT(ich.span_b_x100 > 0, "Ichimoku span_b > 0");
        ASSERT(ich.chikou_x100 > 0, "Ichimoku chikou > 0");
        /* In a steady uptrend, cloud should be bullish */
        ASSERT(ich.cloud_bullish == 1, "Ichimoku bullish cloud in uptrend");
        /* Price should be above cloud in strong uptrend */
        ASSERT(ich.price_vs_cloud == 2, "Ichimoku price above cloud in uptrend");

        printf("    tenkan=%u kijun=%u span_a=%u span_b=%u chikou=%u\n",
               ich.tenkan_x100, ich.kijun_x100,
               ich.span_a_x100, ich.span_b_x100, ich.chikou_x100);
        printf("    cloud_bullish=%u price_vs_cloud=%u tk_cross=%u\n",
               ich.cloud_bullish, ich.price_vs_cloud, ich.tk_cross);
    }

    /* ── P4-T7: RSI 범위 검증 (다양한 입력) ──────── */
    printf("\n[P4-T7] RSI range check (always 0~100)\n");
    {
        /* Test with uptrend data */
        RsiResult rsi1;
        btc_calc_rsi(&rsi1, test_candles, 100, 14);
        ASSERT(rsi1.value >= 0 && rsi1.value <= 100,
               "RSI in range [0,100] for uptrend");

        /* Test with downtrend data (first 50 of macd_candles) */
        RsiResult rsi2;
        btc_calc_rsi(&rsi2, macd_candles, 50, 14);
        ASSERT(rsi2.value >= 0 && rsi2.value <= 100,
               "RSI in range [0,100] for downtrend");

        /* Test with small period */
        RsiResult rsi3;
        btc_calc_rsi(&rsi3, test_candles, 100, 7);
        ASSERT(rsi3.value >= 0 && rsi3.value <= 100,
               "RSI in range [0,100] for period=7");

        /* Test with minimum viable data */
        RsiResult rsi4;
        btc_calc_rsi(&rsi4, test_candles, 20, 14);
        ASSERT(rsi4.value >= 0 && rsi4.value <= 100,
               "RSI in range [0,100] for minimal data");

        printf("    uptrend=%u downtrend=%u p7=%u minimal=%u\n",
               rsi1.value, rsi2.value, rsi3.value, rsi4.value);
    }

    /* ── 결과 요약 ── */
    printf("\n============================\n");
    printf("Phase 4 결과: %d PASS, %d FAIL\n", g_pass, g_fail);
    printf("============================\n");

    return (g_fail == 0) ? 0 : 1;
}
