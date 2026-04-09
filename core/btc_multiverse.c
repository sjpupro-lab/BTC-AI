/*
 * btc_multiverse.c — Multiverse Inference Engine 구현
 * Seed → Propagate → Cross-Boost → Trace
 * DK-1: float/double 0건. 정수 전용.
 * Phase 8.
 */
#include "btc_multiverse.h"
#include "btc_encoding.h"
#include <string.h>

/* ── 가격 방향 임계값 (v6f 인코딩 기준) ──────── */
#define MV_PRICE_UP     140
#define MV_PRICE_DOWN   114

/* ── MvSky 초기화 ────────────────────────────── */

void btc_mv_sky_init(MvSky *sky) {
    memset(sky, 0, sizeof(MvSky));
}

/* ── 캔버스 수집: 주어진 컨텍스트에서 256바이트 에너지 조회 ── */

static void mv_canvas_collect(const CabCanvas *canvas,
                              const uint8_t *ctx, uint8_t ctx_len,
                              uint64_t out[256]) {
    uint32_t gear = cab_gear(ctx, ctx_len);
    uint8_t cluster = (uint8_t)(cab_encode_A(gear, ctx, ctx_len) >> 24);
    uint8_t sem_B = cab_brain_encode_B(ctx_len, cluster);

    for (uint32_t b = 0; b < 256; b++) {
        uint32_t idx = cab_probe_read(canvas, gear, sem_B, (uint8_t)b);
        if (idx != UINT32_MAX) {
            out[b] = (uint64_t)canvas->cells[idx].G;
        }
    }
}

/* ── SEED: 모든 깊이에서 캔버스 조회, depth³ 가중 ── */

void btc_mv_seed(MvSky *sky, const CabCanvas *canvas,
                 const uint8_t *stream, uint32_t stream_len) {
    if (stream_len == 0) return;

    uint32_t max_d = stream_len;
    if (max_d > MV_MAX_DEPTH) max_d = MV_MAX_DEPTH;

    for (uint32_t d = 1; d <= max_d; d++) {
        const uint8_t *ctx = stream + stream_len - d;
        uint64_t collected[256];
        memset(collected, 0, sizeof(collected));

        mv_canvas_collect(canvas, ctx, (uint8_t)d, collected);

        /* depth³ 가중 에너지 */
        uint64_t d3 = (uint64_t)d * d * d;
        uint32_t depth_active = 0;

        for (uint32_t b = 0; b < 256; b++) {
            if (collected[b] > 0) {
                sky->sky[d - 1][b] += collected[b] * d3;
                depth_active = 1;
                sky->total_stars++;
            }
        }

        if (depth_active) sky->active_depths++;
    }
}

/* ── PROPAGATE: top-K 바이트로 재귀 분기 (branch-of-branch) ── */

