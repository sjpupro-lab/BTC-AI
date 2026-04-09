/*
 * cab_pattern.c — 계층적 패턴 인식 엔진
 * ========================================
 * 후보 평가: 계층별 패턴 수집 → 상위 포섭 → 다계층 확인 보너스
 *
 * 핵심 원리:
 *   큰 패턴(상위 계층) 안에 작은 패턴(하위 계층)이 중첩됨.
 *   "the cat " 매칭 시, "at " 매칭은 독립 증거가 아니라 포함된 패턴.
 *   → 상위 계층 매칭이 하위를 포섭(subsume)하여 하위 기여를 감쇠.
 *   → 다계층 동시 매칭은 강한 확인 신호 → 보너스.
 *
 * 평가 단계:
 *   A: 계층별 패턴 수집 — depth별 매칭을 7계층으로 분류
 *   B: 상위 포섭 — 최상위 매칭 계층이 하위를 감쇠
 *   C: 다계층 확인 보너스 — 2+ 계층 동시 매칭 시 강화
 *   D: 부분 매칭(backoff) — 계층별 backoff
 *   E: 패턴 유사도 — 전파된 활성화 기반 보너스
 *
 * DK-1 준수: 정수 연산만 사용.
 * DK-2 준수: float/double 0건.
 */

#include "cab_pattern.h"
#include "canvas_ai_layers.h"
#include "cab_sieve_gear.h"   /* cab_brain_encode_B (Option B) */
#include <string.h>

/* ══════════════════════════════════════════════════
 *  계층적 후보 평가
 * ══════════════════════════════════════════════════ */

