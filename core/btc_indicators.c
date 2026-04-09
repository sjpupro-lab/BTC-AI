/*
 * btc_indicators.c — 기술 지표 엔진 구현
 * DK-1: float/double 0건. 정수 전용.
 * Phase 4.
 */
#include "btc_indicators.h"
#include <string.h>

/* ── 헬퍼: 정수 제곱근 (Newton's method) ─────────── */
static uint32_t isqrt_u64(uint64_t n)
{
    if (n == 0) return 0;
    uint64_t x = n;
    uint64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return (uint32_t)x;
}

/* ── 헬퍼: 구간 내 최고가/최저가 ─────────────────── */
static uint32_t highest_high(const BtcCandle *candles, uint32_t start, uint32_t len)
{
    uint32_t h = 0;
    for (uint32_t i = start; i < start + len; i++) {
        if (candles[i].high_x100 > h) h = candles[i].high_x100;
    }
    return h;
}

static uint32_t lowest_low(const BtcCandle *candles, uint32_t start, uint32_t len)
{
    uint32_t l = UINT32_MAX;
    for (uint32_t i = start; i < start + len; i++) {
        if (candles[i].low_x100 < l) l = candles[i].low_x100;
    }
    return l;
}

/* ══════════════════════════════════════════════════
 *  볼린저 밴드
 * ══════════════════════════════════════════════════ */
void btc_calc_bb(BollingerBands *out,
                 const BtcCandle *candles, uint32_t count,
                 uint32_t period)
{
    memset(out, 0, sizeof(*out));
    if (!candles || count == 0 || period == 0 || count < period) return;

    /* SMA: use the last `period` candles */
    uint32_t start = count - period;
    uint64_t sum = 0;
    for (uint32_t i = start; i < count; i++) {
        sum += candles[i].close_x100;
    }
    uint32_t mean = (uint32_t)(sum / period);
    out->middle_x100 = mean;

    /* Standard deviation (integer) */
    /* Scale down diffs by 100 before squaring to avoid overflow */
    uint64_t sum_sq = 0;
    for (uint32_t i = start; i < count; i++) {
        int64_t diff = (int64_t)candles[i].close_x100 - (int64_t)mean;
        int64_t diff_scaled = diff / 100; /* scale down */
        sum_sq += (uint64_t)(diff_scaled * diff_scaled);
    }
    uint64_t variance = sum_sq / period;
    uint32_t stddev_scaled = isqrt_u64(variance); /* sqrt of (diff/100)^2 */
    uint32_t stddev = stddev_scaled * 100;         /* scale back up */

    uint32_t two_sigma = 2 * stddev;
    out->upper_x100 = mean + two_sigma;
    out->lower_x100 = (mean > two_sigma) ? (mean - two_sigma) : 0;

    /* bandwidth_x100 = (upper - lower) * 100 / middle, clamped to 255 */
    uint32_t bw = BTC_DIV_SAFE((out->upper_x100 - out->lower_x100) * 100,
                                out->middle_x100);
    out->bandwidth_x100 = (uint8_t)BTC_CLAMP(bw, 0, 255);

    /* position: map current price between lower(0) and upper(254) */
    uint32_t cur = candles[count - 1].close_x100;
    uint32_t range = out->upper_x100 - out->lower_x100;
    if (range == 0) {
        out->position = 127;
    } else {
        int32_t pos;
        if (cur <= out->lower_x100) {
            pos = 0;
        } else if (cur >= out->upper_x100) {
            pos = 254;
        } else {
            pos = (int32_t)((uint64_t)(cur - out->lower_x100) * 254 / range);
        }
        out->position = (uint8_t)BTC_CLAMP(pos, 0, 254);
    }
}

/* ══════════════════════════════════════════════════
 *  RSI (Wilder's EMA)
 * ══════════════════════════════════════════════════ */
