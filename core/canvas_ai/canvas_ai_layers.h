/*
 * canvas_ai_layers.h — 7계층 AI 레이어 정의
 * ==========================================
 * 언어학적 계층 구조 기반 7계층.
 *
 * 가중치 우선순위: Word > Morpheme > Phrase > Clause > Sentence > Byte(베이스)
 * Topic은 가장 거친(coarse) 계층으로 uint8_t 범위.
 *
 * 계층별 가중치 타입:
 *   Byte / Morpheme / Word  → uint32_t (32비트 정밀도)
 *   Phrase / Clause / Sentence → uint16_t (16비트 정밀도)
 *   Topic → uint8_t (8비트 정밀도)
 *
 * 계층 구조:
 *   Layer 0: Byte      (depth 1)        — 바이트 (단일 문자)        [uint32_t, 베이스]
 *   Layer 1: Morpheme  (depth 2-8)      — 형태소 (접두/접미사, 어근) [uint32_t]
 *   Layer 2: Word      (depth 9-24)     — 단어                     [uint32_t, 최강]
 *   Layer 3: Phrase    (depth 25-48)    — 구 (명사구, 동사구)       [uint16_t]
 *   Layer 4: Clause    (depth 49-80)    — 절                       [uint16_t]
 *   Layer 5: Sentence  (depth 81-112)   — 문장                     [uint16_t]
 *   Layer 6: Topic     (depth 113-128)  — 토픽 (주제/담화)          [uint8_t]
 */

#ifndef CANVAS_AI_LAYERS_H
#define CANVAS_AI_LAYERS_H

#include <stdint.h>

/* ── 최대 depth ───────────────────────────────── */
#define CAB_MAX_DEPTH  128

/* ── 7계층 인덱스 ──────────────────────────────── */
#define CAB_LAYER_BYTE      0   /* 바이트   */
#define CAB_LAYER_MORPHEME  1   /* 형태소   */
#define CAB_LAYER_WORD      2   /* 단어     */
#define CAB_LAYER_PHRASE    3   /* 구       */
#define CAB_LAYER_CLAUSE    4   /* 절       */
#define CAB_LAYER_SENTENCE  5   /* 문장     */
#define CAB_LAYER_TOPIC     6   /* 토픽     */
#define CAB_NUM_LAYERS      7

/* ── 계층별 가중치 타입 비트 ──────────────────── */
#define CAB_WTYPE_U32   32   /* Byte, Morpheme, Word   */
#define CAB_WTYPE_U16   16   /* Phrase, Clause, Sentence */
#define CAB_WTYPE_U8     8   /* Topic                   */

/* ── 계층 이름 (디버그용) ──────────────────────── */
static const char *const CAB_LAYER_NAMES[CAB_NUM_LAYERS] = {
    "Byte", "Morpheme", "Word", "Phrase", "Clause", "Sentence", "Topic"
};

/* ── 계층 범위 정의 ────────────────────────────── */
typedef struct {
    uint8_t  min_depth;
    uint8_t  max_depth;
    uint32_t base_weight;   /* 기본 가중치 (계층별 타입에 따라 범위 제한) */
    uint8_t  weight_bits;   /* 가중치 타입: 32, 16, 또는 8 */
} CabLayerRange;

/*
 * 가중치 설계 원칙:
 *   Word(50000) > Morpheme(30000) > Phrase(8000) > Clause(2000)
 *   > Sentence(500) > Topic(64) > Byte(1, 베이스)
 *
 * G(uint16_t, max 65535) × weight(max 50000) = max 3,276,750,000
 *   → uint32_t(max 4,294,967,295) 범위 안에서 안전.
 */
static const CabLayerRange CAB_LAYER_RANGES[CAB_NUM_LAYERS] = {
    {  1,   1,     1, CAB_WTYPE_U32 },  /* Byte      — 베이스        */
    {  2,   8, 30000, CAB_WTYPE_U32 },  /* Morpheme  — 2위           */
    {  9,  24, 50000, CAB_WTYPE_U32 },  /* Word      — 최강          */
    { 25,  48,  8000, CAB_WTYPE_U16 },  /* Phrase    — 3위           */
    { 49,  80,  2000, CAB_WTYPE_U16 },  /* Clause    — 4위           */
    { 81, 112,   500, CAB_WTYPE_U16 },  /* Sentence  — 5위           */
    {113, 128,    64, CAB_WTYPE_U8  },  /* Topic     — 가장 거침     */
};

/* ── depth → layer 변환 ────────────────────────── */
static inline uint8_t depth_to_layer(uint8_t depth) {
    if (depth <= 1)   return CAB_LAYER_BYTE;
    if (depth <= 8)   return CAB_LAYER_MORPHEME;
    if (depth <= 24)  return CAB_LAYER_WORD;
    if (depth <= 48)  return CAB_LAYER_PHRASE;
    if (depth <= 80)  return CAB_LAYER_CLAUSE;
    if (depth <= 112) return CAB_LAYER_SENTENCE;
    return CAB_LAYER_TOPIC;
}

/* ── depth → 가중치 (uint32_t 반환) ───────────── */
static inline uint32_t layer_weight(uint8_t depth) {
    /*
     * 가중치 우선순위: Word > Morpheme > Phrase > Clause > Sentence > Byte
     * 같은 계층 내에서는 depth가 높을수록 약간 더 높은 가중치 (+50% max).
     *
     * 계층별 타입 제한:
     *   uint32_t 계층 (Byte/Morpheme/Word): 값 ≤ 4,294,967,295
     *   uint16_t 계층 (Phrase/Clause/Sentence): 값 ≤ 65,535
     *   uint8_t  계층 (Topic): 값 ≤ 255
     */
    if (depth == 0) return 0;
    if (depth > CAB_MAX_DEPTH) depth = CAB_MAX_DEPTH;

    uint8_t layer = depth_to_layer(depth);
    const CabLayerRange *r = &CAB_LAYER_RANGES[layer];
    uint32_t base = r->base_weight;
    uint8_t span = r->max_depth - r->min_depth;

    if (span == 0) return base;

    /* 계층 내 점진적 증가: min_depth에서 base, max_depth에서 base * 1.5 */
    uint8_t pos = depth - r->min_depth;
    return base + (base * pos) / (span * 2);
}

/* ── layer → 대표 depth ────────────────────────── */
static inline uint8_t layer_to_depth(uint8_t layer) {
    if (layer >= CAB_NUM_LAYERS) return 1;
    return CAB_LAYER_RANGES[layer].min_depth;
}

/* ── layer → 가중치 타입 비트 ──────────────────── */
static inline uint8_t layer_weight_bits(uint8_t layer) {
    if (layer >= CAB_NUM_LAYERS) return CAB_WTYPE_U32;
    return CAB_LAYER_RANGES[layer].weight_bits;
}

#endif /* CANVAS_AI_LAYERS_H */
