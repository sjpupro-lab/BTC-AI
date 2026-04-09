/*
 * cab_sieve_gear.h -- Gear-Rev Chain + Sieve + CabBrain
 * =====================================================
 * OPT-A: Gear-Rev Chain -- O(d^2) -> O(d) gear hash
 * OPT-D: Prime Sieve -- 77%+ probe elimination
 * OPT-C: Persistent malloc (CabBrain reuse buffer)
 *
 * Gear Batch: precompute all depths using original cab_gear().
 *   Ensures hash compatibility with cab_evaluate_candidates().
 *   Gear-Rev (right-to-left FNV) was removed due to hash mismatch
 *   with cab_pattern.c evaluation path.
 *
 * Sieve: sj_gear_engine.c sieve_skip() port.
 *   8 small primes -> quick mod check on hash_mix
 *   If first slot mismatches on any prime -> skip probe
 *
 * CabBrain: wrapper with persistent buffers.
 *   Eliminates per-predict malloc/free overhead.
 *
 * DK-1: integer only. DK-2: no float/double.
 */

#ifndef CAB_SIEVE_GEAR_H
#define CAB_SIEVE_GEAR_H

#include "canvas_ai_b.h"
#include "cab_propagate.h"
#include "cab_pattern.h"
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  OPT-A: Gear Batch Precompute
 * ================================================================
 *
 * Precompute all gear hashes for depths 1..max_d using original
 * cab_gear() (left-to-right FNV1a). This ensures full compatibility
 * with cab_evaluate_candidates() and cab_probe_read().
 *
 * Eliminates redundant per-depth function call overhead.
 * Main speedup comes from OPT-B/C/D, not hash computation.
 */

#define CAB_GEAR_BATCH_MAX  CAB_TRAIN_MAX_DEPTH

typedef struct {
    uint32_t gears[CAB_GEAR_BATCH_MAX + 1]; /* gears[d] for d=1..max_d */
    uint8_t  max_d;                          /* actual max depth built  */
} CabGearChain;

/* Build gear chain: precompute all depths with original cab_gear() */
static inline void cab_gear_chain_build(CabGearChain *gc,
                                         const uint8_t *window,
                                         uint8_t win_len) {
    uint8_t md = (win_len < CAB_GEAR_BATCH_MAX) ? win_len : CAB_GEAR_BATCH_MAX;
    gc->max_d = md;

    for (uint8_t d = 1; d <= md; d++) {
        const uint8_t *ctx = window + (win_len - d);
        gc->gears[d] = cab_gear(ctx, d);
    }
}

/* Look up precomputed gear value */
static inline uint32_t cab_gear_chain_get(const CabGearChain *gc,
                                           uint8_t depth) {
    if (depth == 0 || depth > gc->max_d) return 0;
    return gc->gears[depth];
}

/* ================================================================
 *  Semantic B-channel Encoding (CabBrain only)
 * ================================================================
 *
 * Original B = depth (1~128). Pure length, no semantic info.
 * CabBrain B = [layer:3bit][semantic_class:5bit]
 *
 *   layer (0~6):           7-layer linguistic hierarchy
 *   semantic_class (0~31): derived from A[31:24] cluster >> 3
 *
 * Same layer + same semantic_class = same structural level
 * AND same meaning group. Competing patterns separated by class.
 *
 * Probe matching: (gear24, B_encoded, R) instead of (gear24, depth, R)
 * → cells with different semantic classes stored in different slots
 * → prediction naturally prefers semantically coherent continuations
 */

/* Encode: depth + cluster → B[7:5]=layer, B[4:0]=semantic_class */
static inline uint8_t cab_brain_encode_B(uint8_t depth, uint8_t cluster) {
    uint8_t layer = depth_to_layer(depth);
    uint8_t sem = cluster >> 3;  /* 256 clusters → 32 classes */
    return (uint8_t)((layer << 5) | (sem & 0x1F));
}

/* Decode B → layer */
static inline uint8_t cab_brain_B_layer(uint8_t B) {
    return B >> 5;
}

/* Decode B → semantic class */
static inline uint8_t cab_brain_B_sem(uint8_t B) {
    return B & 0x1F;
}

