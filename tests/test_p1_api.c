/*
 * test_p1_api.c — Phase 1: API 파싱 + TF 매니저 검증
 */
#include <stdio.h>
#include <string.h>
#include "../core/btc_types.h"
#include "../core/btc_api.h"

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s  (line %d)\n", msg, __LINE__); g_fail++; } \
} while(0)

/* CryptoCompare 실제 응답 형식 mock */
static const char *MOCK_JSON =
    "{\"Response\":\"Success\","
    "\"Data\":{\"Data\":["
    "{\"time\":1700000000,\"open\":35000,\"high\":35500,"
    "\"low\":34800,\"close\":35200,\"volumefrom\":12.5},"
    "{\"time\":1700003600,\"open\":35200,\"high\":35800,"
    "\"low\":35100,\"close\":35600,\"volumefrom\":18.3},"
    "{\"time\":1700007200,\"open\":35600,\"high\":36000,"
    "\"low\":35400,\"close\":35900,\"volumefrom\":9.7}"
    "]}}";

int main(void) {
    printf("=== Phase 1: API & TF Manager ===\n\n");

    /* P1-T1: JSON 파싱 기본 */
    printf("[T1] JSON 파싱 기본\n");
    BtcApiResult res;
    int ret = btc_api_parse_mock(&res, MOCK_JSON);
    ASSERT(ret == BTC_OK,           "parse returns BTC_OK");
    ASSERT(res.error == BTC_OK,     "res.error == OK");
    ASSERT(res.count == 3,          "3 candles parsed");
    ASSERT(res.candles[0].open_x100   == 3500000, "candle[0] open=35000.00");
    ASSERT(res.candles[0].close_x100  == 3520000, "candle[0] close=35200.00");
    ASSERT(res.candles[0].high_x100   == 3550000, "candle[0] high=35500.00");
    ASSERT(res.candles[0].low_x100    == 3480000, "candle[0] low=34800.00");
    ASSERT(res.candles[0].timestamp   == 1700000000ULL, "candle[0] time OK");

    /* P1-T2: 볼륨 파싱 (소수 포함) */
    printf("\n[T2] 볼륨 소수 파싱\n");
    /* volumefrom=12.5 → 125 (×10) */
    ASSERT(res.candles[0].volume_x10 == 125, "vol 12.5 → volume_x10=125");
    ASSERT(res.candles[1].volume_x10 == 183, "vol 18.3 → volume_x10=183");

    /* P1-T3: TF 매니저 LIFO 동작 */
    printf("\n[T3] TF 매니저 LIFO (최신이 idx=0)\n");
    BtcTfManager m;
    btc_tf_init(&m);

    BtcCandle c1 = {.timestamp=1000, .close_x100=3500000};
    BtcCandle c2 = {.timestamp=2000, .close_x100=3501000};
    BtcCandle c3 = {.timestamp=3000, .close_x100=3502000};

    btc_tf_push(&m, TF_1H, &c1);
    btc_tf_push(&m, TF_1H, &c2);
    btc_tf_push(&m, TF_1H, &c3);

    ASSERT(btc_tf_count(&m, TF_1H) == 3, "count == 3");
    ASSERT(btc_tf_get(&m, TF_1H, 0)->close_x100 == 3502000, "idx 0 = newest");
    ASSERT(btc_tf_get(&m, TF_1H, 1)->close_x100 == 3501000, "idx 1 = prev");
    ASSERT(btc_tf_get(&m, TF_1H, 2)->close_x100 == 3500000, "idx 2 = oldest");

    /* P1-T4: 버퍼 오버플로 방지 */
    printf("\n[T4] 버퍼 오버플로 방지 (max=%d)\n", TF_BUFFER_SIZE);
    BtcTfManager m2;
    btc_tf_init(&m2);
    BtcCandle dummy = {.close_x100 = 3500000};
    for (int i = 0; i < TF_BUFFER_SIZE + 10; i++) {
        dummy.timestamp = (uint64_t)i;
        dummy.close_x100 = 3500000 + (uint32_t)i * 100;
        btc_tf_push(&m2, TF_1M, &dummy);
    }
    ASSERT(btc_tf_count(&m2, TF_1M) == TF_BUFFER_SIZE,
           "count capped at TF_BUFFER_SIZE");
    /* 최신 캔들이 idx=0에 있어야 함 */
    uint32_t newest_expected = 3500000 + (uint32_t)(TF_BUFFER_SIZE + 9) * 100;
    ASSERT(btc_tf_get(&m2, TF_1M, 0)->close_x100 == newest_expected,
           "newest candle at idx=0 after overflow");

    /* P1-T5: 평균 볼륨 계산 */
    printf("\n[T5] 평균 볼륨\n");
    BtcTfManager m3;
    btc_tf_init(&m3);
    BtcCandle vc;
    memset(&vc, 0, sizeof(vc));
    vc.volume_x10 = 100;
    btc_tf_push(&m3, TF_1H, &vc);
    vc.volume_x10 = 200;
    btc_tf_push(&m3, TF_1H, &vc);
    vc.volume_x10 = 300;
    btc_tf_push(&m3, TF_1H, &vc);
    /* 평균 = (300+200+100)/3 = 200 */
    uint32_t avg = btc_tf_avg_vol(&m3, TF_1H, 3);
    ASSERT(avg == 200, "avg vol (100+200+300)/3 == 200");

    /* P1-T6: NULL 안전성 */
    printf("\n[T6] NULL 안전성\n");
    ASSERT(btc_tf_count(NULL, TF_1H) == 0, "count(NULL) == 0");
    ASSERT(btc_tf_get(NULL, TF_1H, 0) == NULL, "get(NULL) == NULL");

    /* ── 결과 ── */
    printf("\n============================\n");
    printf("Phase 1 결과: %d PASS, %d FAIL\n", g_pass, g_fail);
    printf("============================\n");
    return (g_fail == 0) ? 0 : 1;
}
