/*
 * cab_propagate_fast.h -- Sparse Active List + Fast Propagation
 * ==============================================================
 * OPT-B: Sparse active list for O(active) propagation.
 *
 * Instead of scanning all cells, maintain a list of active indices.
 * Propagation only visits active cells + their neighbors.
 *
 * DK-1: integer only. DK-2: no float/double.
 */

#ifndef CAB_PROPAGATE_FAST_H
#define CAB_PROPAGATE_FAST_H

#include "cab_propagate.h"
#include "canvas_ai_layers.h"
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  Sparse Active List
 * ================================================================ */

#define CAB_SPARSE_MAX_ACTIVE  8192

typedef struct {
    uint32_t *indices;    /* active cell indices */
    uint32_t  count;      /* current count */
    uint32_t  capacity;   /* max capacity */
} CabSparseList;

static inline int cab_sparse_init(CabSparseList *sl, uint32_t capacity) {
    if (!sl) return -1;
    sl->indices = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    if (!sl->indices) return -1;
    sl->count = 0;
    sl->capacity = capacity;
    return 0;
}

static inline void cab_sparse_free(CabSparseList *sl) {
    if (sl) {
        free(sl->indices);
        sl->indices = NULL;
        sl->count = 0;
    }
}

static inline void cab_sparse_push(CabSparseList *sl, uint32_t idx) {
    if (!sl || !sl->indices) return;
    if (sl->count < sl->capacity) {
        sl->indices[sl->count++] = idx;
    }
}

/* ================================================================
 *  Fast Propagation (sparse)
 * ================================================================
 *
 * Only visits cells in the sparse active list.
 * Much faster than full-scan cab_propagate() when active << total.
 */

static inline void cab_propagate_fast(CabActivation *act,
                                       const CabSparseList *sl,
                                       const CabCanvas *c,
                                       const CabPropConfig *cfg) {
    if (!act || !sl || !c || !cfg) return;
    if (!act->activation || !sl->indices) return;

    uint32_t total = (uint32_t)act->width * act->height;
    uint32_t cap = c->capacity;
    if (total > cap) total = cap;

    /* Temporary buffer for propagation results */
    uint32_t *temp = (uint32_t *)calloc(total, sizeof(uint32_t));
    if (!temp) return;

    for (uint8_t iter = 0; iter < cfg->max_iterations; iter++) {
        memset(temp, 0, total * sizeof(uint32_t));

        for (uint32_t si = 0; si < sl->count; si++) {
            uint32_t i = sl->indices[si];
            if (i >= total) continue;
            if (act->activation[i] == 0) continue;

            uint32_t src_energy = act->activation[i];
            const CabCell *src = &c->cells[i];

            if (src->G == 0 && src->A == 0) continue;

            uint8_t src_layer = depth_to_layer(src->B);
            uint8_t src_cluster = cab_get_cluster(src->A);

            /* Same-layer diffusion */
            for (uint16_t r = 1; r <= cfg->same_layer_radius; r++) {
                uint32_t left  = (i >= r) ? (i - r) : 0;
                uint32_t right = (i + r < total) ? (i + r) : (total - 1);

                if (left < total &&
                    c->cells[left].B == src->B &&
                    c->cells[left].G > 0) {
                    uint32_t gain = src_energy / (r * 4);
                    temp[left] += gain;
                }
                if (right < total &&
                    c->cells[right].B == src->B &&
                    c->cells[right].G > 0) {
                    uint32_t gain = src_energy / (r * 4);
                    temp[right] += gain;
                }
            }

            /* Upward/downward + cluster propagation */
            for (uint16_t r = 1; r <= 64; r++) {
                for (int dir = 0; dir < 2; dir++) {
                    uint32_t neighbor;
                    if (dir == 0) {
                        if (i + r >= total) continue;
                        neighbor = i + r;
                    } else {
                        if (i < r) continue;
                        neighbor = i - r;
                    }

                    const CabCell *nb = &c->cells[neighbor];
                    if (nb->G == 0 && nb->A == 0) continue;

                    uint8_t nb_layer = depth_to_layer(nb->B);
                    uint8_t nb_cluster = cab_get_cluster(nb->A);

                    if (nb_layer > src_layer && nb_cluster == src_cluster) {
                        uint32_t gain = (src_energy * cfg->upward_gain) >> 8;
                        temp[neighbor] += gain;
                    }
                    if (nb_layer < src_layer && nb_cluster == src_cluster) {
                        uint32_t gain = (src_energy * cfg->downward_gain) >> 8;
                        temp[neighbor] += gain;
                    }
                    if (nb_layer == src_layer && nb_cluster == src_cluster) {
                        uint32_t gain = (src_energy * cfg->cluster_gain) >> 8;
                        temp[neighbor] += gain;
                    }
                }
            }
        }

        /* Merge propagation results */
        for (uint32_t i = 0; i < total; i++) {
            act->activation[i] += temp[i];
        }
    }

    free(temp);
}

#endif /* CAB_PROPAGATE_FAST_H */
