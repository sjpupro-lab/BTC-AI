/*
 * btc_api.h — CryptoCompare API 래퍼 + 멀티 타임프레임 매니저
 * =============================================================
 * Phase 1. DK-1: float/double 0건.
 */
#pragma once
#include "btc_types.h"

/* ── API 결과 ────────────────────────────────── */
#define API_MAX_CANDLES 2000

typedef struct {
    BtcCandle candles[API_MAX_CANDLES];
    uint32_t  count;
    int       error;         /* 0=OK, 음수=에러 코드 */
    char      error_msg[64];
} BtcApiResult;

/*
 * CryptoCompare REST endpoint:
 *   https://min-api.cryptocompare.com/data/v2/histominute
 *   ?fsym=BTC&tsym=USD&limit=N&api_key=KEY
 *
 * 타임프레임별 엔드포인트 매핑:
 *   TF_1M  → histominute
 *   TF_5M  → histominute (aggregate=5)
 *   TF_15M → histominute (aggregate=15)
 *   TF_1H  → histohour
 *   TF_4H  → histohour   (aggregate=4)
 *   TF_1D  → histoday
 *   TF_1W  → histoday    (aggregate=7)
 */

/* 동기 fetch — NDK 스레드에서 호출 */
int btc_api_fetch(
    BtcApiResult *out,
    BtcTimeframe  tf,
    uint32_t      limit,     /* 최대 API_MAX_CANDLES */
    const char   *api_key
);

/* JSON mock 파서 — 테스트용 */
int btc_api_parse_mock(BtcApiResult *out, const char *json);

/* WebSocket 라이브 틱 콜백 */
typedef void (*BtcTickCallback)(const BtcCandle *candle, void *ctx);
int  btc_api_subscribe(BtcTickCallback cb, void *ctx, const char *api_key);
void btc_api_unsubscribe(void);

/* ── 멀티 타임프레임 매니저 ──────────────────── */
#define TF_BUFFER_SIZE 500   /* 타임프레임당 최대 보관 캔들 수 */

typedef struct {
    BtcCandle  buf[TF_COUNT][TF_BUFFER_SIZE];
    uint32_t   count[TF_COUNT];
    uint64_t   last_update[TF_COUNT];
} BtcTfManager;

void             btc_tf_init(BtcTfManager *m);

/* push: 새 캔들을 앞에 삽입 (idx 0 = 최신) */
void             btc_tf_push(BtcTfManager *m, BtcTimeframe tf,
                              const BtcCandle *c);

/* get: idx 0 = 최신, idx 1 = 이전, ... */
const BtcCandle *btc_tf_get(const BtcTfManager *m, BtcTimeframe tf,
                              uint32_t idx);

uint32_t         btc_tf_count(const BtcTfManager *m, BtcTimeframe tf);

/* 이동평균 볼륨 (N 캔들 평균) — 인코딩 엔진에 전달 */
uint32_t         btc_tf_avg_vol(const BtcTfManager *m, BtcTimeframe tf,
                                 uint32_t n);
