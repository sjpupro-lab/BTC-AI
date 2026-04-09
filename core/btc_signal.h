/*
 * btc_signal.h — 신호 융합 엔진
 * DK-1: float/double 0건.
 * Phase 7.
 */
#pragma once
#include "btc_types.h"
#include "btc_canvas_brain.h"
#include "btc_indicators.h"
#include "btc_pattern.h"

typedef struct {
    uint8_t  canvas_score;   /* max 40 */
    uint8_t  bb_score;       /* max 15 */
    uint8_t  sr7_score;      /* max 15 */
    uint8_t  rsi_score;      /* max 10, premium */
    uint8_t  macd_score;     /* max 10, premium */
    uint8_t  pattern_score;  /* max 10, premium */

    uint8_t      total_score;  /* 0~100 */
    SignalDir    direction;
    SignalStrength strength;

    const char  *reason_canvas;
    const char  *reason_bb;
    const char  *reason_sr7;
} BtcSignal;

typedef struct {
    int premium_enabled;
} BtcSignalConfig;

void btc_signal_compute(BtcSignal *out,
                        const BtcPrediction   *canvas,
                        const BollingerBands  *bb,
                        const SR7Levels       *sr7,
                        const RsiResult       *rsi,       /* NULL if free */
                        const MacdResult      *macd,      /* NULL if free */
                        const PatternSimilarityResult *pat, /* NULL if free */
                        const BtcCandle       *current,
                        const BtcSignalConfig *cfg);
