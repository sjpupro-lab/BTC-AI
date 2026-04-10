/*
 * main_realtime_full.c — BTC-AI 전체 앙상블 실시간 루프
 * =======================================================
 * TF: 30m / 1h / 1d / 1w  |  30초 주기 갱신
 * Canvas AI + BT-BranchTuring [+ SJ-CANVAOS BtCanvas]
 * WH/BH 자동 로깅 · Timewarp --replay 모드
 *
 * 빌드:
 *   make btc_full_realtime              (SJ-CANVAOS 없이)
 *   make btc_full_realtime_canvaos      (SJ-CANVAOS 포함)
 *
 * 실행:
 *   ./btc_full_realtime
 *   ./btc_full_realtime --replay 2026-04-01 3      (TF_1H=3)
 *
 * DK-1: float/double 0건.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core/btc_types.h"
#include "core/btc_signal.h"
#include "btc_binance_realtime.h"
#include "btc_full_ensemble.h"

static const char * const TF_LABEL[REALTIME_TF_COUNT] = {
    "30m", "1h", "1d", "1w"
};

int main(int argc, char **argv)
{
    /* ── 리플레이 모드 ── */
    if (argc > 1 && strcmp(argv[1], "--replay") == 0) {
        const char  *date = (argc > 2) ? argv[2] : "2026-04-01";
        BtcTimeframe tf   = (argc > 3) ? (BtcTimeframe)atoi(argv[3]) : TF_1H;

        btc_full_ensemble_init();
        return btc_replay_from_log(date, tf);
    }

    /* ── 실시간 루프 초기화 ── */
    RealtimeTfBuffer buffers[REALTIME_TF_COUNT];
    btc_realtime_init(buffers);
    btc_full_ensemble_init();

    printf("🌌 BTC-AI FULL ENSEMBLE 실시간 시작\n");
    printf("   TF: 30m / 1h / 1d / 1w  |  갱신 주기: 30초\n");
    printf("   엔진: Canvas AI + BT-BranchTuring");
#ifdef BTC_CANVAOS_ENABLED
    printf(" + SJ-CANVAOS BtCanvas");
#endif
    printf("\n   로깅: WH 이벤트 CSV + BH 압축 자동\n\n");

    /* ── 메인 루프 ── */
    while (1) {
        btc_realtime_update_all(buffers);

        for (int i = 0; i < REALTIME_TF_COUNT; i++) {
            RealtimeTfBuffer *b = &buffers[i];
            if (b->count < 10u) {
                printf("[%s] 데이터 부족 (%u 캔들)\n",
                       TF_LABEL[i], b->count);
                continue;
            }

            FullEnsembleResult ens = btc_full_ensemble_predict(
                b->candles, b->count, b->tf);

            /* WH 이벤트 로깅 + BH 압축 */
            btc_wh_bh_log_tick(
                &b->candles[b->count - 1u], b->tf, &ens);

            /* 신뢰도: confidence_pct / 10 = 정수 %, ×0.1% 단위 */
            printf("[%s] ENSEMBLE %3d | %s | Conf %3u.%u%% | %s\n",
                   TF_LABEL[i],
                   ens.total_score,
                   ens.direction ==  1 ? "LONG  ↑" :
                   ens.direction == -1 ? "SHORT ↓" : "HOLD  –",
                   ens.confidence_pct / 10u,
                   ens.confidence_pct % 10u,
                   ens.reason);
        }

        printf("⏳ 30초 후 다음 업데이트...\n\n");
        sleep(30);
    }

    btc_realtime_free(buffers);
    return 0;
}
