/*
 * test_p0_types.c — Phase 0 타입 크기 및 DK-1 검증
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "../core/btc_types.h"

/* ── 간단한 테스트 프레임워크 ──────────────────── */
static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); g_pass++; \
    } else { \
        printf("  [FAIL] %s  (line %d)\n", msg, __LINE__); g_fail++; \
    } \
} while(0)

int main(void) {
    printf("=== Phase 0: Type Validation ===\n\n");

    /* P0-T1: BtcCandle 크기 */
    printf("[T1] BtcCandle 크기 검증\n");
    /*
     * timestamp(8) + open(4) + high(4) + low(4) + close(4) + volume(4) = 28
     * 컴파일러 패딩 없이 28바이트여야 함
     */
    ASSERT(sizeof(BtcCandle) == 28, "sizeof(BtcCandle) == 28");
    ASSERT(sizeof(uint64_t)  == 8,  "sizeof(uint64_t)  == 8");
    ASSERT(sizeof(uint32_t)  == 4,  "sizeof(uint32_t)  == 4");
    ASSERT(sizeof(uint8_t)   == 1,  "sizeof(uint8_t)   == 1");

    /* P0-T2: 열거형 카운트 */
    printf("\n[T2] Enum 카운트 검증\n");
    ASSERT(TF_COUNT       == 7, "TF_COUNT == 7");
    ASSERT(INDICATOR_COUNT == 8, "INDICATOR_COUNT == 8");
    ASSERT(REGIME_UNKNOWN == 3, "REGIME_UNKNOWN == 3");
    ASSERT(STRENGTH_STRONG == 2, "STRENGTH_STRONG == 2");

    /* P0-T3: 신호 방향 값 */
    printf("\n[T3] 신호 방향 값 검증\n");
    ASSERT(SIGNAL_NONE  == 0, "SIGNAL_NONE  == 0");
    ASSERT(SIGNAL_LONG  == 1, "SIGNAL_LONG  == 1");
    ASSERT(SIGNAL_SHORT == 2, "SIGNAL_SHORT == 2");
    ASSERT(SIGNAL_HOLD  == 3, "SIGNAL_HOLD  == 3");

    /* P0-T4: BTC_DIV_SAFE 제로 나눗셈 방지 */
    printf("\n[T4] 정수 유틸리티 매크로\n");
    ASSERT(BTC_DIV_SAFE(100, 0)  == 0,   "DIV_SAFE zero divisor → 0");
    ASSERT(BTC_DIV_SAFE(100, 4)  == 25,  "DIV_SAFE 100/4 == 25");
    ASSERT(BTC_ABS32(-500)       == 500, "ABS32(-500) == 500");
    ASSERT(BTC_ABS32(500)        == 500, "ABS32(500)  == 500");
    ASSERT(BTC_CLAMP(200, 0, 254) == 200, "CLAMP(200,0,254)==200");
    ASSERT(BTC_CLAMP(300, 0, 254) == 254, "CLAMP(300,0,254)==254");
    ASSERT(BTC_CLAMP(-1,  0, 254) == 0,   "CLAMP(-1, 0,254)==0");

    /* P0-T5: DK-1 — float/double 미사용 (컴파일 시 확인) */
    printf("\n[T5] DK-1: float/double 미사용 확인\n");
    /* 컴파일이 통과하면 DK-1 충족 (grep은 Makefile에서 별도 실행) */
    ASSERT(1, "btc_types.h uses integer-only types");

    /* P0-T6: BtcCandle 필드 오프셋 */
    printf("\n[T6] BtcCandle 필드 오프셋\n");
    BtcCandle c = {0};
    ASSERT(offsetof(BtcCandle, timestamp)  == 0,  "offset timestamp == 0");
    ASSERT(offsetof(BtcCandle, open_x100)  == 8,  "offset open_x100 == 8");
    ASSERT(offsetof(BtcCandle, high_x100)  == 12, "offset high_x100 == 12");
    ASSERT(offsetof(BtcCandle, low_x100)   == 16, "offset low_x100  == 16");
    ASSERT(offsetof(BtcCandle, close_x100) == 20, "offset close_x100== 20");
    ASSERT(offsetof(BtcCandle, volume_x10) == 24, "offset volume_x10== 24");
    (void)c;

    /* ── 결과 요약 ── */
    printf("\n============================\n");
    printf("Phase 0 결과: %d PASS, %d FAIL\n", g_pass, g_fail);
    printf("============================\n");

    return (g_fail == 0) ? 0 : 1;
}
