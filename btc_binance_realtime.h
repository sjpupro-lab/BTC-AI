/*
 * btc_binance_realtime.h — Binance 실시간 멀티 타임프레임 버퍼
 * =============================================================
 * Binance REST API (BTCUSDT klines) → 4개 TF BtcCandle 배열 관리
 * TF: 30m / 1h / 1d / 1w
 *
 * DK-1: float/double 0건. 정수 전용.
 * 가격은 ×100 정수, 볼륨은 ×10 정수로 저장.
 */
#pragma once
#include "core/btc_types.h"

/* ── 상수 ──────────────────────────────────────── */
#define REALTIME_MAX_CANDLES   200
#define REALTIME_TF_COUNT        4

/* RT 버퍼 인덱스 */
#define RT_IDX_30M   0   /* 30분봉  → BtcTimeframe TF_15M 매핑 (30m 없음) */
#define RT_IDX_1H    1   /* 1시간봉 → TF_1H */
#define RT_IDX_1D    2   /* 일봉    → TF_1D */
#define RT_IDX_1W    3   /* 주봉    → TF_1W */

/* ── 타임프레임 버퍼 ────────────────────────────── */
typedef struct {
    BtcCandle    candles[REALTIME_MAX_CANDLES];
    uint32_t     count;
    BtcTimeframe tf;
    char         binance_interval[8];  /* "30m", "1h", "1d", "1w" */
} RealtimeTfBuffer;

/* ── API ────────────────────────────────────────── */

/* 4개 TF 버퍼 초기화 (libcurl 전역 초기화 포함) */
void btc_realtime_init(RealtimeTfBuffer buffers[REALTIME_TF_COUNT]);

/* Binance REST API에서 모든 TF 캔들 갱신 */
void btc_realtime_update_all(RealtimeTfBuffer buffers[REALTIME_TF_COUNT]);

/* 리소스 해제 (libcurl 전역 정리) */
void btc_realtime_free(RealtimeTfBuffer buffers[REALTIME_TF_COUNT]);