void btc_mv_propagate(MvSky *sky, const CabCanvas *canvas,
                      const uint8_t *stream, uint32_t stream_len) {
    if (stream_len == 0) return;

    /* 확장 스트림 버퍼 (원본 + 추가 바이트) */
    uint8_t ext_buf[MV_MAX_DEPTH + MV_MAX_HOPS + 1];
    uint32_t base_len = stream_len;
    if (base_len > MV_MAX_DEPTH) base_len = MV_MAX_DEPTH;

    memcpy(ext_buf, stream + stream_len - base_len, base_len);

    for (uint32_t hop = 0; hop < MV_MAX_HOPS; hop++) {
        /* 모든 깊이에서 상위 K개 바이트 수집 */
        uint8_t  top_bytes[MV_TOP_K];
        uint64_t top_energies[MV_TOP_K];
        memset(top_energies, 0, sizeof(top_energies));

        for (uint32_t d = 0; d < MV_MAX_DEPTH; d++) {
            for (uint32_t b = 0; b < 256; b++) {
                uint64_t e = sky->sky[d][b];
                if (e == 0) continue;

                /* 삽입 정렬로 top-K 유지 */
                for (uint32_t k = 0; k < MV_TOP_K; k++) {
                    if (e > top_energies[k]) {
                        /* 아래로 밀기 */
                        for (uint32_t m = MV_TOP_K - 1; m > k; m--) {
                            top_bytes[m] = top_bytes[m - 1];
                            top_energies[m] = top_energies[m - 1];
                        }
                        top_bytes[k] = (uint8_t)b;
                        top_energies[k] = e;
                        break;
                    }
                }
            }
        }

        /* 추가된 에너지가 없으면 종료 */
        uint32_t added = 0;

        /* 각 top 바이트로 스트림 확장 후 재조회 */
        for (uint32_t k = 0; k < MV_TOP_K; k++) {
            if (top_energies[k] == 0) break;

            uint8_t tb = top_bytes[k];
            uint32_t ext_len = base_len + hop + 1;
            if (ext_len > sizeof(ext_buf)) break;

            ext_buf[base_len + hop] = tb;

            /* 확장된 스트림으로 캔버스 재조회 */
            uint32_t max_d = ext_len;
            if (max_d > MV_MAX_DEPTH) max_d = MV_MAX_DEPTH;

            for (uint32_t d = 1; d <= max_d; d++) {
                const uint8_t *ctx = ext_buf + ext_len - d;
                uint64_t collected[256];
                memset(collected, 0, sizeof(collected));

                mv_canvas_collect(canvas, ctx, (uint8_t)d, collected);

                /* 감쇠: (7/8)^(hop+1) */
                for (uint32_t b = 0; b < 256; b++) {
                    if (collected[b] == 0) continue;

                    uint64_t energy = collected[b];
                    for (uint32_t h = 0; h <= hop; h++) {
                        energy = energy * MV_DECAY_NUM / MV_DECAY_DEN;
                    }

                    if (energy > 0) {
                        sky->sky[d - 1][b] += energy;
                        sky->total_stars++;
                        added++;
                    }
                }
            }
        }

        if (added == 0) break; /* 수렴: 더 이상 새 에너지 없음 */
    }
}

/* ── CROSS-BOOST: BH+WH 동시 활성 바이트 증폭 ── */

void btc_mv_cross_boost(MvSky *sky) {
    for (uint32_t b = 0; b < 256; b++) {
        uint32_t bh_active = 0;  /* d < MV_BH_DEPTH 활성 */
        uint32_t wh_active = 0;  /* d >= MV_BH_DEPTH 활성 */
        uint64_t bh_sum = 0;
        uint64_t wh_sum = 0;
        uint32_t n_active = 0;

        for (uint32_t d = 0; d < MV_MAX_DEPTH; d++) {
            if (sky->sky[d][b] > 0) {
                n_active++;
                if (d < MV_BH_DEPTH) {
                    bh_active = 1;
                    bh_sum += sky->sky[d][b];
                } else {
                    wh_active = 1;
                    wh_sum += sky->sky[d][b];
                }
            }
        }

        if (n_active < 2) continue;

        /* 교차 부스트: n_active * avg_energy / 8 */
        uint64_t total_sum = bh_sum + wh_sum;
        uint64_t avg = total_sum / n_active;
        uint64_t boost = n_active * avg / 8;

        /* BH+WH 동시 활성 → 웜홀: ×3 증폭 */
        if (bh_active && wh_active) {
            boost *= MV_CROSS_ALIGN;
            sky->crossings++;
        }

        /* 부스트를 모든 활성 깊이에 배분 */
        for (uint32_t d = 0; d < MV_MAX_DEPTH; d++) {
            if (sky->sky[d][b] > 0) {
                sky->sky[d][b] += boost;
            }
        }
    }
}

/* ── TRACE: 최종 밝기 집계 및 최고 바이트 선택 ── */

uint8_t btc_mv_trace(const MvSky *sky, uint64_t brightness[256]) {
    memset(brightness, 0, 256 * sizeof(uint64_t));

    for (uint32_t b = 0; b < 256; b++) {
        uint64_t bh_e = 0; /* byte-level (d < MV_BH_DEPTH) */
        uint64_t wh_e = 0; /* word-level (d >= MV_BH_DEPTH) */

        for (uint32_t d = 0; d < MV_MAX_DEPTH; d++) {
            if (d < MV_BH_DEPTH)
                bh_e += sky->sky[d][b];
            else
                wh_e += sky->sky[d][b];
        }

        /* word-level 에너지 ×2 (장기 패턴 우선) */
        brightness[b] = bh_e + wh_e * 2;
    }

    /* 최고 밝기 바이트 선택 */
    uint8_t best = 0;
    uint64_t best_e = 0;
    for (uint32_t b = 0; b < 256; b++) {
        if (brightness[b] > best_e) {
            best_e = brightness[b];
            best = (uint8_t)b;
        }
    }
    return best;
}

