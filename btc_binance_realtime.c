/*
 * btc_binance_realtime.c — Binance 실시간 멀티 TF 구현
 * =====================================================
 * libcurl로 BTCUSDT klines 수신 → json-c 파싱 → BtcCandle 변환
 *
 * Binance klines 응답 포맷 (배열의 배열):
 *   [0] open_time (ms, int)
 *   [1] open  (string, e.g. "67245.50")
 *   [2] high  (string)
 *   [3] low   (string)
 *   [4] close (string)
 *   [5] volume (string, e.g. "148976.11")
 *
 * DK-1: float/double 0건. 정수 전용.
 * 가격 "67245.50" → 6724550 (×100, 2자리 소수 보존)
 * 볼륨 "148976.1" → 1489761 (×10, 1자리 소수 보존)
 */
#include "btc_binance_realtime.h"
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>

/* ── 응답 버퍼 ──────────────────────────────────── */
#define RESPONSE_BUF_CAP  (512u * 1024u)   /* 512 KB */

typedef struct {
    char   data[RESPONSE_BUF_CAP];
    size_t used;
} ResponseBuf;

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    ResponseBuf *buf = (ResponseBuf *)userdata;
    size_t bytes = size * nmemb;
    if (buf->used + bytes >= RESPONSE_BUF_CAP - 1u) return 0u; /* 버퍼 초과 */
    memcpy(buf->data + buf->used, ptr, bytes);
    buf->used += bytes;
    buf->data[buf->used] = '\0';
    return bytes;
}

/* ── 정수 가격 파싱 (float 없음) ─────────────────── */

/*
 * "67245.50" → 6724550  (×100)
 * "67245"    → 6724500
 * "67245.5"  → 6724550
 * 소수점 이하 최대 2자리만 처리 (BTC/USDT는 2자리)
 */
static uint32_t parse_price_x100(const char *s)
{
    if (!s || *s == '\0') return 0u;
    uint32_t integer = 0u;
    const char *p = s;
    while (*p >= '0' && *p <= '9') {
        integer = integer * 10u + (uint32_t)(*p - '0');
        p++;
    }
    uint32_t frac = 0u;
    if (*p == '.') {
        p++;
        /* 첫 번째 소수 자리 (십분의 1 → ×10) */
        if (*p >= '0' && *p <= '9') { frac  = (uint32_t)(*p - '0') * 10u; p++; }
        /* 두 번째 소수 자리 (백분의 1 → ×1) */
        if (*p >= '0' && *p <= '9') { frac += (uint32_t)(*p - '0'); }
    }
    return integer * 100u + frac;
}

/*
 * "148976.1" → 1489761  (×10)
 * "148976"   → 1489760
 * 소수점 이하 최대 1자리만 처리
 */
static uint32_t parse_volume_x10(const char *s)
{
    if (!s || *s == '\0') return 0u;
    uint32_t integer = 0u;
    const char *p = s;
    while (*p >= '0' && *p <= '9') {
        integer = integer * 10u + (uint32_t)(*p - '0');
        p++;
    }
    uint32_t frac1 = 0u;
    if (*p == '.' && *(p + 1u) >= '0' && *(p + 1u) <= '9') {
        frac1 = (uint32_t)(*(p + 1u) - '0');
    }
    return integer * 10u + frac1;
}

/* ── Binance klines 한 TF 가져오기 ──────────────── */
static int fetch_binance_klines(RealtimeTfBuffer *buf)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.binance.com/api/v3/klines"
             "?symbol=BTCUSDT&interval=%s&limit=%d",
             buf->binance_interval, REALTIME_MAX_CANDLES);

    ResponseBuf rbuf;
    rbuf.used = 0u;
    rbuf.data[0] = '\0';

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &rbuf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "BTC-AI/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || rbuf.used == 0u) return -1;

    /* JSON 파싱: 최상위 배열 확인 */
    struct json_object *root = json_tokener_parse(rbuf.data);
    if (!root) return -1;
    if (!json_object_is_type(root, json_type_array)) {
        json_object_put(root);
        return -1;
    }

    int arr_len = (int)json_object_array_length(root);
    buf->count = 0u;

    for (int i = 0; i < arr_len && buf->count < REALTIME_MAX_CANDLES; i++) {
        struct json_object *item = json_object_array_get_idx(root, i);
        if (!item || !json_object_is_type(item, json_type_array)) continue;

        /* 각 kline 배열에서 필드 추출 */
        struct json_object *ts_obj   = json_object_array_get_idx(item, 0);
        struct json_object *open_obj = json_object_array_get_idx(item, 1);
        struct json_object *high_obj = json_object_array_get_idx(item, 2);
        struct json_object *low_obj  = json_object_array_get_idx(item, 3);
        struct json_object *cls_obj  = json_object_array_get_idx(item, 4);
        struct json_object *vol_obj  = json_object_array_get_idx(item, 5);

        if (!ts_obj || !open_obj || !high_obj || !low_obj || !cls_obj || !vol_obj)
            continue;

        BtcCandle *c = &buf->candles[buf->count];

        /* timestamp: 밀리초 → 초 (정수 나눗셈, DK-1) */
        int64_t ts_ms = json_object_get_int64(ts_obj);
        c->timestamp  = (uint64_t)(ts_ms / 1000LL);

        c->open_x100  = parse_price_x100(json_object_get_string(open_obj));
        c->high_x100  = parse_price_x100(json_object_get_string(high_obj));
        c->low_x100   = parse_price_x100(json_object_get_string(low_obj));
        c->close_x100 = parse_price_x100(json_object_get_string(cls_obj));
        c->volume_x10 = parse_volume_x10(json_object_get_string(vol_obj));

        buf->count++;
    }

    json_object_put(root);
    return (int)buf->count;
}

/* ── API 구현 ────────────────────────────────────── */

void btc_realtime_init(RealtimeTfBuffer buffers[REALTIME_TF_COUNT])
{
    static const char * const INTERVALS[REALTIME_TF_COUNT] = {
        "30m", "1h", "1d", "1w"
    };
    static const BtcTimeframe TFS[REALTIME_TF_COUNT] = {
        TF_15M, TF_1H, TF_1D, TF_1W   /* 30m → TF_15M 매핑 */
    };

    for (int i = 0; i < REALTIME_TF_COUNT; i++) {
        memset(&buffers[i], 0, sizeof(RealtimeTfBuffer));
        buffers[i].tf = TFS[i];
        strncpy(buffers[i].binance_interval, INTERVALS[i], 7u);
        buffers[i].binance_interval[7] = '\0';
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void btc_realtime_update_all(RealtimeTfBuffer buffers[REALTIME_TF_COUNT])
{
    for (int i = 0; i < REALTIME_TF_COUNT; i++) {
        int n = fetch_binance_klines(&buffers[i]);
        if (n < 0) {
            printf("[REALTIME] TF %s 업데이트 실패\n", buffers[i].binance_interval);
        } else {
            printf("[REALTIME] TF %s: %d 캔들 로드\n",
                   buffers[i].binance_interval, n);
        }
    }
}

void btc_realtime_free(RealtimeTfBuffer buffers[REALTIME_TF_COUNT])
{
    (void)buffers;
    curl_global_cleanup();
}
