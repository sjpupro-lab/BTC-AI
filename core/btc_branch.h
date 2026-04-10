/*
 * btc_branch.h — Branch-of-Branch 시나리오 엔진
 * 다중 미래 시나리오 분기 → 합의 추론
 * DK-1: float/double 0건. 정수 전용.
 * Phase 8.
 */
#pragma once
#include "btc_types.h"
#include "btc_multiverse.h"

/* ── 상수 ─────────────────────────────────────── */
#define BRANCH_MAX       64     /* 최대 브랜치 수 */
#define BRANCH_NONE      0xFFFFFFFFu
#define BRANCH_SCENARIOS 5      /* 기본 시나리오 수 */

/*
 * 시나리오 바이트 (v6f 기준):
 *   STRONG_UP:   170 (~+3.4%)
 *   UP:          150 (~+1.8%)
 *   NEUTRAL:     127 (0%)
 *   DOWN:        104 (~-1.8%)
 *   STRONG_DOWN: 84  (~-3.4%)
 */
#define SCENARIO_STRONG_UP   170
#define SCENARIO_UP          150
#define SCENARIO_NEUTRAL     127
#define SCENARIO_DOWN        104
#define SCENARIO_STRONG_DOWN  84

/* ── BtcBranch: 하나의 분기 시나리오 ─────────── */
typedef struct {
    uint32_t      branch_id;
    uint32_t      parent_id;      /* 부모 브랜치 (0=root) */
    uint8_t       assumed_byte;   /* 이 분기가 가정한 미래 바이트 */
    uint8_t       depth;          /* 분기 깊이 */
    MvPrediction  prediction;     /* 이 분기의 멀티버스 예측 결과 */
} BtcBranch;

/* ── BtcBranchTable: 브랜치 레지스트리 ────────── */
typedef struct {
    BtcBranch branches[BRANCH_MAX];
    uint32_t  count;
} BtcBranchTable;

/* ── BranchConsensus: 다중 분기 합의 결과 ─────── */
typedef struct {
    SignalDir consensus_dir;       /* 합의 방향 */
    uint64_t  consensus_energy;    /* 합의 에너지 합 */
    uint32_t  branch_count;        /* 유효 분기 수 */
    uint32_t  up_count;            /* 상승 예측 분기 수 */
    uint32_t  down_count;          /* 하락 예측 분기 수 */
    uint32_t  hold_count;          /* 보합 예측 분기 수 */
    uint8_t   agreement_pct;       /* 합의율 (0-100) */
    uint8_t   strongest_dir;       /* 최강 에너지 방향 */
    uint64_t  strongest_energy;    /* 최강 에너지 값 */
} BranchConsensus;

/* ── API ─────────────────────────────────────── */

/* 브랜치 테이블 초기화 */
void btc_branch_table_init(BtcBranchTable *bt);

/*
 * 분기 생성: 부모로부터 가정 바이트로 분기
 * 반환: branch_id 또는 BRANCH_NONE (가득 참)
 */
uint32_t btc_branch_create(BtcBranchTable *bt,
                           uint32_t parent_id,
                           uint8_t assumed_byte,
                           uint8_t depth);

/*
 * 시나리오 분기 계산:
 * 5가지 가격 시나리오(강한상승/상승/보합/하락/강한하락)에 대해
 * 각각 바이트 스트림을 확장하고 멀티버스 예측 실행 → 합의 도출
 */
void btc_branch_compute(BranchConsensus *out,
                        BtcCanvasBrain *brain,
                        BtcTimeframe tf,
                        const BtcCandle *candles,
                        uint32_t count);
