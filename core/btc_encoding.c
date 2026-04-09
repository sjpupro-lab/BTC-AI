/*
 * btc_encoding.c — v6f 가격 인코딩 엔진 구현
 * DK-1: float/double 0건. 정수 전용.
 * Phase 2.
 */
#include "btc_encoding.h"
#include <stdlib.h>
#include <string.h>

/* ── v6f 가격 인코딩 ────────────────────────────── */

uint8_t btc_encode_price(uint32_t close_x100, uint32_t prev_x100)
{
    if (prev_x100 == 0)
        return 127;

    int64_t delta_x10000 = ((int64_t)close_x100 - (int64_t)prev_x100)
                           * 10000 / (int64_t)prev_x100;

    int32_t bucket = 127 + (int32_t)(delta_x10000 * 127 / 500);

    return (uint8_t)BTC_CLAMP(bucket, 0, 254);
}

uint32_t btc_decode_price(uint8_t bucket, uint32_t prev_x100)
{
    int32_t delta_x10000 = ((int32_t)bucket - 127) * 500 / 127;

    uint32_t result = (uint32_t)((int64_t)prev_x100
                      + (int64_t)prev_x100 * delta_x10000 / 10000);

    return result;
}

/* ── 볼륨 인코딩 ────────────────────────────────── */

uint8_t btc_encode_volume(uint32_t vol_x10, uint32_t avg_vol_x10)
{
    if (avg_vol_x10 == 0)
        return 127;

    uint32_t ratio = vol_x10 * 127 / avg_vol_x10;

    return (uint8_t)BTC_CLAMP(ratio, 0, 254);
}

/* ── 캔들 형태 인코딩 ───────────────────────────── */

void btc_candle_encode(BtcCandleBytes *out,
                       const BtcCandle *cur,
                       const BtcCandle *prev,
                       uint32_t avg_vol_x10)
{
    /* [0] 가격 변화 */
    out->price = btc_encode_price(cur->close_x100, prev->close_x100);

    /* [1] 상대 볼륨 */
    out->volume = btc_encode_volume(cur->volume_x10, avg_vol_x10);

    /* 범위 (high - low + 1) — 0 나눗셈 방지 */
    uint32_t range = cur->high_x100 - cur->low_x100 + 1;

    /* [2] 몸통 비율 */
    int32_t body_diff = BTC_ABS32((int32_t)cur->close_x100
                                  - (int32_t)cur->open_x100);
    uint32_t body_raw = (uint32_t)body_diff * 254 / range;
    out->body = (uint8_t)BTC_CLAMP(body_raw, 0, 254);

    /* [3] 상단 꼬리 비율 */
    uint32_t max_oc = (cur->open_x100 > cur->close_x100)
                      ? cur->open_x100 : cur->close_x100;
    uint32_t wick_raw = (cur->high_x100 - max_oc) * 254 / range;
    out->upper_wick = (uint8_t)BTC_CLAMP(wick_raw, 0, 254);
}

/* ── 바이트 스트림 빌더 ─────────────────────────── */

int btc_stream_init(BtcByteStream *s, uint32_t cap)
{
    s->data = (uint8_t *)malloc(cap);
    if (!s->data)
        return BTC_ERR_ALLOC;
    s->len = 0;
    s->cap = cap;
    return BTC_OK;
}

void btc_stream_free(BtcByteStream *s)
{
    free(s->data);
    s->data = NULL;
    s->len  = 0;
    s->cap  = 0;
}

void btc_stream_append(BtcByteStream *s, const BtcCandleBytes *cb)
{
    if (s->len + 4 > s->cap)
        return;

    memcpy(s->data + s->len, cb, 4);
    s->len += 4;
}

void btc_stream_reset(BtcByteStream *s)
{
    s->len = 0;
}
