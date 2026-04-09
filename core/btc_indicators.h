/*
 * btc_indicators.h — 기술 지표 엔진
 * DK-1: float/double 0건. 정수 전용.
 * Phase 4.
 */
#pragma once
#include "btc_types.h"

/* ── 볼린저 밴드 (무료) ── */
typedef struct {
    uint32_t middle_x100;    /* SMA(20) */
    uint32_t upper_x100;     /* +2 sigma */
    uint32_t lower_x100;     /* -2 sigma */
    uint8_t  bandwidth_x100; /* (upper-lower)/middle * 100 */
    uint8_t  position;       /* 0=lower breach, 127=middle, 254=upper breach */
} BollingerBands;

void btc_calc_bb(BollingerBands *out,
                 const BtcCandle *candles, uint32_t count,
                 uint32_t period);

/* ── 7단계 S/R (무료, Phase 5에서 구현) ── */
typedef struct {
    uint32_t levels[7];    /* price x100, ascending */
    uint8_t  strength[7];  /* 0~255 each */
} SR7Levels;

/* ── RSI (프리미엄) ── */
typedef struct {
    uint8_t value;       /* 0~100 */
    uint8_t prev_value;
    uint8_t state;       /* 0=normal, 1=overbought(>70), 2=oversold(<30), 3=divergence */
} RsiResult;

void btc_calc_rsi(RsiResult *out,
                  const BtcCandle *candles, uint32_t count,
                  uint32_t period);

/* ── MACD (프리미엄) ── */
typedef struct {
    int32_t macd_x1000;       /* MACD line x1000 */
    int32_t signal_x1000;     /* Signal line x1000 */
    int32_t histogram_x1000;  /* MACD - Signal */
    uint8_t cross;            /* 0=none, 1=golden, 2=dead */
} MacdResult;

void btc_calc_macd(MacdResult *out,
                   const BtcCandle *candles, uint32_t count,
                   uint32_t fast, uint32_t slow, uint32_t signal_p);

/* ── Volume Profile (프리미엄) ── */
#define VP_BINS 24
typedef struct {
    uint32_t price_levels[VP_BINS];
    uint32_t volume[VP_BINS];
    uint32_t poc_x100;   /* Point of Control */
    uint32_t val_x100;   /* Value Area Low */
    uint32_t vah_x100;   /* Value Area High */
} VolumeProfile;

void btc_calc_vp(VolumeProfile *out,
                 const BtcCandle *candles, uint32_t count);

/* ── 피보나치 (프리미엄) ── */
typedef struct {
    uint32_t swing_high_x100;
    uint32_t swing_low_x100;
    uint32_t fib_levels[7]; /* 0.0, 0.236, 0.382, 0.5, 0.618, 0.786, 1.0 */
    uint8_t  current_zone;
} FibResult;

void btc_calc_fib(FibResult *out,
                  const BtcCandle *candles, uint32_t count,
                  uint32_t lookback);

/* ── 이치모쿠 (프리미엄) ── */
typedef struct {
    uint32_t tenkan_x100;
    uint32_t kijun_x100;
    uint32_t span_a_x100;
    uint32_t span_b_x100;
    uint32_t chikou_x100;
    uint8_t  cloud_bullish;  /* 1=bullish, 0=bearish */
    uint8_t  price_vs_cloud; /* 0=below, 1=inside, 2=above */
    uint8_t  tk_cross;       /* 0=none, 1=golden, 2=dead */
} IchimokuResult;

void btc_calc_ichimoku(IchimokuResult *out,
                       const BtcCandle *candles, uint32_t count);
