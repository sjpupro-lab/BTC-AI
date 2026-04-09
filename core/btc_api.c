/*
 * btc_api.c — CryptoCompare API 래퍼 구현
 * =========================================
 * Phase 1. DK-1: float/double 0건.
 *
 * 실제 HTTP는 Phase 8 JNI에서 OkHttp로 처리.
 * 여기서는 JSON 파싱 + TF 매니저만 구현.
 */
#include "btc_api.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ══════════════════════════════════════════════
 *  JSON 미니 파서 (표준 라이브러리만 사용)
 * ══════════════════════════════════════════════
 * CryptoCompare 응답 형식:
 * {"Data":{"Data":[{"time":N,"open":N,"high":N,"low":N,"close":N,"volumefrom":N},...}]}
 *
 * 간단한 토큰 스캔 방식 — 재귀/동적 할당 없음.
 */

/* 문자열에서 키:숫자 쌍 파싱 */
static int json_find_uint64(const char *json, const char *key, uint64_t *out) {
    const char *p = json;
    size_t klen = strlen(key);

    while (*p) {
        /* "key": 형태 탐색 */
        if (*p == '"') {
            p++;
            /* 키 매치 */
            int match = 1;
            for (size_t i = 0; i < klen; i++) {
                if (p[i] != key[i]) { match = 0; break; }
            }
            if (match && p[klen] == '"') {
                const char *v = p + klen + 1;
                while (*v == ':' || *v == ' ') v++;
                if (*v >= '0' && *v <= '9') {
                    *out = 0;
                    while (*v >= '0' && *v <= '9') {
                        *out = *out * 10 + (uint64_t)(*v - '0');
                        v++;
                    }
                    return 1;
                }
            }
        }
        p++;
    }
    return 0;
}

/*
 * 가격 파싱: CryptoCompare는 정수 달러 반환
 * → ×100 변환 (소수점 처리)
 * 예: "open":35000  → open_x100 = 3500000
 *     "open":35000.5 → open_x100 = 3500050 (소수 2자리 × 100)
 */
static uint32_t parse_price_x100(const char *start) {
    const char *p = start;
    uint32_t integer_part = 0;
    uint32_t frac_part    = 0;
    int      frac_digits  = 0;

    /* 정수 부분 */
    while (*p >= '0' && *p <= '9') {
        integer_part = integer_part * 10 + (uint32_t)(*p - '0');
        p++;
    }
    /* 소수 부분 (최대 2자리) */
    if (*p == '.') {
        p++;
        for (int d = 0; d < 2 && *p >= '0' && *p <= '9'; d++) {
            frac_part = frac_part * 10 + (uint32_t)(*p - '0');
            frac_digits++;
            p++;
        }
        /* 소수 1자리면 ×10 보정 */
        if (frac_digits == 1) frac_part *= 10;
    }
    return integer_part * 100 + frac_part;
}

