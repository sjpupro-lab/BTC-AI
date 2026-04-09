/*
 * btc_canvas_brain.h — Canvas AI BTC 래퍼
 * B채널 시장 레짐 분리 + 7계층 매핑
 * DK-1: float/double 0건.
 * Phase 3.
 */
#pragma once
#include "btc_types.h"
#include "btc_encoding.h"
#include "canvas_ai/cab_sieve_gear.h"

#define BTC_CANVAS_CELLS  (1024u * 1024u)  /* 1M cells */
#define BTC_MAX_WIN       128
#define BTC_STREAM_CAP    2048

typedef struct {
    CabBrain       brain;
    BtcByteStream  stream[TF_COUNT];
    MarketRegime   regime;
    uint32_t       candles_trained;
} BtcCanvasBrain;

/* Lifecycle */
int  btc_brain_init(BtcCanvasBrain *b);
void btc_brain_free(BtcCanvasBrain *b);
int  btc_brain_save(const BtcCanvasBrain *b, const char *path);
int  btc_brain_load(BtcCanvasBrain *b, const char *path);

/* Regime detection (EMA-based integer) */
MarketRegime btc_detect_regime(const BtcCandle *candles, uint32_t count);

/* Train one candle */
void btc_brain_train_candle(BtcCanvasBrain *b,
                             BtcTimeframe tf,
                             const BtcCandle *cur,
                             const BtcCandle *prev,
                             uint32_t avg_vol_x10);

/* Prediction */
typedef struct {
    SignalDir     direction;
    uint32_t      confidence;      /* 0~1000 */
    uint8_t       top3_bytes[3];
    uint32_t      top3_scores[3];
    uint32_t      layer_contrib[TF_COUNT];
} BtcPrediction;

void btc_brain_predict(BtcCanvasBrain *b,
                       BtcTimeframe tf,
                       BtcPrediction *out);
