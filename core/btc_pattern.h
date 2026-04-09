/*
 * btc_pattern.h — Canvas AI 패턴 유사도 엔진
 * Gear Hash 기반 캔들 패턴 지문 생성 + 유사도 검색
 * DK-1: float/double 0건.
 * Phase 6.
 */
#pragma once
#include "btc_types.h"
#include "btc_canvas_brain.h"

#define PAT_WINDOW 10
#define PAT_TOP_K  5

typedef struct {
    uint32_t gear;              /* pattern gear hash */
    uint32_t similarity_x100;   /* 0~10000 */
    uint32_t up_score;          /* upward continuation score */
    uint32_t down_score;        /* downward continuation score */
    uint8_t  historical_up_pct; /* historical up percentage */
} PatternMatch;

typedef struct {
    PatternMatch matches[PAT_TOP_K];
    uint8_t      count;
    uint8_t      consensus_direction; /* 1=up, 2=down, 0=unknown */
    uint8_t      consensus_strength;  /* 0~100 */
} PatternSimilarityResult;

/* Search for similar patterns in Canvas AI learned data */
void btc_pattern_search(PatternSimilarityResult *out,
                        BtcCanvasBrain *brain,
                        BtcTimeframe tf,
                        const BtcCandle *recent,
                        uint32_t count);

/* Jaccard similarity between two activation vectors (integer) */
uint32_t btc_pattern_jaccard_x100(const uint32_t *act_a,
                                   const uint32_t *act_b,
                                   uint32_t len);
