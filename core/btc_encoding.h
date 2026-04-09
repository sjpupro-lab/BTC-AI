/*
 * btc_encoding.h — v6f 가격 인코딩 엔진
 * DK-1: float/double 0건. 정수 전용.
 * Phase 2.
 */
#pragma once
#include "btc_types.h"

/*
 * v6f 인코딩: ±5% 가격 변화 → 0~254 바이트
 * 기준선 127 = 변화 없음
 * 공식: bucket = 127 + (delta_x10000 × 127) / 500
 * delta_x10000 = (close - prev) × 10000 / prev
 *              → ±500 범위가 ±5%에 해당
 */
uint8_t btc_encode_price(uint32_t close_x100, uint32_t prev_x100);
uint32_t btc_decode_price(uint8_t bucket, uint32_t prev_x100);

/*
 * 볼륨 인코딩: 상대 볼륨 → 0~254
 * 0 = 볼륨 0%, 127 = 평균 볼륨, 254 = 평균 2배+
 */
uint8_t btc_encode_volume(uint32_t vol_x10, uint32_t avg_vol_x10);

/*
 * 캔들 형태 인코딩 → 4바이트 패킷
 * [0] 가격 변화 (v6f)
 * [1] 볼륨 (상대)
 * [2] 몸통 비율: (|close-open|*254) / (high-low+1)
 * [3] 상단 꼬리 비율: (high - max(open,close))*254 / (high-low+1)
 */
typedef struct {
    uint8_t price;       /* v6f 가격 변화 */
    uint8_t volume;      /* 상대 볼륨 */
    uint8_t body;        /* 몸통 비율 */
    uint8_t upper_wick;  /* 상단 꼬리 */
} BtcCandleBytes;

void btc_candle_encode(BtcCandleBytes *out,
                       const BtcCandle *cur,
                       const BtcCandle *prev,
                       uint32_t avg_vol_x10);

/*
 * 바이트 스트림 빌더: N캔들 → 연속 바이트 배열
 * Canvas AI cab_train_fast()에 직접 투입
 */
typedef struct {
    uint8_t  *data;
    uint32_t  len;
    uint32_t  cap;
} BtcByteStream;

int  btc_stream_init(BtcByteStream *s, uint32_t cap);
void btc_stream_free(BtcByteStream *s);
void btc_stream_append(BtcByteStream *s, const BtcCandleBytes *cb);
void btc_stream_reset(BtcByteStream *s);
