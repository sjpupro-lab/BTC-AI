/*
 * canvas_ai_b.h — Canvas AI Brain (CAB) 핵심 헤더
 * =================================================
 * 해시 테이블 기반 예측 + 밝기 전파/패턴 인식 엔진.
 *
 * CabCell: AI 전용 셀 (CanvasCell과 동일 레이아웃)
 *   A[31:24] = 의미 클러스터
 *   A[23:0]  = gear 해시 하위 24비트
 *   G        = 빈도 (밝기)
 *   R        = 바이트 값 (0~255)
 *   B        = depth (1~128)
 */

#ifndef CANVAS_AI_B_H
#define CANVAS_AI_B_H

#include <stdint.h>
#include <stddef.h>
#include "canvasos_types.h"
#include "canvas_ai_layers.h"
#include "canvas_determinism.h"

/* ── AI 셀 타입 (CanvasCell과 바이너리 호환) ───── */
typedef CanvasCell CabCell;

/* ── 학습 최대 깊이 (Byte+Morpheme+Word+partial Phrase) ── */
#define CAB_TRAIN_MAX_DEPTH  32

/* ── G 클램프: uint16_t 범위 포화 (legacy, 내부용) ── */
#define CAB_G_CLAMP(val) \
    ((uint16_t)(((val) > 0xFFFFu) ? 0xFFFFu : (val)))

/* ── G 감쇠 누산 (diminishing returns) ────────── */
/*
 * 기존: G += 1 (flat increment, hard cap)
 * 신규: G += delta * 1024 / (1024 + G)
 *   G=0    → full delta
 *   G=1024 → delta/2
 *   G=2048 → delta/3
 *   자연적으로 성장 속도 감소, hard cap 불필요
 */
static inline uint16_t cab_g_increment(uint16_t current_g, uint16_t delta) {
    uint32_t threshold = 1024u;
    uint32_t effective = (uint32_t)delta * threshold
                       / (threshold + (uint32_t)current_g);
    if (effective == 0 && delta > 0) effective = 1;  /* 최소 1 증가 */
    uint32_t result = (uint32_t)current_g + effective;
    return (result > 0xFFFFu) ? 0xFFFFu : (uint16_t)result;
}

/* ── 학습 델타: 계층별 가중치 적용 ────────────── */
/*
 * Word(d=9-24)     → delta 4 (최강 학습)
 * Morpheme(d=2-8)  → delta 3
 * Phrase(d=25-48)  → delta 2
 * Byte/Clause/etc  → delta 1 (베이스)
 */
static inline uint16_t cab_train_delta(uint8_t depth) {
    uint8_t layer = depth_to_layer(depth);
    switch (layer) {
        case CAB_LAYER_WORD:      return 4;
        case CAB_LAYER_MORPHEME:  return 3;
        case CAB_LAYER_PHRASE:    return 2;
        default:                  return 1;
    }
}

/* ── 해시 probe 최대 스텝 ──────────────────────── */
#define CAB_PROBE_LIMIT  64

/* ── 윈도우 최대 크기 ──────────────────────────── */
#define CAB_WINDOW_MAX   256

/* ── 캔버스 기본 용량 (2의 거듭제곱) ───────────── */
#define CAB_DEFAULT_CAPACITY  (4096u * 4096u)  /* 16M 셀 */

/* ── 동적 리사이즈 임계값 (Q2: 3/4 = 75%) ─────── */
#define CAB_GROW_THRESHOLD_NUM  3
#define CAB_GROW_THRESHOLD_DEN  4

/* ── CabCanvas: AI 캔버스 ──────────────────────── */
typedef struct {
    CabCell  *cells;           /* 셀 배열 (해시 테이블) */
    uint32_t  capacity;        /* 총 셀 수 (2의 거듭제곱) */
    uint16_t  width;           /* 논리 너비 */
    uint16_t  height;          /* 논리 높이 */
    uint8_t   window[CAB_WINDOW_MAX]; /* 슬라이딩 윈도우 */
    uint8_t   win_len;         /* 현재 윈도우 길이 */
    uint8_t   max_win;         /* 최대 윈도우 크기 */
    uint32_t  cell_count;      /* 사용 중인 셀 수 */
} CabCanvas;

