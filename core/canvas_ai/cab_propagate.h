/*
 * cab_propagate.h — 밝기 전파 엔진 API
 * ======================================
 * 점등(activate) → 전파(propagate) 2단계로 캔버스의 의미장을 형성.
 *
 * 전파 방식:
 *   1) 동일 계층 확산: 같은 depth, 인접 probe 범위 내 확산
 *   2) 상위 계층 전파: depth 4(형태소) → depth 8(단어) → depth 15(구)
 *   3) 하위 계층 전파: 단어 활성 → 구성 형태소 재활성화
 *   4) 의미 확산: A[31:24] 클러스터가 같은 셀 → 약한 밝기 전달
 */

#ifndef CAB_PROPAGATE_H
#define CAB_PROPAGATE_H

#include "canvas_ai_b.h"
#include "canvas_ai_layers.h"

/* ── 전파 설정 ─────────────────────────────────── */
typedef struct {
    uint16_t same_layer_radius;   /* 동일 계층 내 확산 반경 (기본: 4) */
    uint16_t upward_gain;         /* 상위 계층 전파 강도 (기본: 128 = 0.5x Q8) */
    uint16_t downward_gain;       /* 하위 계층 전파 강도 (기본: 64 = 0.25x Q8) */
    uint16_t cluster_gain;        /* A채널 클러스터 전파 강도 (기본: 32 = 0.125x Q8) */
    uint16_t inhibition;          /* 억제 강도 (기본: 0 = 비활성) */
    uint8_t  max_iterations;      /* 전파 반복 횟수 (기본: 2) */
} CabPropConfig;

/* 기본 설정 (v2: 패턴인식 강화) */
static inline CabPropConfig cab_prop_default(void) {
    return (CabPropConfig){
        .same_layer_radius = 4,
        .upward_gain       = 192,   /* 0.75x Q8 (was 0.5x) — 상위 전파 강화 */
        .downward_gain     = 96,    /* 0.375x Q8 (was 0.25x) — 하위 전파 강화 */
        .cluster_gain      = 48,    /* 0.1875x Q8 (was 0.125x) — 의미 확산 강화 */
        .inhibition        = 0,
        .max_iterations    = 2,     /* 전파 반복 (gain 증가로 충분) */
    };
}

/* ── 활성화 버퍼 ───────────────────────────────── */
/*
 * 현재 점등 상태를 임시 저장.
 * 캔버스 셀의 G를 직접 수정하지 않음!
 * 전파는 이 버퍼 위에서 수행.
 */
typedef struct {
    uint32_t *activation;   /* [width * height] 활성화 값 (Q8) */
    uint16_t  width;
    uint16_t  height;
} CabActivation;

/* ── API ───────────────────────────────────────── */

/* 활성화 버퍼 생성/소멸 */
int  cab_activation_init(CabActivation *act, uint16_t w, uint16_t h);
void cab_activation_free(CabActivation *act);
void cab_activation_clear(CabActivation *act);

/* 1단계: 점등 — 현재 문맥의 셀들을 활성화 버퍼에 기록 */
void cab_activate_context(CabActivation *act, const CabCanvas *c,
                          const uint8_t *window, uint8_t win_len);

/* 2단계: 전파 — 활성화 버퍼 내에서 밝기 확산 */
void cab_propagate(CabActivation *act, const CabCanvas *c,
                   const CabPropConfig *cfg);

/* 유틸리티: 활성화된 셀 수 카운트 */
uint32_t cab_activation_count(const CabActivation *act);

#endif /* CAB_PROPAGATE_H */
