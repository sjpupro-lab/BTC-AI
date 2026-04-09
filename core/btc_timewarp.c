/*
 * btc_timewarp.c — BH/WH 시간 압축 + 타임워프 엔진 구현
 * Black Hole: IDLE/LOOP/BURST 패턴 압축
 * White Hole: 패턴 재생 (Canvas AI 재학습)
 * TimeWarp: 시간 점프 + 복귀
 * DK-1: float/double 0건. 정수 전용.
 * Phase 8.
 */
#include "btc_timewarp.h"
#include "btc_encoding.h"
#include <string.h>

/* ── 내부: FNV-1a 해시 ───────────────────────── */

static uint32_t bh_fnv1a_u32(uint32_t val, uint32_t hash) {
    hash ^= val & 0xFFu;
    hash *= BH_FNV_PRIME;
    hash ^= (val >> 8) & 0xFFu;
    hash *= BH_FNV_PRIME;
    hash ^= (val >> 16) & 0xFFu;
    hash *= BH_FNV_PRIME;
    hash ^= (val >> 24) & 0xFFu;
    hash *= BH_FNV_PRIME;
    return hash;
}

/* ── 내부: IDLE 탐지 ─────────────────────────── */
/*
 * 연속 캔들들의 close 가격 변동이 threshold 이내인 구간 탐지.
 * 기준: max(close) - min(close) < BH_IDLE_THRESHOLD
 */
static int bh_detect_idle(BhSummary *out,
                          const BtcCandle *candles,
                          uint32_t from, uint32_t to) {
    if (to - from + 1 < BH_IDLE_MIN_CANDLES) return 0;

    uint32_t min_close = candles[from].close_x100;
    uint32_t max_close = candles[from].close_x100;

    for (uint32_t i = from + 1; i <= to; i++) {
        uint32_t c = candles[i].close_x100;
        if (c < min_close) min_close = c;
        if (c > max_close) max_close = c;
    }

    uint32_t range = max_close - min_close;
    if (range < BH_IDLE_THRESHOLD) {
        out->rule = BH_RULE_IDLE;
        out->from_idx = from;
        out->to_idx = to;
        out->count = to - from + 1;
        out->stride = 0;
        out->pattern_hash = bh_fnv1a_u32(range, BH_FNV_OFFSET);
        return 1;
    }
    return 0;
}

/* ── 내부: LOOP 탐지 ─────────────────────────── */
/*
 * 가격 변화 방향 패턴의 반복을 탐지.
 * close[i] > close[i-1] → 1, < → 2, == → 0 으로 인코딩 후
 * 주기 P에서 K회 이상 반복 확인.
 */
static int bh_detect_loop(BhSummary *out,
                          const BtcCandle *candles,
                          uint32_t from, uint32_t to) {
    uint32_t len = to - from + 1;
    if (len < BH_LOOP_MIN_REPEAT * 2) return 0;

    /* 방향 시퀀스 생성 */
    uint8_t dirs[256];
    uint32_t dir_len = 0;
    for (uint32_t i = from + 1; i <= to && dir_len < 256; i++) {
        if (candles[i].close_x100 > candles[i - 1].close_x100)
            dirs[dir_len++] = 1;
        else if (candles[i].close_x100 < candles[i - 1].close_x100)
            dirs[dir_len++] = 2;
        else
            dirs[dir_len++] = 0;
    }

    /* 주기 P = 2 ~ BH_LOOP_MAX_PERIOD에서 반복 탐지 */
    uint32_t best_period = 0;
    uint32_t best_repeats = 0;
    uint32_t best_hash = BH_FNV_OFFSET;

    for (uint32_t p = 2; p <= BH_LOOP_MAX_PERIOD && p <= dir_len / 2; p++) {
        /* 첫 P개 방향의 해시 */
        uint32_t pattern_hash = BH_FNV_OFFSET;
        for (uint32_t i = 0; i < p; i++) {
            pattern_hash ^= dirs[i];
            pattern_hash *= BH_FNV_PRIME;
        }

        /* 이후 P개씩 비교 */
        uint32_t repeats = 1;
        for (uint32_t off = p; off + p <= dir_len; off += p) {
            uint32_t seg_hash = BH_FNV_OFFSET;
            for (uint32_t i = 0; i < p; i++) {
                seg_hash ^= dirs[off + i];
                seg_hash *= BH_FNV_PRIME;
            }
            if (seg_hash == pattern_hash)
                repeats++;
            else
                break;
        }

        if (repeats >= BH_LOOP_MIN_REPEAT && repeats > best_repeats) {
            best_period = p;
            best_repeats = repeats;
            best_hash = pattern_hash;
        }
    }

    if (best_repeats >= BH_LOOP_MIN_REPEAT) {
        out->rule = BH_RULE_LOOP;
        out->from_idx = from;
        out->to_idx = from + best_period * best_repeats;
        if (out->to_idx > to) out->to_idx = to;
        out->count = best_repeats;
        out->stride = best_period;
        out->pattern_hash = best_hash;
        return 1;
    }
    return 0;
}