/* ══════════════════════════════════════════════════
 *  해시 함수
 * ══════════════════════════════════════════════════ */

/* FNV-1a 해시 (bt_canvas.c에서 가져옴) */
static inline uint32_t cab_fnv1a(const uint8_t *data, uint8_t len) {
    uint32_t h = 0x811c9dc5u;
    for (uint8_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x01000193u;
    }
    return h;
}

/* gear 해시: 문맥(ctx)으로부터 gear 값 생성 */
static inline uint32_t cab_gear(const uint8_t *ctx, uint8_t len) {
    return cab_fnv1a(ctx, len);
}

/* 해시 믹스: gear + depth + byte → probe 시작점
 * gear의 하위 24비트만 사용 (A[23:0]과 일치).
 * → 동적 리사이즈 시 저장된 A에서 해시 재구성 가능.
 */
static inline uint32_t cab_hash_mix(uint32_t gear, uint8_t depth, uint8_t byte_val) {
    uint32_t h = gear & 0x00FFFFFFu;   /* A[23:0] 일치 */
    h ^= (uint32_t)depth << 16;
    h ^= (uint32_t)byte_val;
    h *= 0x01000193u;
    return h;
}

/* ══════════════════════════════════════════════════
 *  A 채널 인코딩 (의미 클러스터링)
 * ══════════════════════════════════════════════════ */

/*
 * A[31:24] = 의미 클러스터 (문맥 바이트 기반)
 * A[23:0]  = gear 해시 하위 24비트 (정확 매칭용)
 *
 * 클러스터 계산: 문맥의 마지막 2바이트 고니블(high nibble)
 *   cluster = (ctx[d-1] & 0xF0) | (ctx[d-2] >> 4)
 *
 * 핵심 성질:
 *   같은 예측 위치에서 depth 1이든 24이든,
 *   ctx[d-1] = window[win_len-1] (항상 동일)
 *   → 같은 위치의 모든 셀이 같은 클러스터에 뭉침
 *   → 전파 시 의미적으로 관련된 셀끼리만 에너지 교환
 *
 * 예시:
 *   "...th" 뒤 예측 → cluster = ('h'&0xF0)|('t'>>4) = 0x67
 *   "...in" 뒤 예측 → cluster = ('n'&0xF0)|('i'>>4) = 0x66
 *   서로 다른 문맥 = 서로 다른 클러스터 = 의미적 분리
 */
static inline uint32_t cab_encode_A(uint32_t gear,
                                     const uint8_t *ctx, uint8_t ctx_len) {
    uint8_t cluster;
    if (ctx_len >= 2) {
        cluster = (ctx[ctx_len - 1] & 0xF0) | (ctx[ctx_len - 2] >> 4);
    } else if (ctx_len >= 1) {
        cluster = ctx[0];
    } else {
        cluster = 0;
    }
    return ((uint32_t)cluster << 24) | (gear & 0x00FFFFFFu);
}

/* A 채널에서 클러스터 추출 */
static inline uint8_t cab_get_cluster(uint32_t A) {
    return (uint8_t)(A >> 24);
}

/* ══════════════════════════════════════════════════
 *  Probe: 해시 테이블 읽기/쓰기
 * ══════════════════════════════════════════════════ */