void cab_evaluate_candidates(CabCandidateScores *out,
                             const CabCanvas *c,
                             const CabActivation *act,
                             const uint8_t *window, uint8_t win_len) {
    if (!out || !c || !window) return;

    memset(out, 0, sizeof(*out));

    uint8_t max_d = (win_len < CAB_MAX_DEPTH) ? win_len : CAB_MAX_DEPTH;
    uint32_t act_total = 0;
    if (act && act->activation) {
        act_total = (uint32_t)act->width * act->height;
    }

    for (int cand = 0; cand < 256; cand++) {

        /* ═══════════════════════════════════════════
         *  Phase A: 계층별 패턴 수집
         *  각 depth의 매칭 결과를 7계층으로 분류
         * ═══════════════════════════════════════════ */
        uint32_t layer_score[CAB_NUM_LAYERS];
        uint8_t  layer_hits[CAB_NUM_LAYERS];
        uint8_t  layer_max_depth[CAB_NUM_LAYERS];
        memset(layer_score, 0, sizeof(layer_score));
        memset(layer_hits, 0, sizeof(layer_hits));
        memset(layer_max_depth, 0, sizeof(layer_max_depth));

        for (uint8_t d = 1; d <= max_d; d++) {
            const uint8_t *ctx = window + (win_len - d);
            uint32_t gear = cab_gear(ctx, d);

            /* Option B: sem_B 계산 */
            uint8_t cluster = (uint8_t)(cab_encode_A(gear, ctx, d) >> 24);
            uint8_t sem_B   = cab_brain_encode_B(d, cluster);

            uint32_t idx = cab_probe_read(c, gear, sem_B, (uint8_t)cand);
            if (idx == UINT32_MAX) continue;

            uint8_t layer = depth_to_layer(d);
            uint32_t weight = layer_weight(d);
            uint32_t s = (uint32_t)c->cells[idx].G * weight;

            layer_score[layer] += s;
            layer_hits[layer]++;
            if (d > layer_max_depth[layer]) {
                layer_max_depth[layer] = d;
            }
        }

        /* ═══════════════════════════════════════════
         *  Phase B: 상위 포섭 (hierarchical subsumption)
         *
         *  원리: 큰 패턴 안에 작은 패턴이 중첩됨.
         *  최상위 매칭 계층을 찾고, 하위 계층의 기여를 감쇠.
         *
         *  "the cat " 매칭(Morpheme d=8) 시:
         *    Byte(" "→'s', d=1) 는 독립 증거가 아님 → 감쇠
         *    같은 Morpheme 내 짧은 매칭("t "→'s', d=2)도 이미 포함됨
         *
         *  감쇠 규칙: 계층 gap만큼 비트 시프트 (gap=1 → 1/4, gap=2 → 1/8, ...)
         * ═══════════════════════════════════════════ */
        int top_layer = -1;
        for (int L = CAB_NUM_LAYERS - 1; L >= 0; L--) {
            if (layer_hits[L] > 0) { top_layer = L; break; }
        }

        uint32_t hier_score = 0;
        if (top_layer >= 0) {
            for (int L = 0; L < CAB_NUM_LAYERS; L++) {
                if (layer_hits[L] == 0) continue;

                if (L == top_layer) {
                    /* 최상위 매칭 계층: 전체 점수 */
                    hier_score += layer_score[L];
                } else {
                    /* 하위 계층: 상위에 포섭됨 → 감쇠
                     * gap = 계층 차이 (1이면 바로 아래)
                     * 감쇠량: 1 / 2^(gap+1)
                     *   gap=1 → 1/4, gap=2 → 1/8, gap=3 → 1/16, ... */
                    uint8_t gap = (uint8_t)(top_layer - L);
                    uint8_t shift = gap + 1;
                    if (shift > 8) shift = 8;
                    hier_score += layer_score[L] >> shift;
                }
            }

            /* ═══════════════════════════════════════
             *  Phase C: 다계층 확인 보너스
             *
             *  여러 계층이 동시에 같은 후보를 매칭 → 강한 확인 신호
             *  예: Byte + Morpheme + Word 모두 's' 매칭
             *     → 3계층 확인 보너스 +25%
             *
             *  보너스: +12.5% (>>3) per 추가 계층
             * ═══════════════════════════════════════ */
            uint8_t match_layers = 0;
            for (int L = 0; L < CAB_NUM_LAYERS; L++) {
                if (layer_hits[L] > 0) match_layers++;
            }
            if (match_layers > 1) {
                hier_score += (hier_score * (uint32_t)(match_layers - 1)) >> 3;
            }
        }

        out->exact_scores[cand] = hier_score;

        /* ═══════════════════════════════════════════
         *  Phase D: 부분 매칭 (backoff)
         *  정확 매칭이 없는 경우, 짧은 문맥으로 재탐색
         * ═══════════════════════════════════════════ */
        if (out->exact_scores[cand] == 0) {
            for (int d = (int)max_d; d >= 1; d--) {
                const uint8_t *shorter = window + (win_len - d);
                uint32_t shorter_gear = cab_gear(shorter, (uint8_t)d);

                /* Option B: backoff도 sem_B 사용 */
                uint8_t bk_cluster = (uint8_t)(cab_encode_A(shorter_gear, shorter, (uint8_t)d) >> 24);
                uint8_t bk_sem_B   = cab_brain_encode_B((uint8_t)d, bk_cluster);

                uint32_t idx = cab_probe_read(c, shorter_gear, bk_sem_B, (uint8_t)cand);
                if (idx != UINT32_MAX) {
                    uint32_t weight = layer_weight((uint8_t)d);
                    out->backoff_scores[cand] += (uint32_t)c->cells[idx].G * weight / 2;
                    break;
                }
            }
        }

        /* ═══════════════════════════════════════════
         *  Phase E: 패턴 유사도 (전파된 활성화 기반)
         *
         *  해시매칭(Phase A)과 동등한 1차 신호로 격상.
         *  전파된 활성화 = 문맥 확산 + 의미 확산 정보 포함
         *  → 단순 해시 매칭보다 풍부한 패턴 정보.
         * ═══════════════════════════════════════════ */
        if (act && act->activation) {
            for (uint8_t d = 1; d <= max_d; d++) {
                const uint8_t *ctx = window + (win_len - d);
                uint32_t gear = cab_gear(ctx, d);

                /* Option B: sem_B 사용 */
                uint8_t pt_cluster = (uint8_t)(cab_encode_A(gear, ctx, d) >> 24);
                uint8_t pt_sem_B   = cab_brain_encode_B(d, pt_cluster);

                uint32_t idx = cab_probe_read(c, gear, pt_sem_B, (uint8_t)cand);
                if (idx == UINT32_MAX) continue;

                if (idx < act_total) {
                    out->pattern_scores[cand] += act->activation[idx];
                }
            }
        }

        /* ═══════════════════════════════════════════
         *  Phase F: 합산
         * ═══════════════════════════════════════════ */
        out->scores[cand] = out->exact_scores[cand]
                          + out->backoff_scores[cand]
                          + out->pattern_scores[cand];
    }
}

/* ══════════════════════════════════════════════════
 *  top-3 선택
 * ══════════════════════════════════════════════════ */

uint8_t cab_select_top(const CabCandidateScores *scores,
                       uint8_t *top3, uint32_t *scores3) {
    uint8_t  t3[3] = {0, 0, 0};
    uint32_t s3[3] = {0, 0, 0};

    if (!scores) {
        if (top3)    { top3[0] = top3[1] = top3[2] = 0; }
        if (scores3) { scores3[0] = scores3[1] = scores3[2] = 0; }
        return 0;
    }

    for (int i = 0; i < 256; i++) {
        if (scores->scores[i] > s3[0]) {
            s3[2] = s3[1]; t3[2] = t3[1];
            s3[1] = s3[0]; t3[1] = t3[0];
            s3[0] = scores->scores[i]; t3[0] = (uint8_t)i;
        } else if (scores->scores[i] > s3[1]) {
            s3[2] = s3[1]; t3[2] = t3[1];
            s3[1] = scores->scores[i]; t3[1] = (uint8_t)i;
        } else if (scores->scores[i] > s3[2]) {
            s3[2] = scores->scores[i]; t3[2] = (uint8_t)i;
        }
    }

    if (top3)    { top3[0] = t3[0]; top3[1] = t3[1]; top3[2] = t3[2]; }
    if (scores3) { scores3[0] = s3[0]; scores3[1] = s3[1]; scores3[2] = s3[2]; }
    return t3[0];
}