/* ── 내부: BURST 탐지 ────────────────────────── */
/*
 * 슬라이딩 윈도우에서 급격한 가격 변동(>threshold) 횟수 탐지.
 * BH_BURST_WINDOW 크기 윈도우 내 BH_BURST_MIN_COUNT회 이상.
 */
static int bh_detect_burst(BhSummary *out,
                           const BtcCandle *candles,
                           uint32_t from, uint32_t to) {
    if (to - from + 1 < BH_BURST_WINDOW + 1) return 0;

    uint32_t best_start = from;
    uint32_t best_count = 0;

    for (uint32_t i = from + 1; i + BH_BURST_WINDOW <= to + 1; i++) {
        uint32_t burst_count = 0;
        for (uint32_t j = i; j < i + BH_BURST_WINDOW && j <= to; j++) {
            uint32_t diff = BTC_ABS32((int32_t)candles[j].close_x100
                                    - (int32_t)candles[j - 1].close_x100);
            if (diff > BH_BURST_THRESHOLD) {
                burst_count++;
            }
        }

        if (burst_count >= BH_BURST_MIN_COUNT && burst_count > best_count) {
            best_start = i;
            best_count = burst_count;
        }
    }

    if (best_count >= BH_BURST_MIN_COUNT) {
        out->rule = BH_RULE_BURST;
        out->from_idx = best_start;
        out->to_idx = best_start + BH_BURST_WINDOW - 1;
        if (out->to_idx > to) out->to_idx = to;
        out->count = best_count;
        out->stride = 0;

        /* 버스트 구간 해시 */
        uint32_t hash = BH_FNV_OFFSET;
        for (uint32_t i = out->from_idx; i <= out->to_idx; i++) {
            hash = bh_fnv1a_u32(candles[i].close_x100, hash);
        }
        out->pattern_hash = hash;
        return 1;
    }
    return 0;
}

/* ── btc_bh_analyze: 단일 구간 분석 ─────────── */

int btc_bh_analyze(BhSummary *out,
                   const BtcCandle *candles,
                   uint32_t from, uint32_t to) {
    if (!out || !candles || from >= to) return 0;

    memset(out, 0, sizeof(BhSummary));

    /* 우선순위: IDLE > LOOP > BURST */
    if (bh_detect_idle(out, candles, from, to)) return 1;
    if (bh_detect_loop(out, candles, from, to)) return 1;
    if (bh_detect_burst(out, candles, from, to)) return 1;

    return 0;
}

/* ── btc_bh_compress_range: 전체 범위 압축 ───── */

