/*
 * test_p2_encoding.c — Phase 2 인코딩 엔진 검증
 * DK-1: float/double 0건.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "../core/btc_types.h"
#include "../core/btc_encoding.h"

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
    printf("=== Phase 2: Encoding Engine Validation ===\n\n");

    /* P2-T1: 가격 변화 없음 → 127 */
    printf("[T1] btc_encode_price: 변화 없음\n");
    ASSERT(btc_encode_price(3500000, 3500000) == 127,
           "encode_price(3500000, 3500000) == 127");

    /* P2-T2: +5% → 상단 근처 (>=250) */
    printf("\n[T2] btc_encode_price: +5%%\n");
    uint8_t up5 = btc_encode_price(3675000, 3500000);
    printf("  +5%% bucket = %u\n", up5);
    ASSERT(up5 >= 250, "encode_price(+5%%) >= 250");

    /* P2-T3: -5% → 하단 근처 (<=4) */
    printf("\n[T3] btc_encode_price: -5%%\n");
    uint8_t dn5 = btc_encode_price(3325000, 3500000);
    printf("  -5%% bucket = %u\n", dn5);
    ASSERT(dn5 <= 4, "encode_price(-5%%) <= 4");

    /* P2-T4: 라운드트립 오차 < 5000 ($50) */
    printf("\n[T4] 라운드트립 정밀도\n");
    uint32_t orig = 3500000;
    uint32_t close_val = 3550000;  /* +$500 */
    uint8_t  enc = btc_encode_price(close_val, orig);
    uint32_t dec = btc_decode_price(enc, orig);
    int32_t  err = BTC_ABS32((int32_t)dec - (int32_t)close_val);
    printf("  orig=%u close=%u enc=%u dec=%u err=%d\n",
           orig, close_val, enc, dec, err);
    ASSERT(err < 5000, "roundtrip error < 5000 ($50)");

    /* P2-T5: 캔들 인코딩 바이트 범위 0~254 */
    printf("\n[T5] btc_candle_encode: 바이트 범위 확인\n");
    BtcCandle prev_c = {0};
    prev_c.timestamp  = 1000;
    prev_c.open_x100  = 3490000;
    prev_c.high_x100  = 3510000;
    prev_c.low_x100   = 3480000;
    prev_c.close_x100 = 3500000;
    prev_c.volume_x10 = 800;

    BtcCandle cur_c = {0};
    cur_c.timestamp  = 1060;
    cur_c.open_x100  = 3500000;
    cur_c.high_x100  = 3530000;
    cur_c.low_x100   = 3495000;
    cur_c.close_x100 = 3520000;
    cur_c.volume_x10 = 1000;

    BtcCandleBytes cb;
    btc_candle_encode(&cb, &cur_c, &prev_c, 800);
    printf("  price=%u volume=%u body=%u upper_wick=%u\n",
           cb.price, cb.volume, cb.body, cb.upper_wick);
    ASSERT(cb.price <= 254,      "candle price <= 254");
    ASSERT(cb.volume <= 254,     "candle volume <= 254");
    ASSERT(cb.body <= 254,       "candle body <= 254");
    ASSERT(cb.upper_wick <= 254, "candle upper_wick <= 254");

    /* P2-T6: 볼륨 평균 → 127 */
    printf("\n[T6] btc_encode_volume: 평균 볼륨\n");
    ASSERT(btc_encode_volume(800, 800) == 127,
           "encode_volume(800, 800) == 127");

    /* P2-T7: 스트림 빌더 100개 캔들 → len 400 */
    printf("\n[T7] BtcByteStream 빌더\n");
    BtcByteStream stream;
    int rc = btc_stream_init(&stream, 2000);
    ASSERT(rc == BTC_OK, "stream_init returns BTC_OK");

    BtcCandleBytes dummy = {100, 127, 50, 30};
    for (int i = 0; i < 100; i++) {
        btc_stream_append(&stream, &dummy);
    }
    printf("  stream.len = %u\n", stream.len);
    ASSERT(stream.len == 400, "100 candles → len 400");

    btc_stream_reset(&stream);
    ASSERT(stream.len == 0, "stream_reset → len 0");

    btc_stream_free(&stream);
    ASSERT(stream.data == NULL, "stream_free → data NULL");

    /* P2-T8: close=0 엣지 케이스 */
    printf("\n[T8] btc_encode_price: close=0 엣지 케이스\n");
    uint8_t zero_close = btc_encode_price(0, 3500000);
    printf("  close=0 bucket = %u\n", zero_close);
    ASSERT(zero_close == 0, "encode_price(0, 3500000) == 0 (clamped)");

    /* P2-T9: 디코드 127 → 변화 없음 */
    printf("\n[T9] btc_decode_price: bucket 127 → 변화 없음\n");
    uint32_t dec127 = btc_decode_price(127, 3500000);
    printf("  decode(127, 3500000) = %u\n", dec127);
    ASSERT(dec127 == 3500000, "decode_price(127, 3500000) == 3500000");

    /* ── 결과 요약 ── */
    printf("\n============================\n");
    printf("Phase 2 결과: %d PASS, %d FAIL\n", g_pass, g_fail);
    printf("============================\n");

    return (g_fail == 0) ? 0 : 1;
}