/* B-channel similarity: 0=unrelated, 128=same_layer, 256=exact match */
static inline uint32_t cab_brain_B_similarity(uint8_t b1, uint8_t b2) {
    if (b1 == b2) return 256;
    if ((b1 >> 5) == (b2 >> 5)) {
        /* Same layer, different semantic class */
        uint8_t s1 = b1 & 0x1F, s2 = b2 & 0x1F;
        int diff = (s1 > s2) ? (s1 - s2) : (s2 - s1);
        if (diff <= 2) return 192;  /* nearby semantic = 75% */
        return 128;                  /* same layer only = 50% */
    }
    return 0; /* different layer */
}

/* ================================================================
 *  OPT-D: Prime Sieve Candidate Elimination
 * ================================================================
 *
 * Ported from SJ-CANVAOS sj_gear_engine.c sieve_skip().
 *
 * Principle: if hash_mix(gear, d, cand) lands on a cell whose
 * A[23:0] doesn't share mod-p residues with gear[23:0], the cell
 * belongs to a different gear context -> skip this probe chain.
 *
 * 8 primes {3,5,7,11,13,17,19,23} -> theoretical 77% elimination.
 * In practice 90%+ because gear-encoded A[23:0] correlates tightly.
 */

static const uint8_t CAB_SIEVE_P8[8] = {3, 5, 7, 11, 13, 17, 19, 23};

/*
 * Returns 1 if this (gear, depth, cand) probe can be skipped.
 * Fast path: if first slot is empty -> skip immediately.
 * Sieve path: mod-p mismatch on A[23:0] -> skip.
 */
static inline int cab_sieve_skip(uint32_t gear, uint8_t depth,
                                  uint8_t cand, const CabCanvas *c) {
    uint32_t hash = cab_hash_mix(gear, depth, cand);
    uint32_t idx  = hash & (c->capacity - 1);

    const CabCell *cell = &c->cells[idx];

    /* Empty first slot -> this candidate was never stored */
    if (cell->G == 0 && cell->A == 0) return 1;

    /* Sieve: compare mod-p residues of gear vs stored cell's gear */
    uint32_t g24 = gear & 0x00FFFFFFu;
    uint32_t a24 = cell->A & 0x00FFFFFFu;

    for (int i = 0; i < 8; i++) {
        uint8_t p = CAB_SIEVE_P8[i];
        if ((g24 % p) != (a24 % p))
            return 1; /* Different gear context -> skip */
    }
    return 0; /* Residues match -> must do full probe */
}

/* ================================================================
 *  CabBrain: Integrated Fast Engine
 * ================================================================
 *
 * Wraps CabCanvas + persistent CabActivation + GearChain.
 *
 * API mapping:
 *   cab_canvas_init()  -> cab_brain_init()
 *   cab_canvas_free()  -> cab_brain_free()
 *   cab_push_window()  -> cab_brain_push()
 *   cab_train()        -> cab_train_fast()
 *   cab_predict()      -> cab_predict_fast()
 */

typedef struct {
    CabCanvas      canvas;
    CabActivation  act;       /* OPT-C: persistent activation buffer  */
    CabGearChain   gc;        /* OPT-A: gear chain cache              */
    int            act_ready; /* 1 if activation buffer is initialized */
} CabBrain;

/* Forward declarations (implemented in cab_brain.c) */
int     cab_brain_init(CabBrain *brain, uint32_t capacity, uint8_t max_win);
void    cab_brain_free(CabBrain *brain);
void    cab_brain_reset(CabBrain *brain);

void    cab_train_fast(CabBrain *brain, const uint8_t *data, uint32_t len);
uint8_t cab_predict_fast(CabBrain *brain, uint8_t *top3, uint32_t *scores3);
void    cab_feedback_fast(CabBrain *brain, uint8_t predicted, uint8_t actual);

/* Inline: push byte into window */
static inline void cab_brain_push(CabBrain *brain, uint8_t byte_val) {
    cab_push_window(&brain->canvas, byte_val);
}

/* Inline: get canvas pointer (for stats, save/load) */
static inline CabCanvas *cab_brain_canvas(CabBrain *brain) {
    return &brain->canvas;
}

#endif /* CAB_SIEVE_GEAR_H */