/* ── btc_mv_predict: 전체 멀티버스 추론 파이프라인 ── */

void btc_mv_predict(MvPrediction *out,
                    BtcCanvasBrain *brain,
                    BtcTimeframe tf,
                    const BtcCandle *candles,
                    uint32_t count) {
    if (!out || !brain || !candles) {
        if (out) memset(out, 0, sizeof(MvPrediction));
        return;
    }
    memset(out, 0, sizeof(MvPrediction));

    if (count < 2) return;

    /* 1. 캔들 → 바이트 스트림 인코딩 */
    uint32_t use_count = count;
    if (use_count > MV_MAX_DEPTH + 1) use_count = MV_MAX_DEPTH + 1;

    uint8_t stream_buf[MV_MAX_DEPTH * 4];
    uint32_t stream_len = 0;

    /* 평균 볼륨 계산 */
    uint32_t vol_sum = 0;
    for (uint32_t i = 0; i < use_count; i++) {
        vol_sum += candles[i].volume_x10;
    }
    uint32_t avg_vol = BTC_DIV_SAFE(vol_sum, use_count);
    if (avg_vol == 0) avg_vol = 800;

    const BtcCandle *start = candles + (count - use_count);
    for (uint32_t i = 1; i < use_count; i++) {
        BtcCandleBytes cb;
        btc_candle_encode(&cb, &start[i], &start[i - 1], avg_vol);
        if (stream_len + 4 <= sizeof(stream_buf)) {
            stream_buf[stream_len + 0] = cb.price;
            stream_buf[stream_len + 1] = cb.volume;
            stream_buf[stream_len + 2] = cb.body;
            stream_buf[stream_len + 3] = cb.upper_wick;
            stream_len += 4;
        }
    }

    if (stream_len == 0) return;

    /* 2. 캔버스 접근 */
    CabCanvas *canvas = cab_brain_canvas(&brain->brain);

    /* 3. 멀티버스 파이프라인: SEED → PROPAGATE → CROSS-BOOST → TRACE */
    MvSky sky;
    btc_mv_sky_init(&sky);

    btc_mv_seed(&sky, canvas, stream_buf, stream_len);
    btc_mv_propagate(&sky, canvas, stream_buf, stream_len);
    btc_mv_cross_boost(&sky);

    uint64_t brightness[256];
    uint8_t predicted = btc_mv_trace(&sky, brightness);

    /* 4. 결과 채우기 */
    out->predicted_byte = predicted;
    out->energy = brightness[predicted];
    out->active_depths = sky.active_depths;

    /* top-3 수집 */
    for (uint32_t i = 0; i < 3; i++) {
        out->top3_bytes[i] = 0;
        out->top3_energies[i] = 0;
    }
    for (uint32_t b = 0; b < 256; b++) {
        for (uint32_t k = 0; k < 3; k++) {
            if (brightness[b] > out->top3_energies[k]) {
                for (uint32_t m = 2; m > k; m--) {
                    out->top3_bytes[m] = out->top3_bytes[m - 1];
                    out->top3_energies[m] = out->top3_energies[m - 1];
                }
                out->top3_bytes[k] = (uint8_t)b;
                out->top3_energies[k] = brightness[b];
                break;
            }
        }
    }

    /* 방향 결정: v6f 인코딩 기준 */
    if (predicted > MV_PRICE_UP) {
        out->direction = SIGNAL_LONG;
    } else if (predicted < MV_PRICE_DOWN) {
        out->direction = SIGNAL_SHORT;
    } else {
        out->direction = SIGNAL_HOLD;
    }

    /* 신뢰도: energy 기반 0~100 */
    uint64_t energy_sum = 0;
    for (uint32_t b = 0; b < 256; b++) {
        energy_sum += brightness[b];
    }
    if (energy_sum > 0) {
        out->confidence = (uint8_t)(out->energy * 100 / (energy_sum + 1));
    }

    (void)tf; /* 향후 TF별 분리 시 사용 */
}
