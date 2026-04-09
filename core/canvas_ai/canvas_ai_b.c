/*
 * canvas_ai_b.c — Canvas AI Brain 핵심 구현
 * ==========================================
 * 해시 테이블 기반 학습/예측 + 밝기 전파/패턴 인식 통합.
 *
 * cab_predict: 전파 + 패턴 인식 (v2)
 * cab_predict_legacy: 해시 lookup만 (v1, 보존)
 * cab_train: 점등 + 누산 학습
 * cab_feedback: 예측 보정 (P4)
 */

#include "canvas_ai_b.h"
#include "cab_propagate.h"
#include "cab_pattern.h"
#include "cab_sieve_gear.h"   /* cab_brain_encode_B (Option B) */
#include <stdlib.h>
#include <string.h>

/* ══════════════════════════════════════════════════
 *  캔버스 생성 / 소멸
 * ══════════════════════════════════════════════════ */

int cab_canvas_init(CabCanvas *c, uint32_t capacity, uint8_t max_win) {
    if (!c) return -1;
    memset(c, 0, sizeof(*c));

    /* 용량은 2의 거듭제곱이어야 함 */
    uint32_t cap = 1;
    while (cap < capacity) cap <<= 1;

    c->cells = (CabCell *)calloc(cap, sizeof(CabCell));
    if (!c->cells) return -1;

    c->capacity = cap;
    c->width = 4096;
    c->height = (uint16_t)(cap / 4096);
    if (c->height == 0) c->height = 1;
    c->max_win = (max_win > 0) ? max_win : CAB_MAX_DEPTH;
    c->win_len = 0;
    c->cell_count = 0;

    return 0;
}

void cab_canvas_free(CabCanvas *c) {
    if (c) {
        free(c->cells);
        c->cells = NULL;
        c->capacity = 0;
        c->cell_count = 0;
    }
}

void cab_canvas_reset(CabCanvas *c) {
    if (c && c->cells) {
        memset(c->cells, 0, c->capacity * sizeof(CabCell));
        c->win_len = 0;
        c->cell_count = 0;
    }
}

/* 동적 리사이즈: 용량 2배 확장 + 전체 셀 재배치 */
int cab_canvas_grow(CabCanvas *c) {
    if (!c || !c->cells) return -1;

    uint32_t old_cap = c->capacity;
    uint32_t new_cap = old_cap * 2;
    if (new_cap < old_cap) return -1;   /* 오버플로우 방지 */

    CabCell *old_cells = c->cells;

    /* 새 배열 할당 */
    CabCell *new_cells = (CabCell *)calloc(new_cap, sizeof(CabCell));
    if (!new_cells) return -1;

    /* 새 배열로 교체 */
    c->cells = new_cells;
    c->capacity = new_cap;
    c->cell_count = 0;
    c->width = 4096;
    c->height = (uint16_t)(new_cap / 4096);
    if (c->height == 0) c->height = 1;

    /* 기존 셀 재배치 (cab_hash_mix는 A[23:0]만 사용하므로 재구성 가능) */
    uint32_t new_mask = new_cap - 1;
    for (uint32_t i = 0; i < old_cap; i++) {
        const CabCell *old = &old_cells[i];
        if (old->G == 0 && old->A == 0) continue;

        uint32_t gear24 = old->A & 0x00FFFFFFu;
        uint32_t hash = cab_hash_mix(gear24, old->B, old->R);

        /* 선형 탐색으로 빈 슬롯 찾기 */
        for (uint32_t step = 0; step < CAB_PROBE_LIMIT; step++) {
            uint32_t idx = (hash + step) & new_mask;
            CabCell *nc = &new_cells[idx];
            if (nc->G == 0 && nc->A == 0) {
                *nc = *old;
                c->cell_count++;
                break;
            }
        }
    }

    free(old_cells);
    return 0;
}

/* ══════════════════════════════════════════════════
 *  학습 (cab_train)
 * ══════════════════════════════════════════════════ */