void btc_calc_rsi(RsiResult *out,
                  const BtcCandle *candles, uint32_t count,
                  uint32_t period)
{
    memset(out, 0, sizeof(*out));
    if (!candles || count < period + 2 || period == 0) return;

    /* First pass: sum gains and losses over the initial `period` window */
    uint64_t sum_gain = 0;
    uint64_t sum_loss = 0;
    for (uint32_t i = 1; i <= period; i++) {
        int64_t change = (int64_t)candles[i].close_x100
                       - (int64_t)candles[i - 1].close_x100;
        if (change > 0) sum_gain += (uint64_t)change;
        else            sum_loss += (uint64_t)(-change);
    }

    /* avg_gain_x100 and avg_loss_x100 (extra ×100 for precision) */
    uint64_t avg_gain_x100 = sum_gain * 100 / period;
    uint64_t avg_loss_x100 = sum_loss * 100 / period;

    /* EMA smoothing through the rest of the candles */
    uint8_t prev_rsi = 50; /* default */
    for (uint32_t i = period + 1; i < count; i++) {
        int64_t change = (int64_t)candles[i].close_x100
                       - (int64_t)candles[i - 1].close_x100;
        uint64_t cur_gain_x100 = (change > 0) ? (uint64_t)change * 100 : 0;
        uint64_t cur_loss_x100 = (change < 0) ? (uint64_t)(-change) * 100 : 0;

        avg_gain_x100 = (avg_gain_x100 * (period - 1) + cur_gain_x100) / period;
        avg_loss_x100 = (avg_loss_x100 * (period - 1) + cur_loss_x100) / period;

        /* Compute RSI before last iteration to capture prev */
        if (i == count - 2) {
            if (avg_loss_x100 == 0) {
                prev_rsi = 100;
            } else {
                uint64_t rs_x100 = avg_gain_x100 * 100 / avg_loss_x100;
                uint64_t rsi_val = 100 - BTC_DIV_SAFE(10000, (100 + rs_x100));
                prev_rsi = (uint8_t)BTC_CLAMP(rsi_val, 0, 100);
            }
        }
    }

    /* Final RSI value */
    uint8_t rsi;
    if (avg_loss_x100 == 0) {
        rsi = 100;
    } else {
        uint64_t rs_x100 = avg_gain_x100 * 100 / avg_loss_x100;
        uint64_t rsi_val = 100 - BTC_DIV_SAFE(10000, (100 + rs_x100));
        rsi = (uint8_t)BTC_CLAMP(rsi_val, 0, 100);
    }

    out->value = rsi;
    out->prev_value = prev_rsi;

    /* State detection */
    if (rsi > 70)      out->state = 1; /* overbought */
    else if (rsi < 30) out->state = 2; /* oversold */
    else               out->state = 0; /* normal */
}

/* ══════════════════════════════════════════════════
 *  MACD
 * ══════════════════════════════════════════════════ */
void btc_calc_macd(MacdResult *out,
                   const BtcCandle *candles, uint32_t count,
                   uint32_t fast, uint32_t slow, uint32_t signal_p)
{
    memset(out, 0, sizeof(*out));
    if (!candles || count == 0 || fast == 0 || slow == 0 || signal_p == 0) return;
    if (count < slow + signal_p) return;

    /* EMA multipliers ×10000 */
    uint32_t mult_fast = 2 * 10000 / (fast + 1);
    uint32_t mult_slow = 2 * 10000 / (slow + 1);
    uint32_t mult_sig  = 2 * 10000 / (signal_p + 1);

    /* Initialize fast EMA with first candle's close */
    int64_t ema_fast = (int64_t)candles[0].close_x100;
    int64_t ema_slow = (int64_t)candles[0].close_x100;

    /* We need to store MACD values for signal line EMA.
     * Only need the last signal_p+1 values for detection. */
    /* Use a rolling approach: track signal EMA and prev histogram */
    int64_t signal_ema = 0; /* will be initialized on first MACD value */
    int32_t prev_histogram = 0;
    int32_t cur_histogram = 0;
    int signal_init = 0;
    uint32_t macd_count = 0;

    for (uint32_t i = 1; i < count; i++) {
        int64_t close = (int64_t)candles[i].close_x100;

        /* EMA update: ema = (close * mult + prev_ema * (10000 - mult)) / 10000 */
        ema_fast = (close * (int64_t)mult_fast
                    + ema_fast * (int64_t)(10000 - mult_fast)) / 10000;
        ema_slow = (close * (int64_t)mult_slow
                    + ema_slow * (int64_t)(10000 - mult_slow)) / 10000;

        /* Only compute MACD after we have enough data for slow EMA */
        if (i >= slow - 1) {
            int64_t macd_val = ema_fast - ema_slow; /* in price_x100 units */

            if (!signal_init) {
                signal_ema = macd_val;
                signal_init = 1;
            } else {
                signal_ema = (macd_val * (int64_t)mult_sig
                              + signal_ema * (int64_t)(10000 - mult_sig)) / 10000;
            }

            prev_histogram = cur_histogram;
            /* Scale to x1000: macd_val is in x100 units, multiply by 10 */
            cur_histogram = (int32_t)((macd_val - signal_ema) * 10);
            macd_count++;

            /* Store final values on last candle */
            if (i == count - 1) {
                out->macd_x1000 = (int32_t)(macd_val * 10);
                out->signal_x1000 = (int32_t)(signal_ema * 10);
                out->histogram_x1000 = cur_histogram;
            }
        }
    }

    /* Cross detection: sign change of histogram */
    if (macd_count >= 2) {
        if (prev_histogram <= 0 && cur_histogram > 0) {
            out->cross = 1; /* golden cross */
        } else if (prev_histogram >= 0 && cur_histogram < 0) {
            out->cross = 2; /* dead cross */
        } else {
            out->cross = 0;
        }
    }
}