static uint32_t parse_vol_x10(const char *start) {
    const char *p = start;
    uint32_t integer_part = 0;
    uint32_t frac_part    = 0;
    int      frac_digits  = 0;

    while (*p >= '0' && *p <= '9') {
        integer_part = integer_part * 10 + (uint32_t)(*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        if (*p >= '0' && *p <= '9') {
            frac_part = (uint32_t)(*p - '0');
            frac_digits = 1;
            p++;
        }
    }
    (void)frac_digits;
    return integer_part * 10 + frac_part;
}

/* 키 뒤 숫자 문자열 시작 위치 반환 */
static const char *json_key_val_start(const char *json, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = json;
    size_t slen = strlen(search);

    while (*p) {
        if (strncmp(p, search, slen) == 0) {
            p += slen;
            while (*p == ':' || *p == ' ') p++;
            return p;
        }
        p++;
    }
    return NULL;
}

/* ══════════════════════════════════════════════
 *  btc_api_parse_mock
 * ══════════════════════════════════════════════ */
int btc_api_parse_mock(BtcApiResult *out, const char *json) {
    if (!out || !json) return BTC_ERR_PARAM;

    memset(out, 0, sizeof(*out));

    /* "Data" 배열 탐색 */
    const char *data_start = strstr(json, "\"Data\":[");
    if (!data_start) {
        /* 중첩 구조: {"Data":{"Data":[...] */
        data_start = strstr(json, "\"Data\":{");
        if (data_start) {
            data_start = strstr(data_start + 8, "\"Data\":[");
        }
    }
    if (!data_start) {
        snprintf(out->error_msg, sizeof(out->error_msg), "Data array not found");
        out->error = BTC_ERR_PARSE;
        return BTC_ERR_PARSE;
    }

    const char *p = strchr(data_start, '[');
    if (!p) {
        out->error = BTC_ERR_PARSE;
        return BTC_ERR_PARSE;
    }
    p++; /* '[' 다음 */

    uint32_t idx = 0;
    while (*p && *p != ']' && idx < API_MAX_CANDLES) {
        /* 다음 '{' 탐색 */
        while (*p && *p != '{' && *p != ']') p++;
        if (*p != '{') break;

        /* 객체 파싱 */
        const char *obj_start = p;
        const char *obj_end   = strchr(p, '}');
        if (!obj_end) break;

        /* 임시 버퍼에 객체 복사 */
        char obj[512];
        size_t obj_len = (size_t)(obj_end - obj_start + 1);
        if (obj_len >= sizeof(obj)) obj_len = sizeof(obj) - 1;
        memcpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';

        BtcCandle *c = &out->candles[idx];

        /* time */
        const char *v;
        uint64_t t = 0;
        if (json_find_uint64(obj, "time", &t)) c->timestamp = t;

        /* open */
        v = json_key_val_start(obj, "open");
        if (v) c->open_x100 = parse_price_x100(v);

        /* high */
        v = json_key_val_start(obj, "high");
        if (v) c->high_x100 = parse_price_x100(v);

        /* low */
        v = json_key_val_start(obj, "low");
        if (v) c->low_x100 = parse_price_x100(v);

        /* close */
        v = json_key_val_start(obj, "close");
        if (v) c->close_x100 = parse_price_x100(v);

        /* volumefrom */
        v = json_key_val_start(obj, "volumefrom");
        if (v) c->volume_x10 = parse_vol_x10(v);

        /* 유효 캔들 확인 */
        if (c->open_x100 > 0 || c->close_x100 > 0) {
            idx++;
        }

        p = obj_end + 1;
    }

    out->count = idx;
    out->error = (idx > 0) ? BTC_OK : BTC_ERR_PARSE;
    return out->error;
}

/* 실제 HTTP 구현 — Android에서는 JNI 통해 Java OkHttp 사용 */
int btc_api_fetch(BtcApiResult *out, BtcTimeframe tf,
                  uint32_t limit, const char *api_key) {
    (void)tf; (void)limit; (void)api_key;
    if (!out) return BTC_ERR_PARAM;
    /* Phase 8에서 JNI 콜백으로 채워짐 */
    out->error = BTC_ERR_IO;
    snprintf(out->error_msg, sizeof(out->error_msg),
             "HTTP fetch requires JNI (Phase 8)");
    return BTC_ERR_IO;
}

int  btc_api_subscribe(BtcTickCallback cb, void *ctx, const char *api_key) {
    (void)cb; (void)ctx; (void)api_key;
    return BTC_ERR_IO; /* Phase 8에서 구현 */
}
void btc_api_unsubscribe(void) { /* Phase 8 */ }

/* ══════════════════════════════════════════════
 *  BtcTfManager 구현
 * ══════════════════════════════════════════════ */

void btc_tf_init(BtcTfManager *m) {
    if (!m) return;
    memset(m, 0, sizeof(*m));
}

void btc_tf_push(BtcTfManager *m, BtcTimeframe tf, const BtcCandle *c) {
    if (!m || !c || (uint32_t)tf >= TF_COUNT) return;

    uint32_t cur = m->count[tf];

    if (cur >= TF_BUFFER_SIZE) {
        /* 버퍼 꽉 참 — 가장 오래된 캔들(마지막) 드롭 */
        cur = TF_BUFFER_SIZE - 1;
    }

    /* 우측 시프트 → idx 0에 최신 삽입 */
    for (uint32_t i = cur; i > 0; i--) {
        m->buf[tf][i] = m->buf[tf][i - 1];
    }
    m->buf[tf][0] = *c;

    if (m->count[tf] < TF_BUFFER_SIZE) {
        m->count[tf]++;
    }
    m->last_update[tf] = c->timestamp;
}

const BtcCandle *btc_tf_get(const BtcTfManager *m, BtcTimeframe tf,
                              uint32_t idx) {
    if (!m || (uint32_t)tf >= TF_COUNT) return NULL;
    if (idx >= m->count[tf])            return NULL;
    return &m->buf[tf][idx];
}

uint32_t btc_tf_count(const BtcTfManager *m, BtcTimeframe tf) {
    if (!m || (uint32_t)tf >= TF_COUNT) return 0;
    return m->count[tf];
}

uint32_t btc_tf_avg_vol(const BtcTfManager *m, BtcTimeframe tf, uint32_t n) {
    if (!m || (uint32_t)tf >= TF_COUNT) return 0;
    uint32_t cnt = m->count[tf];
    if (cnt == 0) return 0;
    if (n > cnt) n = cnt;

    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        sum += m->buf[tf][i].volume_x10;
    }
    return (uint32_t)(sum / n);
}
