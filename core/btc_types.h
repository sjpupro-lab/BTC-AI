/*
 * btc_types.h — BTC Candle Sentinel 공통 타입
 * =============================================
 * DK-1: float/double 0건. 모든 가격은 정수(×100).
 * Phase 0 기준.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* ── 캔들 데이터 ──────────────────────────────── */
/*
 * packed: uint64_t(8) + uint32_t×5(20) = 28바이트 강제.
 * 컴파일러 패딩 없이 정확히 28바이트.
 * NDK JNI 직렬화 시 레이아웃 일치 보장.
 */
typedef struct __attribute__((packed)) {
    uint64_t  timestamp;    /* Unix epoch (초) */
    uint32_t  open_x100;    /* 달러 × 100 */
    uint32_t  high_x100;
    uint32_t  low_x100;
    uint32_t  close_x100;
    uint32_t  volume_x10;   /* BTC 볼륨 × 10 */
} BtcCandle;

/* ── 타임프레임 (Canvas AI 7계층에 1:1 대응) ─── */
typedef enum {
    TF_1M  = 0,   /* 1분  → Byte    layer */
    TF_5M  = 1,   /* 5분  → Morpheme */
    TF_15M = 2,   /* 15분 → Word     */
    TF_1H  = 3,   /* 1시간→ Phrase   */
    TF_4H  = 4,   /* 4시간→ Clause   */
    TF_1D  = 5,   /* 일봉 → Sentence */
    TF_1W  = 6,   /* 주봉 → Topic    */
    TF_COUNT = 7
} BtcTimeframe;

/* ── 시장 레짐 (B채널 sem_B에 매핑) ─────────── */
typedef enum {
    REGIME_BULL     = 0,
    REGIME_BEAR     = 1,
    REGIME_SIDEWAYS = 2,
    REGIME_UNKNOWN  = 3
} MarketRegime;

/* ── 신호 방향 ───────────────────────────────── */
typedef enum {
    SIGNAL_NONE  = 0,
    SIGNAL_LONG  = 1,
    SIGNAL_SHORT = 2,
    SIGNAL_HOLD  = 3
} SignalDir;

/* ── 신호 강도 ───────────────────────────────── */
typedef enum {
    STRENGTH_WEAK   = 0,   /* score  0~40  */
    STRENGTH_MED    = 1,   /* score 41~69  */
    STRENGTH_STRONG = 2    /* score 70~100 */
} SignalStrength;

/* ── 지표 종류 ───────────────────────────────── */
typedef enum {
    INDICATOR_BB       = 0,  /* 볼린저 밴드 (무료) */
    INDICATOR_SR7      = 1,  /* 7단계 S/R   (무료) */
    INDICATOR_RSI      = 2,  /* RSI         (프리미엄) */
    INDICATOR_MACD     = 3,  /* MACD        (프리미엄) */
    INDICATOR_VOL_PROF = 4,  /* 볼륨 프로파일(프리미엄) */
    INDICATOR_FIBO     = 5,  /* 피보나치    (프리미엄) */
    INDICATOR_ICHIMOKU = 6,  /* 이치모쿠    (프리미엄) */
    INDICATOR_PAT_SIM  = 7,  /* 패턴 유사도 (프리미엄, Canvas AI 고유) */
    INDICATOR_COUNT    = 8
} IndicatorType;

/* ── 에러 코드 ───────────────────────────────── */
#define BTC_OK            0
#define BTC_ERR_ALLOC    -1
#define BTC_ERR_PARAM    -2
#define BTC_ERR_IO       -3
#define BTC_ERR_PARSE    -4
#define BTC_ERR_OVERFLOW -5

/* ── DK-1 보조 매크로 ────────────────────────── */
/* 정수 나눗셈 안전 가드 */
#define BTC_DIV_SAFE(a, b) ((b) != 0 ? (a) / (b) : 0)

/* 정수 절댓값 */
#define BTC_ABS32(x) ((int32_t)(x) < 0 ? -(int32_t)(x) : (int32_t)(x))

/* 클램프 */
#define BTC_CLAMP(v, lo, hi) \
    ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