/* probe 읽기: 일치하는 셀의 인덱스 반환, 없으면 UINT32_MAX */
static inline uint32_t cab_probe_read(const CabCanvas *c,
                                       uint32_t gear, uint8_t depth,
                                       uint8_t byte_val) {
    uint32_t hash = cab_hash_mix(gear, depth, byte_val);
    uint32_t mask = c->capacity - 1;

    for (uint32_t step = 0; step < CAB_PROBE_LIMIT; step++) {
        uint32_t idx = (hash + step) & mask;
        const CabCell *cell = &c->cells[idx];

        /* 빈 셀 = 없음 */
        if (cell->G == 0 && cell->A == 0) return UINT32_MAX;

        /* 일치 확인: R(바이트), B(깊이), A 하위 24비트(gear) */
        if (cell->R == byte_val &&
            cell->B == depth &&
            (cell->A & 0x00FFFFFFu) == (gear & 0x00FFFFFFu)) {
            return idx;
        }
    }
    return UINT32_MAX;
}

/* probe 쓰기: 일치하는 셀의 인덱스 반환, 없으면 빈 슬롯 할당 */
static inline uint32_t cab_probe_write(CabCanvas *c,
                                        uint32_t gear, uint8_t depth,
                                        uint8_t byte_val) {
    uint32_t hash = cab_hash_mix(gear, depth, byte_val);
    uint32_t mask = c->capacity - 1;

    uint32_t first_empty = UINT32_MAX;

    for (uint32_t step = 0; step < CAB_PROBE_LIMIT; step++) {
        uint32_t idx = (hash + step) & mask;
        CabCell *cell = &c->cells[idx];

        /* 빈 셀 발견 */
        if (cell->G == 0 && cell->A == 0) {
            if (first_empty == UINT32_MAX) first_empty = idx;
            return first_empty;  /* 첫 번째 빈 슬롯 반환 */
        }

        /* 이미 존재하는 셀 */
        if (cell->R == byte_val &&
            cell->B == depth &&
            (cell->A & 0x00FFFFFFu) == (gear & 0x00FFFFFFu)) {
            return idx;
        }

        if (first_empty == UINT32_MAX && cell->G == 0) {
            first_empty = idx;
        }
    }

    /* probe 한계 도달: 첫 빈 슬롯이 있으면 그곳에, 없으면 마지막 위치 */
    if (first_empty != UINT32_MAX) return first_empty;
    return (hash + CAB_PROBE_LIMIT - 1) & mask;
}

/* ══════════════════════════════════════════════════
 *  윈도우 관리
 * ══════════════════════════════════════════════════ */

/* 윈도우에 바이트 추가 */
static inline void cab_push_window(CabCanvas *c, uint8_t byte_val) {
    if (c->win_len < c->max_win) {
        c->window[c->win_len++] = byte_val;
    } else {
        /* 윈도우가 가득 차면 왼쪽 시프트 */
        for (uint8_t i = 1; i < c->max_win; i++) {
            c->window[i - 1] = c->window[i];
        }
        c->window[c->max_win - 1] = byte_val;
    }
}

/* ══════════════════════════════════════════════════
 *  밝기 계산 (bt_pattern.c에서 가져옴)
 * ══════════════════════════════════════════════════ */

/* 채널 조합형 밝기: R, G, B 등급 인코딩 */
typedef struct {
    uint8_t r_grade;  /* R 등급 (0~7) */
    uint8_t g_grade;  /* G 등급 (0~7) */
    uint8_t b_grade;  /* B 등급 (0~7) */
} BtPad;

/* bt_encode_pad: 셀 값을 3비트 등급으로 인코딩 */
static inline BtPad bt_encode_pad(const CabCell *cell) {
    BtPad pad;
    pad.r_grade = cell->R >> 5;          /* 0~7 */
    pad.g_grade = (uint8_t)(cell->G >> 13); /* 0~7 */
    pad.b_grade = cell->B >> 5;          /* 0~7 */
    return pad;
}

/* bt_decode_pad: 등급을 원래 범위로 디코딩 */
static inline void bt_decode_pad(const BtPad *pad, uint8_t *R, uint16_t *G, uint8_t *B) {
    *R = (pad->r_grade << 5) | 0x10;
    *G = ((uint16_t)pad->g_grade << 13) | 0x1000;
    *B = (pad->b_grade << 5) | 0x10;
}