int btc_bh_compress_range(BtcBhEngine *engine,
                          const BtcCandle *candles,
                          uint32_t count) {
    if (!engine || !candles || count < 2) return 0;

    memset(engine, 0, sizeof(BtcBhEngine));
    engine->stats.total_candles = count;

    /* 청크 단위 분석: BH_IDLE_MIN_CANDLES × 4 = 64 캔들 */
    uint32_t chunk_size = BH_IDLE_MIN_CANDLES * 4;
    if (chunk_size > count) chunk_size = count;

    uint32_t pos = 0;
    while (pos + chunk_size <= count && engine->summary_count < BH_MAX_SUMMARIES) {
        uint32_t end = pos + chunk_size - 1;
        if (end >= count) end = count - 1;

        BhSummary summary;
        if (btc_bh_analyze(&summary, candles, pos, end)) {
            engine->summaries[engine->summary_count++] = summary;

            uint32_t saved = summary.to_idx - summary.from_idx;
            if (saved > 1) saved -= 1; /* 요약 1개로 대체 */
            engine->stats.candles_saved += saved;

            switch (summary.rule) {
                case BH_RULE_IDLE:  engine->stats.idle_count++;  break;
                case BH_RULE_LOOP:  engine->stats.loop_count++;  break;
                case BH_RULE_BURST: engine->stats.burst_count++; break;
                default: break;
            }

            /* 다음 분석 시작점: 감지된 구간 이후 */
            pos = summary.to_idx + 1;
        } else {
            pos += chunk_size / 2; /* 오버랩하면서 전진 */
        }
    }

    return (int)engine->summary_count;
}

/* ── btc_bh_replay: BH 요약 재생 (Canvas AI 재학습) ── */

int btc_bh_replay(const BhSummary *summary,
                  const BtcCandle *candles,
                  BtcCanvasBrain *brain,
                  BtcTimeframe tf) {
    if (!summary || !candles || !brain) return -1;

    switch (summary->rule) {
        case BH_RULE_IDLE:
            /* IDLE: 대표 캔들 1개만 학습 (변동 없음 구간) */
            if (summary->from_idx + 1 <= summary->to_idx) {
                uint32_t mid = (summary->from_idx + summary->to_idx) / 2;
                uint32_t avg_vol = candles[mid].volume_x10;
                btc_brain_train_candle(brain, tf,
                                       &candles[mid], &candles[mid - 1],
                                       avg_vol);
            }
            return 0;

        case BH_RULE_LOOP:
            /* LOOP: 한 주기만 학습 (반복 구간 압축) */
            for (uint32_t i = summary->from_idx + 1;
                 i <= summary->from_idx + summary->stride
                 && i <= summary->to_idx; i++) {
                uint32_t avg_vol = candles[i].volume_x10;
                btc_brain_train_candle(brain, tf,
                                       &candles[i], &candles[i - 1],
                                       avg_vol);
            }
            return 0;

        case BH_RULE_BURST:
            /* BURST: 전체 학습 (급변 구간은 모두 중요) */
            for (uint32_t i = summary->from_idx + 1;
                 i <= summary->to_idx; i++) {
                uint32_t avg_vol = candles[i].volume_x10;
                btc_brain_train_candle(brain, tf,
                                       &candles[i], &candles[i - 1],
                                       avg_vol);
            }
            return 0;

        default:
            return -1;
    }
}

/* ── TimeWarp: 초기화 ────────────────────────── */

void btc_timewarp_init(BtcTimeWarp *tw) {
    if (!tw) return;
    tw->saved_idx = 0;
    tw->target_idx = 0;
    tw->active = 0;
}

/* ── TimeWarp: 시간 점프 ─────────────────────── */

int btc_timewarp_goto(BtcTimeWarp *tw,
                      uint32_t target_idx,
                      uint32_t current_idx) {
    if (!tw) return -1;
    tw->saved_idx = current_idx;
    tw->target_idx = target_idx;
    tw->active = 1;
    return 0;
}

/* ── TimeWarp: 복귀 ──────────────────────────── */

int btc_timewarp_resume(BtcTimeWarp *tw, uint32_t *restored_idx) {
    if (!tw || !tw->active) return -1;
    if (restored_idx) *restored_idx = tw->saved_idx;
    tw->active = 0;
    tw->target_idx = 0;
    return 0;
}

/* ── TimeWarp: 변화량 측정 ───────────────────── */

uint32_t btc_timewarp_diff(const BtcCandle *candles,
                           uint32_t from_idx, uint32_t to_idx) {
    if (!candles || from_idx >= to_idx) return 0;

    uint32_t count = 0;
    for (uint32_t i = from_idx + 1; i <= to_idx; i++) {
        uint32_t diff = BTC_ABS32((int32_t)candles[i].close_x100
                                - (int32_t)candles[i - 1].close_x100);
        if (diff > 0) count++;
    }
    return count;
}