/* ══════════════════════════════════════════════════
 *  Volume Profile
 * ══════════════════════════════════════════════════ */
void btc_calc_vp(VolumeProfile *out,
                 const BtcCandle *candles, uint32_t count)
{
    memset(out, 0, sizeof(*out));
    if (!candles || count == 0) return;

    /* Find price range */
    uint32_t min_price = UINT32_MAX;
    uint32_t max_price = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (candles[i].low_x100 < min_price) min_price = candles[i].low_x100;
        if (candles[i].high_x100 > max_price) max_price = candles[i].high_x100;
    }

    if (max_price <= min_price) {
        /* Flat market: put everything in first bin */
        out->price_levels[0] = min_price;
        out->poc_x100 = min_price;
        out->val_x100 = min_price;
        out->vah_x100 = min_price;
        return;
    }

    uint32_t range = max_price - min_price;
    uint32_t bin_size = range / VP_BINS;
    if (bin_size == 0) bin_size = 1;

    /* Set up price levels (midpoint of each bin) */
    for (int b = 0; b < VP_BINS; b++) {
        out->price_levels[b] = min_price + bin_size * (uint32_t)b + bin_size / 2;
    }

    /* Accumulate volume into bins based on close price */
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (candles[i].close_x100 - min_price) / bin_size;
        if (idx >= VP_BINS) idx = VP_BINS - 1;
        out->volume[idx] += candles[i].volume_x10;
    }

    /* POC: bin with maximum volume */
    uint32_t poc_idx = 0;
    uint32_t max_vol = 0;
    uint64_t total_vol = 0;
    for (int b = 0; b < VP_BINS; b++) {
        total_vol += out->volume[b];
        if (out->volume[b] > max_vol) {
            max_vol = out->volume[b];
            poc_idx = (uint32_t)b;
        }
    }
    out->poc_x100 = out->price_levels[poc_idx];

    /* Value Area: 70% of total volume centered around POC */
    uint64_t target_vol = total_vol * 70 / 100;
    uint64_t area_vol = out->volume[poc_idx];
    uint32_t va_low = poc_idx;
    uint32_t va_high = poc_idx;

    while (area_vol < target_vol) {
        uint32_t add_low  = (va_low > 0) ? out->volume[va_low - 1] : 0;
        uint32_t add_high = (va_high < VP_BINS - 1)
                            ? out->volume[va_high + 1] : 0;

        if (add_low == 0 && add_high == 0) break;

        if (add_low >= add_high && va_low > 0) {
            va_low--;
            area_vol += out->volume[va_low];
        } else if (va_high < VP_BINS - 1) {
            va_high++;
            area_vol += out->volume[va_high];
        } else if (va_low > 0) {
            va_low--;
            area_vol += out->volume[va_low];
        } else {
            break;
        }
    }

    out->val_x100 = out->price_levels[va_low];
    out->vah_x100 = out->price_levels[va_high];
}

/* ══════════════════════════════════════════════════
 *  피보나치
 * ══════════════════════════════════════════════════ */
void btc_calc_fib(FibResult *out,
                  const BtcCandle *candles, uint32_t count,
                  uint32_t lookback)
{
    memset(out, 0, sizeof(*out));
    if (!candles || count == 0 || lookback == 0) return;

    uint32_t lb = (lookback > count) ? count : lookback;
    uint32_t start = count - lb;

    /* Find swing high and swing low in lookback window */
    uint32_t hi = 0;
    uint32_t lo = UINT32_MAX;
    for (uint32_t i = start; i < count; i++) {
        if (candles[i].high_x100 > hi) hi = candles[i].high_x100;
        if (candles[i].low_x100 < lo)  lo = candles[i].low_x100;
    }

    out->swing_high_x100 = hi;
    out->swing_low_x100  = lo;

    uint32_t range = hi - lo;

    /* Fibonacci levels: low + range * ratio/1000 */
    /* Ratios: 0, 236, 382, 500, 618, 786, 1000 */
    static const uint32_t fib_ratios[7] = { 0, 236, 382, 500, 618, 786, 1000 };
    for (int i = 0; i < 7; i++) {
        out->fib_levels[i] = lo + (uint32_t)((uint64_t)range * fib_ratios[i] / 1000);
    }

    /* Current zone: which fib zone the latest close falls in */
    uint32_t cur = candles[count - 1].close_x100;
    out->current_zone = 6; /* default: above 1.0 */
    for (int i = 0; i < 6; i++) {
        if (cur < out->fib_levels[i + 1]) {
            out->current_zone = (uint8_t)i;
            break;
        }
    }
}

