/*
 * cab_brain.c -- CabBrain: Fast integrated AI engine
 * ===================================================
 * Combines all optimizations:
 *   OPT-A: Gear-Rev Chain      (O(d^2) -> O(d) gear hash)
 *   OPT-B: Sparse Active List  (O(total) -> O(active) propagation)
 *   OPT-C: Persistent malloc   (eliminate per-predict allocation)
 *   OPT-D: Prime Sieve         (77%+ probe elimination)
 *
 * DK-1: integer only. DK-2: no float/double.
 */

#include "cab_sieve_gear.h"
#include "cab_propagate_fast.h"
#include "cab_pattern.h"      /* cab_evaluate_candidates, cab_select_top */
#include <stdlib.h>
#include <string.h>

/* ══════════════════════════════════════════════════
 *  Brain lifecycle
 * ══════════════════════════════════════════════════ */

int cab_brain_init(CabBrain *brain, uint32_t capacity, uint8_t max_win) {
    if (!brain) return -1;
    memset(brain, 0, sizeof(*brain));

    int rc = cab_canvas_init(&brain->canvas, capacity, max_win);
    if (rc != 0) return rc;

    /* OPT-C: Persistent activation buffer */
    rc = cab_activation_init(&brain->act,
                             brain->canvas.width,
                             brain->canvas.height);
    if (rc != 0) {
        cab_canvas_free(&brain->canvas);
        return rc;
    }
    brain->act_ready = 1;
    return 0;
}

void cab_brain_free(CabBrain *brain) {
    if (!brain) return;
    if (brain->act_ready) {
        cab_activation_free(&brain->act);
        brain->act_ready = 0;
    }
    cab_canvas_free(&brain->canvas);
}

void cab_brain_reset(CabBrain *brain) {
    if (!brain) return;
    cab_canvas_reset(&brain->canvas);
    if (brain->act_ready) {
        cab_activation_clear(&brain->act);
    }
}

/* ══════════════════════════════════════════════════
 *  Fast training (original cab_gear + OPT-C)
 * ══════════════════════════════════════════════════
 *
 * Same logic as cab_train() but with:
 *   - Persistent activation buffer management (OPT-C)
 *   - Batch gear precompute via cab_gear_chain_build
 *   - Uses original cab_gear() for full hash compatibility
 */

void cab_train_fast(CabBrain *brain, const uint8_t *data, uint32_t len) {
    if (!brain || !data || len < 2) return;
    CabCanvas *c = &brain->canvas;

    /* Session boundary: reset window */
    c->win_len = 0;

    for (uint32_t i = 0; i + 1 < len; i++) {

        /* Push FIRST: match predict-time window state */
        cab_push_window(c, data[i]);

        /* Dynamic resize: check every 1024 bytes */
        if ((i & 0x3FF) == 0 &&
            c->cell_count * CAB_GROW_THRESHOLD_DEN
            > c->capacity * CAB_GROW_THRESHOLD_NUM) {
            cab_canvas_grow(c);
            /* OPT-C: re-init activation buffer if canvas grew */
            if (brain->act_ready &&
                (uint32_t)brain->act.width * brain->act.height < c->capacity) {
                cab_activation_free(&brain->act);
                brain->act_ready = 0;
                if (cab_activation_init(&brain->act, c->width, c->height) == 0) {
                    brain->act_ready = 1;
                }
            }
        }

        /* Batch gear precompute (original cab_gear, hash compatible) */
        cab_gear_chain_build(&brain->gc, c->window, c->win_len);

        uint8_t max_d = (c->win_len < CAB_TRAIN_MAX_DEPTH)
                      ? c->win_len : CAB_TRAIN_MAX_DEPTH;

        /* 단어 경계 감지: 현재 바이트가 공백이면 다음은 단어 시작 */
        int at_word_start = (c->win_len > 0
                             && c->window[c->win_len - 1] == ' ');
        /* 문장 경계: ". " 또는 "? " 뒤 */
        int at_sent_start = (c->win_len > 1
                             && c->window[c->win_len - 1] == ' '
                             && (c->window[c->win_len - 2] == '.'
                                 || c->window[c->win_len - 2] == '?'
                                 || c->window[c->win_len - 2] == '!'));

        for (uint8_t d = 1; d <= max_d; d++) {
            uint32_t gear = cab_gear_chain_get(&brain->gc, d);
            const uint8_t *ctx = c->window + (c->win_len - d);
            uint8_t next_byte = data[i + 1];

            /* Semantic B: [layer:3][sem_class:5] */
            uint8_t cluster = (uint8_t)((cab_encode_A(gear, ctx, d) >> 24));
            uint8_t sem_B = cab_brain_encode_B(d, cluster);

            uint32_t idx = cab_probe_write(c, gear, sem_B, next_byte);
            CabCell *cell = &c->cells[idx];

            if (cell->G == 0 && cell->A == 0) {
                cell->A = cab_encode_A(gear, ctx, d);
                cell->B = sem_B;
                cell->R = next_byte;
                c->cell_count++;
            }

            /* 정교한 학습: 단어/문장 경계에서 가중 강화 */
            uint16_t delta = cab_train_delta(d);
            if (at_sent_start) delta = delta * 3;       /* 문장 시작: 3배 */
            else if (at_word_start) delta = delta * 2;   /* 단어 시작: 2배 */
            cell->G = cab_g_increment(cell->G, delta);
        }
    }
    /* Push last byte */
    if (len > 0) {
        cab_push_window(c, data[len - 1]);
    }
}

