/*
 * cab_pattern.h — 패턴 인식 엔진 API
 * ====================================
 * 후보 평가: 정확 매칭 + 부분 매칭(backoff) + 패턴 유사도
 *
 * 평가 방식:
 *   A: 정확 매칭 — 기존 probe_read 방식
 *   B: 부분 매칭 — 긴 문맥에서 못 찾으면 짧은 문맥으로 backoff
 *   C: 패턴 유사도 — 전파된 활성화 기반 보너스
 *   합산: scores = exact + backoff + pattern
 */

#ifndef CAB_PATTERN_H
#define CAB_PATTERN_H

#include "canvas_ai_b.h"
#include "cab_propagate.h"

/* ── 후보 평가 결과 ────────────────────────────── */
typedef struct {
    uint32_t scores[256];          /* 각 후보의 총 score */
    uint32_t exact_scores[256];    /* 정확 매칭 score */
    uint32_t backoff_scores[256];  /* 부분 매칭 score (짧은 문맥) */
    uint32_t pattern_scores[256];  /* 패턴 유사도 score */
} CabCandidateScores;

/* ── API ───────────────────────────────────────── */

/*
 * 후보 평가: 정확 매칭 + 부분 매칭(backoff) + 패턴 유사도
 *
 * out: 결과 저장
 * c: 캔버스 (학습된 상태)
 * act: 활성화 버퍼 (전파 후 상태, NULL이면 패턴 매칭 스킵)
 * window: 현재 문맥
 * win_len: 문맥 길이
 */
void cab_evaluate_candidates(CabCandidateScores *out,
                             const CabCanvas *c,
                             const CabActivation *act,
                             const uint8_t *window, uint8_t win_len);

/*
 * 최종 예측: scores에서 top-3 선택
 *
 * 반환: top-1 바이트 값
 * top3: top-3 바이트 값 (배열[3])
 * scores3: top-3 점수 (배열[3])
 */
uint8_t cab_select_top(const CabCandidateScores *scores,
                       uint8_t *top3, uint32_t *scores3);

#endif /* CAB_PATTERN_H */
