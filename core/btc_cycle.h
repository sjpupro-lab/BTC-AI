/*
 * btc_cycle.h — 소인수분해 사이클 탐지기 + 7단계 S/R
 * DK-1: float/double 0건. 정수 전용.
 * Phase 5.
 */
#pragma once
#include "btc_types.h"
#include "btc_indicators.h"  /* SR7Levels */

#define CYCLE_MAX_PERIOD 512
#define CYCLE_TOP_N      8

typedef struct {
    uint32_t period;           /* period in candles */
    uint32_t strength_x100;    /* autocorrelation strength x100 */
    uint32_t factors[8];       /* prime factors */
    uint8_t  factor_count;
} CyclePeriod;

typedef struct {
    CyclePeriod periods[CYCLE_TOP_N];
    uint8_t     count;
    uint32_t    dominant_period;
} CycleResult;

/* Autocorrelation-based cycle detection (integer) */
void btc_detect_cycles(CycleResult *out,
                       const BtcCandle *candles, uint32_t count);

/* Prime factorization (MVE port) */
uint32_t btc_factorize(uint32_t N,
                       uint32_t *factors_out, uint32_t max_factors);

/* 7-level S/R using factorization-based natural division */
void btc_calc_sr7(SR7Levels *out,
                  const BtcCandle *candles, uint32_t count,
                  uint32_t lookback);

/* S/R level strength: count bounces near level */
uint8_t btc_sr_strength(uint32_t level_x100,
                        const BtcCandle *candles, uint32_t count,
                        uint32_t tolerance_x100);