void cab_train(CabCanvas *c, const uint8_t *data, uint32_t len) {
    if (!c || !data || len < 2) return;

    /* 세션 경계: 윈도우 리셋으로 이전 학습 꼬리 오염 방지 */
    c->win_len = 0;

    for (uint32_t i = 0; i + 1 < len; i++) {

        /* Push FIRST: 윈도우 상태가 예측 시점과 일치하도록.
         * 학습: window=[data[0]..data[i]], target=data[i+1]
         * 예측: window=[data[0]..data[i]], predict data[i+1]  ← 일치! */
        cab_push_window(c, data[i]);

        /* 동적 리사이즈: 매 1024바이트마다 채움률 체크.
         * 75% 초과 시 자동 2배 확장. 루프 중간에도 실행. */
        if ((i & 0x3FF) == 0 &&
            c->cell_count * CAB_GROW_THRESHOLD_DEN
            > c->capacity * CAB_GROW_THRESHOLD_NUM) {
            cab_canvas_grow(c);
        }

        /* 학습은 CAB_TRAIN_MAX_DEPTH까지만 */
        uint8_t max_d = (c->win_len < CAB_TRAIN_MAX_DEPTH)
                      ? c->win_len : CAB_TRAIN_MAX_DEPTH;

        for (uint8_t d = 1; d <= max_d; d++) {
            const uint8_t *ctx = c->window + (c->win_len - d);
            uint32_t gear = cab_gear(ctx, d);
            uint8_t next_byte = data[i + 1];

            /* Option B: sem_B 계산 */
            uint8_t cluster = (uint8_t)(cab_encode_A(gear, ctx, d) >> 24);
            uint8_t sem_B   = cab_brain_encode_B(d, cluster);

            uint32_t idx = cab_probe_write(c, gear, sem_B, next_byte);
            CabCell *cell = &c->cells[idx];

            if (cell->G == 0 && cell->A == 0) {
                cell->A = cab_encode_A(gear, ctx, d);
                cell->B = sem_B;
                cell->R = next_byte;
                c->cell_count++;
            }
            uint16_t delta = cab_train_delta(d);
            cell->G = cab_g_increment(cell->G, delta);
        }
    }
    /* 마지막 바이트 push (학습 대상이 아닌 마지막 바이트) */
    if (len > 0) {
        cab_push_window(c, data[len - 1]);
    }
}

/* ══════════════════════════════════════════════════
 *  예측 v2 (전파 + 패턴 인식)
 * ══════════════════════════════════════════════════ */

uint8_t cab_predict(CabCanvas *c, uint8_t *top3, uint32_t *scores3) {
    if (!c || c->win_len == 0) {
        if (top3) { top3[0] = top3[1] = top3[2] = 0; }
        if (scores3) { scores3[0] = scores3[1] = scores3[2] = 0; }
        return 0;
    }

    /* 1. 활성화 버퍼 생성 */
    CabActivation act;
    if (cab_activation_init(&act, c->width, c->height) != 0) {
        /* 메모리 부족 시 legacy로 폴백 */
        return cab_predict_legacy(c, top3, scores3);
    }

    /* 2. 점등: 현재 문맥의 셀들을 활성화 */
    cab_activate_context(&act, c, c->window, c->win_len);

    /* 3. 전파: 밝기 확산 */
    CabPropConfig cfg = cab_prop_default();
    cab_propagate(&act, c, &cfg);

    /* 4. 후보 평가: 정확 + 부분 + 패턴 유사도 */
    CabCandidateScores cand;
    cab_evaluate_candidates(&cand, c, &act, c->window, c->win_len);

    /* 5. 선택 */
    uint8_t result = cab_select_top(&cand, top3, scores3);

    cab_activation_free(&act);
    return result;
}

/* ══════════════════════════════════════════════════
 *  예측 legacy (해시 lookup만)
 * ══════════════════════════════════════════════════ */

uint8_t cab_predict_legacy(CabCanvas *c, uint8_t *top3, uint32_t *scores3) {
    uint8_t  t3[3]  = {0, 0, 0};
    uint32_t s3[3]  = {0, 0, 0};

    if (!c || c->win_len == 0) {
        if (top3) { top3[0] = top3[1] = top3[2] = 0; }
        if (scores3) { scores3[0] = scores3[1] = scores3[2] = 0; }
        return 0;
    }

    uint8_t max_d = (c->win_len < CAB_MAX_DEPTH) ? c->win_len : CAB_MAX_DEPTH;

    for (int cand = 0; cand < 256; cand++) {
        uint32_t score = 0;

        for (uint8_t d = 1; d <= max_d; d++) {
            const uint8_t *ctx = c->window + (c->win_len - d);
            uint32_t gear = cab_gear(ctx, d);

            /* Option B: sem_B 사용 */
            uint8_t cluster = (uint8_t)(cab_encode_A(gear, ctx, d) >> 24);
            uint8_t sem_B   = cab_brain_encode_B(d, cluster);

            uint32_t idx = cab_probe_read(c, gear, sem_B, (uint8_t)cand);
            if (idx == UINT32_MAX) continue;

            uint32_t weight = (uint32_t)layer_weight(d);
            score += (uint32_t)c->cells[idx].G * weight;
        }

        /* top-3 삽입 */
        if (score > s3[0]) {
            s3[2] = s3[1]; t3[2] = t3[1];
            s3[1] = s3[0]; t3[1] = t3[0];
            s3[0] = score;  t3[0] = (uint8_t)cand;
        } else if (score > s3[1]) {
            s3[2] = s3[1]; t3[2] = t3[1];
            s3[1] = score;  t3[1] = (uint8_t)cand;
        } else if (score > s3[2]) {
            s3[2] = score;  t3[2] = (uint8_t)cand;
        }
    }

    if (top3) { top3[0] = t3[0]; top3[1] = t3[1]; top3[2] = t3[2]; }
    if (scores3) { scores3[0] = s3[0]; scores3[1] = s3[1]; scores3[2] = s3[2]; }
    return t3[0];
}

