/*
 * test_p5_cycle.c — Phase 5 사이클 탐지 + S/R 검증
 * DK-1: float/double 0건. 정수 전용.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../core/btc_types.h"
#include "../core/btc_cycle.h"

/* ── 간단한 테스트 프레임워크 ──────────────────── */
static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); g_pass++; \
    } else { \
        printf("  [FAIL] %s  (line %d)\n", msg, __LINE__); g_fail++; \
    } \
} while(0)

int main(void) {
    printf("=== Phase 5: Cycle Detection & S/R ===\n\n");

    /* P5-T1: btc_factorize(12) == 2 unique factors (2, 3) */
    printf("[T1] btc_factorize(12)\n");
    {
        uint32_t factors[8] = {0};
        uint32_t n = btc_factorize(12, factors, 8);
        ASSERT(n == 2,          "12 has 2 unique prime factors");
        ASSERT(factors[0] == 2, "12: first factor is 2");
        ASSERT(factors[1] == 3, "12: second factor is 3");
    }

    /* P5-T2: btc_factorize(360) == 3 factors (2, 3, 5) */
    printf("\n[T2] btc_factorize(360)\n");
    {
        uint32_t factors[8] = {0};
        uint32_t n = btc_factorize(360, factors, 8);
        ASSERT(n == 3,          "360 has 3 unique prime factors");
        ASSERT(factors[0] == 2, "360: first factor is 2");
        ASSERT(factors[1] == 3, "360: second factor is 3");
        ASSERT(factors[2] == 5, "360: third factor is 5");
    }

    /* P5-T3: btc_factorize(17) == 1 factor (17, prime) */
    printf("\n[T3] btc_factorize(17)\n");
    {
        uint32_t factors[8] = {0};
        uint32_t n = btc_factorize(17, factors, 8);
        ASSERT(n == 1,           "17 has 1 unique prime factor");
        ASSERT(factors[0] == 17, "17: factor is 17 (prime)");
    }

    /* P5-T4: btc_factorize(1) == 0 factors */
    printf("\n[T4] btc_factorize(1)\n");
    {
        uint32_t factors[8] = {0};
        uint32_t n = btc_factorize(1, factors, 8);
        ASSERT(n == 0, "1 has 0 prime factors");
    }

    /* P5-T5: Cycle detection on periodic data (24-candle period) */
    printf("\n[T5] Cycle detection: 24-candle periodic data\n");
    {
        BtcCandle cyclic[200];
        memset(cyclic, 0, sizeof(cyclic));
        for (int i = 0; i < 200; i++) {
            uint32_t phase = (uint32_t)(i % 24);
            uint32_t price = 3500000 + (phase < 12 ? phase * 10000 : (24 - phase) * 10000);
            cyclic[i].close_x100 = price;
            cyclic[i].high_x100  = price + 5000;
            cyclic[i].low_x100   = price - 5000;
            cyclic[i].open_x100  = price;
            cyclic[i].volume_x10 = 1000;
        }

        CycleResult cr;
        btc_detect_cycles(&cr, cyclic, 200);

        ASSERT(cr.count > 0, "At least one cycle period detected");

        /* Dominant period should be ~24 (or a harmonic like 24, 48, 12) */
        uint8_t found_24 = 0;
        for (uint8_t j = 0; j < cr.count; j++) {
            if (cr.periods[j].period == 24) {
                found_24 = 1;
                break;
            }
        }
        ASSERT(found_24, "Period 24 detected in top cycles");
        ASSERT(cr.dominant_period == 24 ||
               cr.dominant_period == 48 ||
               cr.dominant_period == 12,
               "Dominant period is 24 or harmonic (12/48)");
    }

    /* P5-T6: SR7 levels ascending order */
    printf("\n[T6] SR7 levels ascending order\n");
    {
        BtcCandle candles[50];
        memset(candles, 0, sizeof(candles));
        for (int i = 0; i < 50; i++) {
            /* Price oscillates between 3000000 and 4000000 */
            uint32_t base = 3000000 + (uint32_t)(i % 10) * 100000;
            candles[i].close_x100 = base;
            candles[i].high_x100  = base + 50000;
            candles[i].low_x100   = base - 50000;
            candles[i].open_x100  = base;
            candles[i].volume_x10 = 500;
        }

        SR7Levels sr;
        btc_calc_sr7(&sr, candles, 50, 50);

        uint8_t ascending = 1;
        for (int k = 1; k < 7; k++) {
            if (sr.levels[k] < sr.levels[k - 1]) {
                ascending = 0;
                break;
            }
        }
        ASSERT(ascending, "SR7 levels are in ascending order");
        ASSERT(sr.levels[0] <= sr.levels[6], "Level[0] <= Level[6]");
    }

    /* P5-T7: SR7 strength values in range 0~255 */
    printf("\n[T7] SR7 strength range 0~255\n");
    {
        BtcCandle candles[50];
        memset(candles, 0, sizeof(candles));
        for (int i = 0; i < 50; i++) {
            uint32_t base = 3000000 + (uint32_t)(i % 10) * 100000;
            candles[i].close_x100 = base;
            candles[i].high_x100  = base + 50000;
            candles[i].low_x100   = base - 50000;
            candles[i].open_x100  = base;
            candles[i].volume_x10 = 500;
        }

        SR7Levels sr;
        btc_calc_sr7(&sr, candles, 50, 50);

        /* uint8_t range is inherently 0~255; verify type size as proxy */
        ASSERT(sizeof(sr.strength[0]) == 1,
               "SR7 strength element is uint8_t (1 byte, range 0~255)");

        /* At least some levels should have nonzero strength */
        uint8_t has_nonzero = 0;
        for (int k = 0; k < 7; k++) {
            if (sr.strength[k] > 0) {
                has_nonzero = 1;
                break;
            }
        }
        ASSERT(has_nonzero, "At least one SR7 level has nonzero strength");
    }

    /* P5-T8: btc_sr_strength edge case: all zeros returns 0 */
    printf("\n[T8] btc_sr_strength edge case: zero count\n");
    {
        uint8_t s = btc_sr_strength(0, NULL, 0, 1000);
        ASSERT(s == 0, "sr_strength with zero level/count returns 0");

        BtcCandle empty;
        memset(&empty, 0, sizeof(empty));
        uint8_t s2 = btc_sr_strength(3500000, &empty, 0, 1000);
        ASSERT(s2 == 0, "sr_strength with count=0 returns 0");
    }

    /* ── 결과 요약 ── */
    printf("\n============================\n");
    printf("Phase 5 결과: %d PASS, %d FAIL\n", g_pass, g_fail);
    printf("============================\n");

    return (g_fail == 0) ? 0 : 1;
}
