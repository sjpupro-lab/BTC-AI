/*
 * canvas_determinism.h — 결정론 보장 규칙 (DK-1 ~ DK-5)
 * ========================================================
 * 모든 AI 연산은 결정론적이어야 한다.
 * 같은 캔버스 상태 → 같은 결과를 보장.
 *
 * DK-1: 정수 연산만 사용 (float/double 금지)
 * DK-2: 부동소수점 0건 (grep -rn "float\|double" 검증)
 * DK-3: 난수 금지 (rand/srand/random 금지)
 * DK-4: 시간 의존 금지 (time/clock 기반 분기 금지)
 * DK-5: 메모리 주소 의존 금지 (포인터 비교로 결과 결정 금지)
 */

#ifndef CANVAS_DETERMINISM_H
#define CANVAS_DETERMINISM_H

/*
 * 컴파일 타임 검증 매크로:
 * 구조체 크기가 예상과 다르면 컴파일 에러.
 */
#define DK_STATIC_ASSERT(cond, msg) \
    typedef char dk_static_assert_##msg[(cond) ? 1 : -1]

/*
 * DK-1 검증: 정수 연산 확인용 래퍼
 * Q8 고정소수점: 256 = 1.0, 128 = 0.5, 64 = 0.25
 */
#define Q8_ONE    256u
#define Q8_HALF   128u
#define Q8_QUARTER 64u

/* Q8 곱셈: (a * b) >> 8 */
static inline uint32_t q8_mul(uint32_t a, uint32_t b) {
    return (a * b) >> 8;
}

/* Q8 나눗셈: (a << 8) / b */
static inline uint32_t q8_div(uint32_t a, uint32_t b) {
    if (b == 0) return 0;
    return (a << 8) / b;
}

/* 포화 덧셈 (uint16_t) */
static inline uint16_t sat_add_u16(uint16_t a, uint16_t b) {
    uint32_t sum = (uint32_t)a + b;
    return (sum > 0xFFFFu) ? 0xFFFFu : (uint16_t)sum;
}

/* 포화 덧셈 (uint32_t) */
static inline uint32_t sat_add_u32(uint32_t a, uint32_t b) {
    uint32_t sum = a + b;
    return (sum < a) ? 0xFFFFFFFFu : sum;
}

/*
 * DK-2 런타임 검증:
 * 바이너리에서 float/double 사용을 감지하는 건 컴파일 타임에서 해야 함.
 * 빌드 스크립트에서: grep -rn "float\|double" core/ include/ 로 검증.
 */

#endif /* CANVAS_DETERMINISM_H */
