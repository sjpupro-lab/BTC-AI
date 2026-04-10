/*
 * btc_full_ensemble.h — BTC-AI + SJ-CANVAOS 완전 통합 앙상블
 * ==============================================================
 * Canvas AI (BTC-AI)   → 60% 기여 (score 0~60)
 * BT BranchTuring      → 20% 기여 (score 0~20) — BTC-AI btc_branch
 * SJ-CANVAOS BtCanvas  → 20% 기여 (score 0~20) — Q16.16 정수 확률
 *   └─ SJ-CANVAOS 비활성 시 BranchTuring이 40% 전부 담당
 *
 * WH (White Hole): 틱마다 CSV 이벤트 로깅 + 주기적 bt_stream_save
 * BH (Black Hole):  주기적 btc_compress() 로 캔버스 압축
 * Timewarp: WH 이벤트 CSV 탐색 + bt_stream_load + BTC-AI 타임워프
 *
 * SJ-CANVAOS 활성화: -DBTC_CANVAOS_ENABLED 컴파일 플래그
 *   (git submodule update --init --recursive 실행 후 사용)
 *
 * DK-1/DK-2: float/double 0건. 정수 전용.
 *   SJ-CANVAOS Q16.16 확률(0~65536) → 정수 산술로 변환
 */
#ifndef BTC_FULL_ENSEMBLE_H
#define BTC_FULL_ENSEMBLE_H

#include "core/btc_types.h"
#include "core/btc_canvas_brain.h"

/* ── SJ-CANVAOS 선택적 통합 ───────────────────────────
 * 서브모듈 초기화 후 -DBTC_CANVAOS_ENABLED 로 빌드하면
 * bt_canvas.h / bt_stream.h / bt_delta.h 가 활성화된다.
 * 이 헤더들은 <stdint.h> 만 의존하며 BTC-AI 타입과 충돌 없음.
 * ─────────────────────────────────────────────────── */
#ifdef BTC_CANVAOS_ENABLED
#  include "sj-canvaos/include/bt_canvas.h"
#  include "sj-canvaos/include/bt_stream.h"
#  include "sj-canvaos/include/bt_delta.h"
#endif

/* ── 앙상블 결과 ─────────────────────────────────── */
typedef struct {
    int32_t  total_score;       /* 0~100 (합산) */
    int32_t  direction;         /* 1=LONG, -1=SHORT, 0=HOLD */
    uint32_t confidence_pct;    /* 0~1000 (×0.1 % 표현, DK-1) */
    int32_t  canvas_ai_score;   /* 0~60  (BTC-AI Canvas AI 기여) */
    int32_t  bt_branch_score;   /* 0~40  (BT BranchTuring + SJ 기여) */
    char     reason[128];
} FullEnsembleResult;

/* ── API ─────────────────────────────────────────── */

/*
 * 초기화 (한 번만 호출)
 * - BTC-AI Canvas Brain init
 * - SJ-CANVAOS BtCanvas malloc + btc_init  (CANVAOS_ENABLED 시)
 * - WH 로그 디렉토리 생성
 */
void btc_full_ensemble_init(void);

/*
 * 실시간 예측 (멀티 TF용)
 * - 새 캔들 증분 학습 (g_tf_last_ts 기반)
 * - Canvas AI + BT BranchTuring + SJ-CANVAOS 앙상블
 */
FullEnsembleResult btc_full_ensemble_predict(
    const BtcCandle *candles, uint32_t count, BtcTimeframe tf);

/*
 * WH 이벤트 CSV 로깅 + 주기적 WH 스트림 저장 + BH 압축
 * (실시간 루프에서 매 틱 호출)
 */
void btc_wh_bh_log_tick(
    const BtcCandle *latest, BtcTimeframe tf,
    const FullEnsembleResult *result);

/*
 * Timewarp 리플레이 모드
 * date_str: "YYYY-MM-DD" 형식
 * WH 이벤트 CSV 탐색 → 근접 틱 발견 → 상태 복원
 */
int btc_replay_from_log(const char *date_str, BtcTimeframe tf);

#endif /* BTC_FULL_ENSEMBLE_H */