/* ══════════════════════════════════════════════════
 *  Semantic B-channel Prediction
 * ══════════════════════════════════════════════════
 *
 * Training stores B = [layer:3][sem_class:5].
 * Prediction probes with semantic B, then applies
 * B-similarity weighting: exact=100%, same_layer+near_sem=75%,
 * same_layer=50%, different_layer=0%.
 *
 * This separates competing patterns by semantic class.
 * "the rock" and "the rain" get different B values,
 * so predictions after "the rock" context prefer
 * rock-related continuations over rain-related ones.
 */

uint8_t cab_predict_fast(CabBrain *brain, uint8_t *top3, uint32_t *scores3) {
    CabCanvas *c = &brain->canvas;

    if (!c || c->win_len == 0) {
        if (top3) { top3[0] = top3[1] = top3[2] = 0; }
        if (scores3) { scores3[0] = scores3[1] = scores3[2] = 0; }
        return 0;
    }

    /* OPT-C: Reuse persistent activation buffer */
    if (!brain->act_ready) {
        return cab_predict_legacy(c, top3, scores3);
    }

    cab_activation_clear(&brain->act);

    uint8_t max_d = (c->win_len < CAB_MAX_DEPTH) ? c->win_len : CAB_MAX_DEPTH;
    uint32_t total = (uint32_t)brain->act.width * brain->act.height;

    /* OPT-B: Sparse active list */
    CabSparseList sl;
    int have_sparse = (cab_sparse_init(&sl, CAB_SPARSE_MAX_ACTIVE) == 0);

    /* ── 점등: sem_B 기반 activation (Option B 통일) ── */
    for (uint8_t d = 1; d <= max_d; d++) {
        const uint8_t *ctx = c->window + (c->win_len - d);
        uint32_t gear = cab_gear(ctx, d);

        uint8_t cluster   = (uint8_t)(cab_encode_A(gear, ctx, d) >> 24);
        uint8_t ctx_sem_B = cab_brain_encode_B(d, cluster);

        for (int cand = 0; cand < 256; cand++) {
            uint32_t idx = cab_probe_read(c, gear, ctx_sem_B, (uint8_t)cand);
            if (idx == UINT32_MAX || idx >= total) continue;

            uint32_t weight = (uint32_t)layer_weight(d);
            uint32_t energy = (uint32_t)c->cells[idx].G * weight;

            if (have_sparse && brain->act.activation[idx] == 0) {
                cab_sparse_push(&sl, idx);
            }
            brain->act.activation[idx] += energy;
        }
    }

    /* OPT-B: Sparse propagation */
    CabPropConfig cfg = cab_prop_default();
    if (have_sparse) {
        cab_propagate_fast(&brain->act, &sl, c, &cfg);
        cab_sparse_free(&sl);
    } else {
        cab_propagate(&brain->act, c, &cfg);
    }

    /* ── 패턴 평가: cab_evaluate_candidates 사용 (Option B 통일 후 호환) ──
     * B채널이 sem_B로 통일됐으므로 cab_pattern.c의 Phase A~E 전부 활용 가능.
     * subsumption(Phase B) + 다계층 보너스(Phase C) + backoff(Phase D) 포함. */
    CabCandidateScores cand_scores;
    cab_evaluate_candidates(&cand_scores, c, &brain->act, c->window, c->win_len);

    uint8_t result = cab_select_top(&cand_scores, top3, scores3);
    return result;
}

/* ══════════════════════════════════════════════════
 *  Fast feedback (gear-rev compatible)
 * ══════════════════════════════════════════════════ */

void cab_feedback_fast(CabBrain *brain, uint8_t predicted, uint8_t actual) {
    CabCanvas *c = &brain->canvas;
    if (!c || c->win_len == 0) return;
    if (predicted == actual) return;

    uint8_t max_d = (c->win_len < CAB_TRAIN_MAX_DEPTH)
                  ? c->win_len : CAB_TRAIN_MAX_DEPTH;

    for (uint8_t d = 1; d <= max_d; d++) {
        const uint8_t *ctx = c->window + (c->win_len - d);
        uint32_t gear = cab_gear(ctx, d);
        uint8_t cluster = (uint8_t)(cab_encode_A(gear, ctx, d) >> 24);
        uint8_t sem_B = cab_brain_encode_B(d, cluster);

        /* Reinforce actual value */
        uint32_t idx = cab_probe_write(c, gear, sem_B, actual);
        CabCell *cell = &c->cells[idx];
        if (cell->G == 0 && cell->A == 0) {
            cell->A = cab_encode_A(gear, ctx, d);
            cell->B = sem_B;
            cell->R = actual;
            c->cell_count++;
        }
        uint16_t delta = cab_train_delta(d);
        cell->G = cab_g_increment(cell->G, delta);

        /* Decay wrong prediction */
        uint32_t wrong_idx = cab_probe_read(c, gear, sem_B, predicted);
        if (wrong_idx != UINT32_MAX && c->cells[wrong_idx].G > 0) {
            c->cells[wrong_idx].G--;
        }
    }
}