/* ══════════════════════════════════════════════════
 *  피드백 (P4)
 * ══════════════════════════════════════════════════ */

void cab_feedback(CabCanvas *c, uint8_t predicted, uint8_t actual) {
    if (!c || c->win_len == 0) return;
    if (predicted == actual) return;  /* 정확 예측이면 보정 불필요 */

    /* 피드백도 학습 깊이 제한 적용 */
    uint8_t max_d = (c->win_len < CAB_TRAIN_MAX_DEPTH)
                  ? c->win_len : CAB_TRAIN_MAX_DEPTH;

    for (uint8_t d = 1; d <= max_d; d++) {
        const uint8_t *ctx = c->window + (c->win_len - d);
        uint32_t gear = cab_gear(ctx, d);

        /* Option B: sem_B 계산 */
        uint8_t cluster = (uint8_t)(cab_encode_A(gear, ctx, d) >> 24);
        uint8_t sem_B   = cab_brain_encode_B(d, cluster);

        /* 실제 값을 계층별 가중치로 보강 */
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

        /* 잘못 예측한 값을 약하게 감쇠 */
        uint32_t wrong_idx = cab_probe_read(c, gear, sem_B, predicted);
        if (wrong_idx != UINT32_MAX && c->cells[wrong_idx].G > 0) {
            c->cells[wrong_idx].G--;
        }
    }
}

/* ══════════════════════════════════════════════════
 *  통계 수집 (CabStats)
 * ══════════════════════════════════════════════════ */

void cab_stats(const CabCanvas *c, CabStats *out) {
    if (!c || !out) return;
    memset(out, 0, sizeof(*out));

    out->total_cells = c->cell_count;
    out->capacity = c->capacity;

    for (uint32_t i = 0; i < c->capacity; i++) {
        const CabCell *cell = &c->cells[i];
        if (cell->G == 0 && cell->A == 0) continue;

        if (cell->G > out->max_g) out->max_g = cell->G;
        if (cell->B > out->max_depth_used) out->max_depth_used = cell->B;

        uint8_t layer = depth_to_layer(cell->B);
        if (layer < CAB_NUM_LAYERS && out->layer_cell_count[layer] < 255) {
            out->layer_cell_count[layer]++;
        }
    }
}

/* ══════════════════════════════════════════════════
 *  저장 / 로드 (스텁 — 구현 예정)
 * ══════════════════════════════════════════════════ */

/* ══════════════════════════════════════════════════
 *  저장 / 로드 (CVP 기반 무손실 직렬화)
 * ══════════════════════════════════════════════════ */

#include <stdio.h>

#define CAB_SAVE_MAGIC   0x43414233u  /* "CAB3" */
#define CAB_SAVE_VERSION 3

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t capacity;
    uint32_t cell_count;
    uint16_t width;
    uint16_t height;
    uint8_t  max_win;
    uint8_t  win_len;
    uint8_t  reserved[2];
    uint32_t reserved2[2];
} CabFileHeader;  /* 32 bytes */

int cab_save(const CabCanvas *c, const char *path) {
    if (!c || !c->cells || !path) return -1;

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    CabFileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic      = CAB_SAVE_MAGIC;
    hdr.version    = CAB_SAVE_VERSION;
    hdr.capacity   = c->capacity;
    hdr.cell_count = c->cell_count;
    hdr.width      = c->width;
    hdr.height     = c->height;
    hdr.max_win    = c->max_win;
    hdr.win_len    = c->win_len;

    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return -1; }
    if (fwrite(c->cells, sizeof(CabCell), c->capacity, f) != c->capacity) { fclose(f); return -1; }
    if (fwrite(c->window, 1, c->max_win, f) != c->max_win) { fclose(f); return -1; }

    fclose(f);
    return 0;
}

int cab_load(CabCanvas *c, const char *path) {
    if (!c || !path) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    CabFileHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return -2; }
    if (hdr.magic != CAB_SAVE_MAGIC)    { fclose(f); return -3; }
    if (hdr.version != CAB_SAVE_VERSION) { fclose(f); return -4; }

    /* 기존 캔버스 해제 후 새 크기로 초기화 */
    cab_canvas_free(c);
    if (cab_canvas_init(c, hdr.capacity, hdr.max_win) != 0) { fclose(f); return -5; }

    if (fread(c->cells, sizeof(CabCell), hdr.capacity, f) != hdr.capacity) { fclose(f); return -6; }
    if (fread(c->window, 1, hdr.max_win, f) != hdr.max_win) { fclose(f); return -7; }

    c->cell_count = hdr.cell_count;
    c->win_len    = hdr.win_len;

    fclose(f);
    return 0;
}