/* ══════════════════════════════════════════════════
 *  이치모쿠
 * ══════════════════════════════════════════════════ */
void btc_calc_ichimoku(IchimokuResult *out,
                       const BtcCandle *candles, uint32_t count)
{
    memset(out, 0, sizeof(*out));
    /* Need at least 52 candles for span_b */
    if (!candles || count < 52) return;

    uint32_t last = count - 1;

    /* Tenkan-sen (conversion line): (highest_high_9 + lowest_low_9) / 2 */
    uint32_t tenkan_start = count - 9;
    uint32_t hh9 = highest_high(candles, tenkan_start, 9);
    uint32_t ll9 = lowest_low(candles, tenkan_start, 9);
    out->tenkan_x100 = (hh9 + ll9) / 2;

    /* Kijun-sen (base line): (highest_high_26 + lowest_low_26) / 2 */
    uint32_t kijun_start = count - 26;
    uint32_t hh26 = highest_high(candles, kijun_start, 26);
    uint32_t ll26 = lowest_low(candles, kijun_start, 26);
    out->kijun_x100 = (hh26 + ll26) / 2;

    /* Senkou Span A: (tenkan + kijun) / 2 */
    out->span_a_x100 = (out->tenkan_x100 + out->kijun_x100) / 2;

    /* Senkou Span B: (highest_high_52 + lowest_low_52) / 2 */
    uint32_t hh52 = highest_high(candles, 0, count);
    uint32_t ll52 = lowest_low(candles, 0, count);
    /* Use last 52 candles if we have more than 52 */
    if (count >= 52) {
        uint32_t sb_start = count - 52;
        hh52 = highest_high(candles, sb_start, 52);
        ll52 = lowest_low(candles, sb_start, 52);
    }
    out->span_b_x100 = (hh52 + ll52) / 2;

    /* Chikou Span: current close */
    out->chikou_x100 = candles[last].close_x100;

    /* Cloud direction */
    out->cloud_bullish = (out->span_a_x100 > out->span_b_x100) ? 1 : 0;

    /* Price vs cloud */
    uint32_t cloud_top    = (out->span_a_x100 > out->span_b_x100)
                            ? out->span_a_x100 : out->span_b_x100;
    uint32_t cloud_bottom = (out->span_a_x100 < out->span_b_x100)
                            ? out->span_a_x100 : out->span_b_x100;
    uint32_t cur_price = candles[last].close_x100;

    if (cur_price > cloud_top)         out->price_vs_cloud = 2; /* above */
    else if (cur_price < cloud_bottom) out->price_vs_cloud = 0; /* below */
    else                               out->price_vs_cloud = 1; /* inside */

    /* TK cross: compare current and previous tenkan vs kijun */
    /* Previous tenkan/kijun using one candle earlier window */
    if (count >= 53) {
        uint32_t prev_hh9  = highest_high(candles, tenkan_start - 1, 9);
        uint32_t prev_ll9  = lowest_low(candles, tenkan_start - 1, 9);
        uint32_t prev_tenkan = (prev_hh9 + prev_ll9) / 2;

        uint32_t prev_hh26 = highest_high(candles, kijun_start - 1, 26);
        uint32_t prev_ll26 = lowest_low(candles, kijun_start - 1, 26);
        uint32_t prev_kijun = (prev_hh26 + prev_ll26) / 2;

        int cur_above  = (out->tenkan_x100 > out->kijun_x100);
        int prev_above = (prev_tenkan > prev_kijun);

        if (cur_above && !prev_above) {
            out->tk_cross = 1; /* golden cross */
        } else if (!cur_above && prev_above) {
            out->tk_cross = 2; /* dead cross */
        } else {
            out->tk_cross = 0;
        }
    }
}