/* bt_brightness: 채널 조합형 밝기 계산 */
static inline uint32_t bt_brightness(const CabCell *cell) {
    /* 밝기 = G * layer_weight(B) + R 보너스 */
    uint32_t base = (uint32_t)cell->G * layer_weight(cell->B);
    uint32_t r_bonus = (uint32_t)cell->R;  /* 바이트 값 자체는 약한 보너스 */
    return base + r_bonus;
}

/* ══════════════════════════════════════════════════
 *  빈도 ↔ 밝기 로그 스케일 (bt_canvas.c에서 가져옴)
 * ══════════════════════════════════════════════════ */

/* freq_to_g: 빈도 → G값 (로그 스케일, 정수 근사) */
static inline uint16_t freq_to_g(uint32_t freq) {
    if (freq == 0) return 0;
    /* log2 근사: 최상위 비트 위치 */
    uint32_t log2 = 0;
    uint32_t tmp = freq;
    while (tmp > 1) { tmp >>= 1; log2++; }
    /* G = log2 * 4096, 최대 65535 */
    uint32_t g = log2 * 4096;
    return (g > 0xFFFFu) ? 0xFFFFu : (uint16_t)g;
}

/* g_to_freq: G값 → 빈도 (역변환) */
static inline uint32_t g_to_freq(uint16_t g) {
    if (g == 0) return 0;
    uint32_t log2 = g / 4096;
    return 1u << log2;
}

/* ══════════════════════════════════════════════════
 *  캔버스 생성/소멸
 * ══════════════════════════════════════════════════ */

int  cab_canvas_init(CabCanvas *c, uint32_t capacity, uint8_t max_win);
void cab_canvas_free(CabCanvas *c);
void cab_canvas_reset(CabCanvas *c);

/* 동적 리사이즈: 용량 2배 확장 (해시 재배치 포함) */
int  cab_canvas_grow(CabCanvas *c);

/* ══════════════════════════════════════════════════
 *  학습 / 예측 API
 * ══════════════════════════════════════════════════ */

/* 학습: 데이터를 캔버스에 누산 */
void cab_train(CabCanvas *c, const uint8_t *data, uint32_t len);

/* 예측 (v2: 전파 + 패턴 인식) */
uint8_t cab_predict(CabCanvas *c, uint8_t *top3, uint32_t *scores3);

/* 예측 (legacy: 해시 lookup만) */
uint8_t cab_predict_legacy(CabCanvas *c, uint8_t *top3, uint32_t *scores3);

/* 피드백: 예측 결과를 기반으로 캔버스 보정 (P4) */
void cab_feedback(CabCanvas *c, uint8_t predicted, uint8_t actual);

/* ══════════════════════════════════════════════════
 *  통계 (CabStats)
 * ══════════════════════════════════════════════════ */

typedef struct {
    uint32_t total_cells;      /* 사용 중인 셀 수               */
    uint32_t capacity;         /* 캔버스 용량                   */
    uint32_t train_bytes;      /* 학습된 총 바이트 수            */
    uint32_t predict_count;    /* 예측 호출 횟수                */
    uint32_t correct_count;    /* 정확 예측 수 (feedback 기반)    */
    uint16_t max_g;            /* 현재 최대 G 값                */
    uint8_t  max_depth_used;   /* 학습에 사용된 최대 depth        */
    uint8_t  layer_cell_count[CAB_NUM_LAYERS]; /* 계층별 셀 수 (상위 255) */
} CabStats;

/* 통계 수집 */
void cab_stats(const CabCanvas *c, CabStats *out);

/* ══════════════════════════════════════════════════
 *  저장 / 로드 API (구현 예정)
 * ══════════════════════════════════════════════════ */

/* 캔버스를 파일에 저장 (바이너리 형식) */
int cab_save(const CabCanvas *c, const char *path);

/* 파일에서 캔버스 로드 */
int cab_load(CabCanvas *c, const char *path);

#endif /* CANVAS_AI_B_H */
