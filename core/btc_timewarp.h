/*
 * btc_timewarp.h — BH/WH 시간 압축 + 타임워프 엔진
 * Black Hole: 패턴 압축 (IDLE/LOOP/BURST)
 * White Hole: 압축 해제 및 재생
 * TimeWarp: 시간 이동 (특정 캔들 위치로 점프)
 * DK-1: float/double 0건. 정수 전용.
 * Phase 8.
 */
#pragma once
#include "btc_types.h"
#include "btc_canvas_brain.h"

/* ── BH 압축 규칙 ────────────────────────────── */
typedef enum {
    BH_RULE_NONE  = 0,  /* 패턴 없음 */
    BH_RULE_IDLE  = 1,  /* 가격 변동 없음 (±threshold 이내) */
    BH_RULE_LOOP  = 2,  /* 패턴 반복 (K회 이상) */
    BH_RULE_BURST = 3,  /* 급변동 (짧은 구간 큰 변화) */
} BhCompressRule;

/* ── BH 상수 ─────────────────────────────────── */
#define BH_IDLE_MIN_CANDLES   16   /* IDLE 최소 캔들 수 */
#define BH_IDLE_THRESHOLD     200  /* IDLE 판정: close 변동 < 200 ($2.00) */
#define BH_LOOP_MIN_REPEAT    3    /* LOOP 최소 반복 횟수 */
#define BH_LOOP_MAX_PERIOD    32   /* LOOP 최대 주기 */
#define BH_BURST_THRESHOLD    500  /* BURST 판정: 한 캔들 변동 > 500 ($5.00) */
#define BH_BURST_WINDOW       4    /* BURST 슬라이딩 윈도우 크기 */
#define BH_BURST_MIN_COUNT    3    /* BURST 최소 횟수 */
#define BH_MAX_SUMMARIES      64   /* 최대 요약 수 */

/* ── FNV-1a 상수 (해시) ──────────────────────── */
#define BH_FNV_OFFSET  0x811C9DC5u
#define BH_FNV_PRIME   0x01000193u

/* ── BhSummary: 하나의 압축 요약 ─────────────── */
typedef struct {
    BhCompressRule rule;
    uint32_t from_idx;      /* 시작 캔들 인덱스 */
    uint32_t to_idx;        /* 종료 캔들 인덱스 */
    uint32_t count;          /* LOOP 반복 수 / BURST 이벤트 수 */
    uint32_t stride;         /* LOOP 주기 (캔들 수) */
    uint32_t pattern_hash;   /* FNV-1a 해시 (무결성 확인) */
} BhSummary;

/* ── BhStats: 압축 통계 ──────────────────────── */
typedef struct {
    uint32_t idle_count;     /* IDLE 구간 수 */
    uint32_t loop_count;     /* LOOP 구간 수 */
    uint32_t burst_count;    /* BURST 구간 수 */
    uint32_t candles_saved;  /* 압축으로 절약된 캔들 수 */
    uint32_t total_candles;  /* 분석된 총 캔들 수 */
} BhStats;

/* ── BtcBhEngine: BH 압축 엔진 ──────────────── */
typedef struct {
    BhSummary summaries[BH_MAX_SUMMARIES];
    uint32_t  summary_count;
    BhStats   stats;
} BtcBhEngine;

/* ── BtcTimeWarp: 타임워프 상태 ──────────────── */
typedef struct {
    uint32_t saved_idx;     /* 워프 전 캔들 인덱스 */
    uint32_t target_idx;    /* 워프 후 캔들 인덱스 */
    uint8_t  active;        /* 워프 모드 활성 여부 */
} BtcTimeWarp;

/* ── BH 분석 API ─────────────────────────────── */

/* 단일 구간 분석: [from, to] 범위에서 패턴 탐지 */
int btc_bh_analyze(BhSummary *out,
                   const BtcCandle *candles,
                   uint32_t from, uint32_t to);

/* 전체 범위 압축 분석: 모든 패턴 탐지 + 통계 */
int btc_bh_compress_range(BtcBhEngine *engine,
                          const BtcCandle *candles,
                          uint32_t count);

/* BH 요약 재생: 압축된 패턴을 Canvas AI에 재학습 */
int btc_bh_replay(const BhSummary *summary,
                  const BtcCandle *candles,
                  BtcCanvasBrain *brain,
                  BtcTimeframe tf);

/* ── TimeWarp API ────────────────────────────── */

void btc_timewarp_init(BtcTimeWarp *tw);

/* 특정 캔들 인덱스로 점프 (현재 위치 저장) */
int btc_timewarp_goto(BtcTimeWarp *tw,
                      uint32_t target_idx,
                      uint32_t current_idx);

/* 원래 위치로 복귀 */
int btc_timewarp_resume(BtcTimeWarp *tw, uint32_t *restored_idx);

/* 두 시점 사이의 변화량 (비제로 가격 변동 캔들 수) */
uint32_t btc_timewarp_diff(const BtcCandle *candles,
                           uint32_t from_idx, uint32_t to_idx);
