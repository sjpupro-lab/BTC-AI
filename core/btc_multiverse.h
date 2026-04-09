/*
 * btc_multiverse.h — Multiverse Inference Engine
 * Seed → Propagate → Cross-Boost → Trace 파이프라인
 * DK-1: float/double 0건. 정수 전용.
 * Phase 8.
 */
#pragma once
#include "btc_types.h"
#include "btc_canvas_brain.h"

/* ── 상수 ─────────────────────────────────────── */
#define MV_MAX_DEPTH   32    /* 최대 컨텍스트 깊이 */
#define MV_MAX_HOPS    3     /* 전파 재귀 깊이 */
#define MV_TOP_K       6     /* 전파 시 상위 K개 선택 */
#define MV_DECAY_NUM   7     /* 에너지 감쇠 분자 */
#define MV_DECAY_DEN   8     /* 에너지 감쇠 분모 (7/8 per hop) */
#define MV_BH_DEPTH    3     /* BH/WH 경계 (d<3=BH, d>=3=WH) */
#define MV_CROSS_ALIGN 3     /* BH+WH 동시 활성 시 증폭 배수 */

/* ── MvSky: 멀티버스 에너지 공간 ─────────────── */
typedef struct {
    uint64_t sky[MV_MAX_DEPTH][256]; /* [depth][byte] → 에너지 */
    uint32_t active_depths;          /* 활성 깊이 수 */
    uint32_t total_stars;            /* 비제로 (depth,byte) 쌍 수 */
    uint32_t crossings;              /* cross-boost 발생 횟수 */
} MvSky;

/* ── MvPrediction: 멀티버스 예측 결과 ─────────── */
typedef struct {
    SignalDir direction;       /* LONG/SHORT/HOLD */
    uint64_t  energy;          /* 최고 에너지 (신뢰도) */
    uint32_t  active_depths;   /* 활성 유니버스 수 */
    uint8_t   predicted_byte;  /* 예측 바이트 (0-255) */
    uint8_t   confidence;      /* 0-100 */
    uint8_t   top3_bytes[3];
    uint64_t  top3_energies[3];
} MvPrediction;

/*
 * 멀티버스 추론 파이프라인
 * 1. 캔들 인코딩 → 바이트 스트림
 * 2. SEED: 모든 깊이에서 캔버스 조회, depth³ 가중
 * 3. PROPAGATE: top-K 바이트로 재귀 분기 (branch-of-branch)
 * 4. CROSS-BOOST: BH+WH 동시 활성 바이트 증폭
 * 5. TRACE: byte_energy + word_energy×2 → 최종 선택
 */
void btc_mv_predict(MvPrediction *out,
                    BtcCanvasBrain *brain,
                    BtcTimeframe tf,
                    const BtcCandle *candles,
                    uint32_t count);

/* 내부 단계 함수 (테스트용 공개) */
void btc_mv_sky_init(MvSky *sky);
void btc_mv_seed(MvSky *sky, const CabCanvas *canvas,
                 const uint8_t *stream, uint32_t stream_len);
void btc_mv_propagate(MvSky *sky, const CabCanvas *canvas,
                      const uint8_t *stream, uint32_t stream_len);
void btc_mv_cross_boost(MvSky *sky);
uint8_t btc_mv_trace(const MvSky *sky, uint64_t brightness[256]);
