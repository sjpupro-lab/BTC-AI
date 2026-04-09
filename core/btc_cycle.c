/*
 * btc_cycle.c — 소인수분해 사이클 탐지기 + 7단계 S/R 구현
 * DK-1: float/double 0건. 정수 전용.
 * Phase 5.
 */
#include "btc_cycle.h"
#include <string.h>

/* ── btc_factorize ────────────────────────────────
 * Trial-division prime factorization.
 * Returns count of unique prime factors stored in factors_out.
 */
uint32_t btc_factorize(uint32_t N,
                       uint32_t *factors_out, uint32_t max_factors)
{
    if (N <= 1) return 0;

    uint32_t count = 0;
    uint32_t d = 2;

    while (d * d <= N && count < max_factors) {
        if (N % d == 0) {
            factors_out[count++] = d;
            while (N % d == 0) {
                N /= d;
            }
        }
        d++;
    }
    /* remaining N > 1 is the last prime factor */
    if (N > 1 && count < max_factors) {
        factors_out[count++] = N;
    }
    return count;
}

/* ── btc_detect_cycles ────────────────────────────
 * Integer autocorrelation-based cycle detection.
 * Uses int64_t intermediates to avoid overflow.
 */
void btc_detect_cycles(CycleResult *out,
                       const BtcCandle *candles, uint32_t count)
{
    memset(out, 0, sizeof(*out));

    if (count < 4) return;

    /* Compute mean of close prices */
    int64_t sum = 0;
    for (uint32_t i = 0; i < count; i++) {
        sum += (int64_t)candles[i].close_x100;
    }
    int64_t mean = sum / (int64_t)count;

    /* Scale values down by /100 to keep products in int64_t range */
    /* val[i] = (close_x100[i] - mean) / 100 */
    /* Stack limit: use CYCLE_MAX_PERIOD*2+2 at most, but count could be larger.
     * We compute on-the-fly instead of storing full arrays to avoid VLA. */

    /* Compute variance (lag=0 autocorrelation) for normalization */
    int64_t var_sum = 0;
    for (uint32_t i = 0; i < count; i++) {
        int64_t v = ((int64_t)candles[i].close_x100 - mean) / 100;
        var_sum += v * v;
    }
    if (var_sum == 0) return; /* flat data */

    /* Scan lags 2..min(count/2, CYCLE_MAX_PERIOD) */
    uint32_t max_lag = count / 2;
    if (max_lag > CYCLE_MAX_PERIOD) max_lag = CYCLE_MAX_PERIOD;

    /* Store top N peaks: strength_x100 and period */
    /* Initialize to zero (already done by memset) */

    /* We track local maxima: autocorr[lag] > autocorr[lag-1] && autocorr[lag] > autocorr[lag+1] */
    int64_t prev_corr = 0;
    int64_t curr_corr = 0;

    /* Compute autocorrelation for lag=2 first */
    if (max_lag < 3) return;

    /* Helper: compute autocorrelation at a given lag */
    /* We'll compute lag-by-lag and detect peaks */

    /* Pre-compute autocorr for lag=2 */
    {
        int64_t ac = 0;
        uint32_t lag = 2;
        for (uint32_t i = 0; i + lag < count; i++) {
            int64_t vi = ((int64_t)candles[i].close_x100 - mean) / 100;
            int64_t vl = ((int64_t)candles[i + lag].close_x100 - mean) / 100;
            ac += vi * vl;
        }
        prev_corr = ac;
    }

    for (uint32_t lag = 3; lag <= max_lag; lag++) {
        /* Compute autocorrelation at this lag */
        int64_t ac = 0;
        for (uint32_t i = 0; i + lag < count; i++) {
            int64_t vi = ((int64_t)candles[i].close_x100 - mean) / 100;
            int64_t vl = ((int64_t)candles[i + lag].close_x100 - mean) / 100;
            ac += vi * vl;
        }
        curr_corr = ac;

        /* Check if prev_corr (at lag-1) is a local maximum */
        if (lag >= 4 && prev_corr > 0) {
            /* We need autocorr[lag-2] to confirm local max at lag-1.
             * prev_corr is autocorr[lag-1], curr_corr is autocorr[lag].
             * We need the one before prev_corr. Track it. */
        }

        prev_corr = curr_corr;
    }

    /* Simpler approach: compute all autocorrelations, then find peaks */
    /* Use a compact buffer for autocorrelation values */
    /* max_lag <= CYCLE_MAX_PERIOD = 512, so 512 * 8 = 4KB on stack is fine */
    int64_t autocorr[CYCLE_MAX_PERIOD + 1];
    memset(autocorr, 0, sizeof(autocorr));

    for (uint32_t lag = 2; lag <= max_lag; lag++) {
        int64_t ac = 0;
        for (uint32_t i = 0; i + lag < count; i++) {
            int64_t vi = ((int64_t)candles[i].close_x100 - mean) / 100;
            int64_t vl = ((int64_t)candles[i + lag].close_x100 - mean) / 100;
            ac += vi * vl;
        }
        autocorr[lag] = ac;
    }

    /* Find top CYCLE_TOP_N local maxima (peaks) */
    uint8_t found = 0;
    for (uint32_t lag = 3; lag < max_lag && found < CYCLE_TOP_N; lag++) {
        if (autocorr[lag] > 0 &&
            autocorr[lag] >= autocorr[lag - 1] &&
            autocorr[lag] >= autocorr[lag + 1]) {
            /* This is a local maximum — insert in sorted order by strength */
            uint32_t strength = (uint32_t)BTC_DIV_SAFE(autocorr[lag] * 10000,
                                                        var_sum);
            /* Find insertion point (descending by strength) */
            uint8_t pos = found;
            for (uint8_t j = 0; j < found; j++) {
                if (strength > out->periods[j].strength_x100) {
                    pos = j;
                    break;
                }
            }
            /* Shift lower entries down */
            if (found < CYCLE_TOP_N) {
                for (uint8_t j = found; j > pos; j--) {
                    out->periods[j] = out->periods[j - 1];
                }
                found++;
            } else if (pos < CYCLE_TOP_N) {
                for (uint8_t j = CYCLE_TOP_N - 1; j > pos; j--) {
                    out->periods[j] = out->periods[j - 1];
                }
            } else {
                continue; /* weaker than all current top-N, skip */
            }

            out->periods[pos].period = lag;
            out->periods[pos].strength_x100 = strength;
            out->periods[pos].factor_count = (uint8_t)btc_factorize(
                lag, out->periods[pos].factors, 8);
        }
    }

    out->count = found;
    if (found > 0) {
        out->dominant_period = out->periods[0].period;
    }
}

