/*
 * cab_propagate.c — 밝기 전파 엔진
 * ==================================
 * 1단계: 점등 (activate) — 현재 문맥 셀들을 활성화 버퍼에 기록
 * 2단계: 전파 (propagate) — 활성화 버퍼 내에서 밝기 확산
 *
 * 전파 방식:
 *   - 동일 계층 확산: 같은 depth, 인접 probe 범위 내
 *   - 상위 계층 전파: 낮은 depth → 높은 depth (추상화)
 *   - 하위 계층 전파: 높은 depth → 낮은 depth (세부 재활성화)
 *   - 의미 확산: 같은 A[31:24] 클러스터 → 약한 밝기 전달
 *
 * DK-1 준수: 정수 연산만 사용.
 * DK-2 준수: float/double 0건.
 */

#include "cab_propagate.h"
#include "cab_sieve_gear.h"   /* cab_brain_encode_B */
#include <stdlib.h>
#include <string.h>

/* ══════════════════════════════════════════════════
 *  활성화 버퍼 관리
 * ══════════════════════════════════════════════════ */

int cab_activation_init(CabActivation *act, uint16_t w, uint16_t h) {
    if (!act) return -1;
    uint32_t total = (uint32_t)w * h;
    act->activation = (uint32_t *)calloc(total, sizeof(uint32_t));
    if (!act->activation) return -1;
    act->width = w;
    act->height = h;
    return 0;
}

void cab_activation_free(CabActivation *act) {
    if (act) {
        free(act->activation);
        act->activation = NULL;
    }
}

void cab_activation_clear(CabActivation *act) {
    if (act && act->activation) {
        uint32_t total = (uint32_t)act->width * act->height;
        memset(act->activation, 0, total * sizeof(uint32_t));
    }
}

uint32_t cab_activation_count(const CabActivation *act) {
    if (!act || !act->activation) return 0;
    uint32_t total = (uint32_t)act->width * act->height;
    uint32_t count = 0;
    for (uint32_t i = 0; i < total; i++) {
        if (act->activation[i] > 0) count++;
    }
    return count;
}

/* ══════════════════════════════════════════════════
 *  1단계: 점등 (activate)
 * ══════════════════════════════════════════════════ */

void cab_activate_context(CabActivation *act, const CabCanvas *c,
                          const uint8_t *window, uint8_t win_len) {
    if (!act || !c || !window || win_len == 0) return;

    cab_activation_clear(act);

    uint32_t total = (uint32_t)act->width * act->height;
    uint8_t max_d = (win_len < CAB_MAX_DEPTH) ? win_len : CAB_MAX_DEPTH;

    for (uint8_t d = 1; d <= max_d; d++) {
        const uint8_t *ctx = window + (win_len - d);
        uint32_t gear = cab_gear(ctx, d);

        /* sem_B 계산 (Option B: B = [layer:3][sem:5]) */
        uint8_t cluster  = (uint8_t)(cab_encode_A(gear, ctx, d) >> 24);
        uint8_t sem_B    = cab_brain_encode_B(d, cluster);

        /* 256 후보 전부 점등 (있는 것만) */
        for (int cand = 0; cand < 256; cand++) {
            uint32_t idx = cab_probe_read(c, gear, sem_B, (uint8_t)cand);
            if (idx == UINT32_MAX) continue;
            if (idx >= total) continue;

            /* 기존 셀의 G × 계층 가중치를 활성화 버퍼에 기록 */
            uint32_t weight = (uint32_t)layer_weight(d);
            uint32_t energy = (uint32_t)c->cells[idx].G * weight;
            act->activation[idx] += energy;
        }
    }
}

/* ══════════════════════════════════════════════════
 *  2단계: 전파 (propagate)
 * ══════════════════════════════════════════════════ */

void cab_propagate(CabActivation *act, const CabCanvas *c,
                   const CabPropConfig *cfg) {
    if (!act || !c || !cfg) return;
    if (!act->activation) return;

    uint32_t total = (uint32_t)act->width * act->height;
    uint32_t cap = c->capacity;
    if (total > cap) total = cap;

    /* 임시 버퍼 (전파 결과를 여기에 쌓고, 끝나면 원본에 합산) */
    uint32_t *temp = (uint32_t *)calloc(total, sizeof(uint32_t));
    if (!temp) return;

    for (uint8_t iter = 0; iter < cfg->max_iterations; iter++) {

        memset(temp, 0, total * sizeof(uint32_t));

        for (uint32_t i = 0; i < total; i++) {
            if (act->activation[i] == 0) continue;

            uint32_t src_energy = act->activation[i];
            const CabCell *src = &c->cells[i];

            /* 빈 셀이면 스킵 */
            if (src->G == 0 && src->A == 0) continue;

            uint8_t src_layer = depth_to_layer(src->B);
            uint8_t src_cluster = cab_get_cluster(src->A);

            /* === 동일 계층 확산 === */
            for (uint16_t r = 1; r <= cfg->same_layer_radius; r++) {
                uint32_t left  = (i >= r) ? (i - r) : 0;
                uint32_t right = (i + r < total) ? (i + r) : (total - 1);

                /* 왼쪽: 같은 계층이면 전파 */
                if (left < total &&
                    c->cells[left].B == src->B &&
                    c->cells[left].G > 0) {
                    uint32_t gain = src_energy / (r * 4);  /* 거리 반비례 감쇠 */
                    temp[left] += gain;
                }

                /* 오른쪽: 같은 계층이면 전파 */
                if (right < total &&
                    c->cells[right].B == src->B &&
                    c->cells[right].G > 0) {
                    uint32_t gain = src_energy / (r * 4);
                    temp[right] += gain;
                }
            }

            /* === 상위/하위 계층 전파 + 의미 확산 === */
            /* probe 범위(64) 내에서 양방향 탐색 */
            for (uint16_t r = 1; r <= 64; r++) {
                /* 양방향: forward (+r) 와 backward (-r) */
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

                    /* 상위 계층이고 같은 클러스터 */
                    if (nb_layer > src_layer && nb_cluster == src_cluster) {
                        uint32_t gain = (src_energy * cfg->upward_gain) >> 8;
                        temp[neighbor] += gain;
                    }

                    /* 하위 계층이고 같은 클러스터 */
                    if (nb_layer < src_layer && nb_cluster == src_cluster) {
                        uint32_t gain = (src_energy * cfg->downward_gain) >> 8;
                        temp[neighbor] += gain;
                    }

                    /* 같은 클러스터, 같은 계층 (의미 확산) */
                    if (nb_layer == src_layer && nb_cluster == src_cluster) {
                        uint32_t gain = (src_energy * cfg->cluster_gain) >> 8;
                        temp[neighbor] += gain;
                    }
                }
            }
        }

        /* 전파 결과를 활성화 버퍼에 합산 */
        for (uint32_t i = 0; i < total; i++) {
            act->activation[i] += temp[i];
        }
    }

    free(temp);
}
