/*
 * btc_canvas_brain.c — Canvas AI BTC Core 구현
 * DK-1: float/double 0건. 정수 전용.
 * Phase 3.
 */
#include "btc_canvas_brain.h"
#include <string.h>

/* ══════════════════════════════════════════════════
 *  Lifecycle
 * ══════════════════════════════════════════════════ */

int btc_brain_init(BtcCanvasBrain *b) {
    if (!b) return BTC_ERR_PARAM;
    memset(b, 0, sizeof(*b));

    int rc = cab_brain_init(&b->brain, BTC_CANVAS_CELLS, BTC_MAX_WIN);
    if (rc != 0) return BTC_ERR_ALLOC;

    for (int i = 0; i < TF_COUNT; i++) {
        if (btc_stream_init(&b->stream[i], BTC_STREAM_CAP) != 0) {
            /* Roll back: free already-initialised streams */
            for (int j = 0; j < i; j++)
                btc_stream_free(&b->stream[j]);
            cab_brain_free(&b->brain);
            return BTC_ERR_ALLOC;
        }
    }

    b->regime          = REGIME_UNKNOWN;
    b->candles_trained = 0;
    return BTC_OK;
}

void btc_brain_free(BtcCanvasBrain *b) {
    if (!b) return;
    cab_brain_free(&b->brain);
    for (int i = 0; i < TF_COUNT; i++)
        btc_stream_free(&b->stream[i]);
}

/* ══════════════════════════════════════════════════
 *  Save / Load
 * ══════════════════════════════════════════════════ */

int btc_brain_save(const BtcCanvasBrain *b, const char *path) {
    if (!b || !path) return BTC_ERR_PARAM;
    return cab_save(&b->brain.canvas, path);
}

int btc_brain_load(BtcCanvasBrain *b, const char *path) {
    if (!b || !path) return BTC_ERR_PARAM;
    return cab_load(&b->brain.canvas, path);
}

/* ══════════════════════════════════════════════════
 *  Regime Detection (integer EMA)
 * ══════════════════════════════════════════════════ */

/*
 * EMA alpha ×10000:
 *   EMA20: alpha = 2*10000/21 = 952
 *   EMA50: alpha = 2*10000/51 = 392
 *
 * ema = (close * alpha + prev_ema * (10000 - alpha)) / 10000
 *
 * Regime:
 *   ema20 > ema50 * 1005 / 1000 → BULL
 *   ema20 < ema50 *  995 / 1000 → BEAR
 *   else                        → SIDEWAYS
 */
MarketRegime btc_detect_regime(const BtcCandle *candles, uint32_t count) {
    if (!candles || count < 50) return REGIME_UNKNOWN;

    #define EMA20_ALPHA  952u     /* 2*10000/21 */
    #define EMA50_ALPHA  392u     /* 2*10000/51 */
    #define EMA_SCALE    10000u

    uint64_t ema20 = (uint64_t)candles[0].close_x100;
    uint64_t ema50 = (uint64_t)candles[0].close_x100;

    for (uint32_t i = 1; i < count; i++) {
        uint64_t close = (uint64_t)candles[i].close_x100;

        ema20 = (close * EMA20_ALPHA
                 + ema20 * (EMA_SCALE - EMA20_ALPHA)) / EMA_SCALE;

        ema50 = (close * EMA50_ALPHA
                 + ema50 * (EMA_SCALE - EMA50_ALPHA)) / EMA_SCALE;
    }

    /* Compare with 0.5% threshold */
    uint64_t bull_thresh = ema50 * 1005 / 1000;
    uint64_t bear_thresh = ema50 *  995 / 1000;

    if (ema20 > bull_thresh) return REGIME_BULL;
    if (ema20 < bear_thresh) return REGIME_BEAR;
    return REGIME_SIDEWAYS;

    #undef EMA20_ALPHA
    #undef EMA50_ALPHA
    #undef EMA_SCALE
}

/* ══════════════════════════════════════════════════
 *  Train One Candle
 * ══════════════════════════════════════════════════ */

void btc_brain_train_candle(BtcCanvasBrain *b,
                             BtcTimeframe tf,
                             const BtcCandle *cur,
                             const BtcCandle *prev,
                             uint32_t avg_vol_x10) {
    if (!b || !cur || !prev) return;
    if ((int)tf < 0 || (int)tf >= TF_COUNT) return;

    /* Encode candle to 4-byte packet */
    BtcCandleBytes cb;
    btc_candle_encode(&cb, cur, prev, avg_vol_x10);

    /* Append to timeframe stream */
    btc_stream_append(&b->stream[tf], &cb);

    /* Train the brain if stream has enough data (>= 8 bytes) */
    BtcByteStream *s = &b->stream[tf];
    if (s->len >= 8) {
        cab_train_fast(&b->brain, s->data, s->len);
    }

    b->candles_trained++;
}

/* ══════════════════════════════════════════════════
 *  Prediction
 * ══════════════════════════════════════════════════ */

void btc_brain_predict(BtcCanvasBrain *b,
                       BtcTimeframe tf,
                       BtcPrediction *out) {
    if (!b || !out) return;
    if ((int)tf < 0 || (int)tf >= TF_COUNT) return;

    memset(out, 0, sizeof(*out));

    BtcByteStream *s = &b->stream[tf];
    if (s->len < 4) {
        out->direction  = SIGNAL_NONE;
        out->confidence = 0;
        return;
    }

    /* Push recent stream bytes into brain window */
    cab_brain_reset(&b->brain);
    uint32_t start = (s->len > BTC_MAX_WIN) ? s->len - BTC_MAX_WIN : 0;
    for (uint32_t i = start; i < s->len; i++) {
        cab_brain_push(&b->brain, s->data[i]);
    }

    /* Predict */
    uint8_t  top3[3]   = {0, 0, 0};
    uint32_t scores[3] = {0, 0, 0};
    cab_predict_fast(&b->brain, top3, scores);

    out->top3_bytes[0]  = top3[0];
    out->top3_bytes[1]  = top3[1];
    out->top3_bytes[2]  = top3[2];
    out->top3_scores[0] = scores[0];
    out->top3_scores[1] = scores[1];
    out->top3_scores[2] = scores[2];

    /* Direction from top prediction byte:
     *   price byte > 140 → bullish (SIGNAL_LONG)
     *   price byte < 114 → bearish (SIGNAL_SHORT)
     *   else              → SIGNAL_HOLD
     *
     * 127 = no change in v6f encoding.
     * 140 ≈ +1% move, 114 ≈ -1% move.
     */
    uint8_t top_byte = top3[0];
    if (top_byte > 140) {
        out->direction = SIGNAL_LONG;
    } else if (top_byte < 114) {
        out->direction = SIGNAL_SHORT;
    } else {
        out->direction = SIGNAL_HOLD;
    }

    /* Confidence: top_score * 1000 / (sum + 1), clamped 0~1000 */
    uint32_t sum = scores[0] + scores[1] + scores[2] + 1;
    uint32_t conf = scores[0] * 1000 / sum;
    if (conf > 1000) conf = 1000;
    out->confidence = conf;

    /* Layer contributions: current tf gets confidence, others zero */
    for (int i = 0; i < TF_COUNT; i++) {
        out->layer_contrib[i] = 0;
    }
    out->layer_contrib[tf] = out->confidence;
}