/* ── btc_sr_strength ──────────────────────────────
 * Count candles where high or low touches within tolerance of level.
 */
uint8_t btc_sr_strength(uint32_t level_x100,
                        const BtcCandle *candles, uint32_t count,
                        uint32_t tolerance_x100)
{
    if (count == 0 || level_x100 == 0) return 0;

    uint32_t touches = 0;
    for (uint32_t i = 0; i < count; i++) {
        /* Support touch: low near level */
        int32_t diff_low = (int32_t)candles[i].low_x100 - (int32_t)level_x100;
        if (diff_low < 0) diff_low = -diff_low;
        if ((uint32_t)diff_low <= tolerance_x100) {
            touches++;
        }

        /* Resistance touch: high near level */
        int32_t diff_high = (int32_t)candles[i].high_x100 - (int32_t)level_x100;
        if (diff_high < 0) diff_high = -diff_high;
        if ((uint32_t)diff_high <= tolerance_x100) {
            touches++;
        }
    }

    uint32_t result = touches * 20;
    if (result > 255) result = 255;
    return (uint8_t)result;
}

/* ── btc_calc_sr7 ─────────────────────────────────
 * 7-level S/R using factorization-based natural division.
 */
void btc_calc_sr7(SR7Levels *out,
                  const BtcCandle *candles, uint32_t count,
                  uint32_t lookback)
{
    memset(out, 0, sizeof(*out));

    if (count == 0) return;

    /* Determine window */
    uint32_t start = 0;
    if (count > lookback && lookback > 0) {
        start = count - lookback;
    }

    /* Find high and low in lookback window */
    uint32_t high = candles[start].high_x100;
    uint32_t low  = candles[start].low_x100;
    for (uint32_t i = start + 1; i < count; i++) {
        if (candles[i].high_x100 > high) high = candles[i].high_x100;
        if (candles[i].low_x100  < low)  low  = candles[i].low_x100;
    }

    uint32_t range = high - low;
    if (range == 0) {
        /* Flat data: all levels same */
        for (int k = 0; k < 7; k++) {
            out->levels[k] = low;
            out->strength[k] = 0;
        }
        return;
    }

    /* Start with 7 equally-spaced levels: low + range*k/6 for k=0..6 */
    uint32_t base_levels[7];
    for (int k = 0; k < 7; k++) {
        base_levels[k] = low + (uint32_t)BTC_DIV_SAFE((uint64_t)range * (uint32_t)k, 6);
    }

    /* Find all significant divisors of range (up to 64) */
    uint32_t divisors[64];
    uint32_t div_count = 0;

    /* Collect divisors by iterating up to sqrt(range) */
    for (uint32_t d = 1; (uint64_t)d * d <= range && div_count < 60; d++) {
        if (range % d == 0) {
            divisors[div_count++] = d;
            uint32_t pair = range / d;
            if (pair != d && div_count < 64) {
                divisors[div_count++] = pair;
            }
        }
    }

    /* For each level, snap to nearest divisor-based grid position */
    for (int k = 0; k < 7; k++) {
        uint32_t best_level = base_levels[k];
        uint32_t best_dist = UINT32_MAX;

        for (uint32_t di = 0; di < div_count; di++) {
            uint32_t d = divisors[di];
            if (d == 0) continue;
            uint32_t step = range / d;
            if (step == 0) continue;

            /* Find nearest multiple: candidate = low + step * round((base - low) / step) */
            uint32_t offset = base_levels[k] - low;
            uint32_t mult = BTC_DIV_SAFE(offset + step / 2, step); /* rounded division */
            uint32_t candidate = low + step * mult;

            /* Clamp candidate to [low, high] */
            if (candidate < low) candidate = low;
            if (candidate > high) candidate = high;

            int32_t dist = (int32_t)candidate - (int32_t)base_levels[k];
            if (dist < 0) dist = -dist;
            if ((uint32_t)dist < best_dist) {
                best_dist = (uint32_t)dist;
                best_level = candidate;
            }
        }

        out->levels[k] = best_level;
    }

    /* Ensure ascending order (simple insertion sort) */
    for (int i = 1; i < 7; i++) {
        uint32_t key = out->levels[i];
        int j = i - 1;
        while (j >= 0 && out->levels[j] > key) {
            out->levels[j + 1] = out->levels[j];
            j--;
        }
        out->levels[j + 1] = key;
    }

    /* Compute strength for each level */
    uint32_t tol = range / 50; /* 2% of range as tolerance */
    if (tol == 0) tol = 1;
    uint32_t window_count = count - start;
    for (int k = 0; k < 7; k++) {
        out->strength[k] = btc_sr_strength(out->levels[k],
                                           &candles[start], window_count,
                                           tol);
    }
}
