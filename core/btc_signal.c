/*
 * btc_signal.c — 신호 융합 엔진 구현
 * DK-1: float/double 0건. 정수 전용.
 * Phase 7.
 */
#include "btc_signal.h"
#include <stddef.h>
#include <string.h>

/* ── 내부: Canvas 점수 (max 40) ─────────────────── */
static void score_canvas(BtcSignal *out, const BtcPrediction *canvas)
{
    uint8_t base = 0;

    if (canvas->direction == SIGNAL_LONG) {
        base = (uint8_t)(canvas->confidence * 40 / 1000);
        out->reason_canvas = "Canvas AI: LONG signal";
    } else if (canvas->direction == SIGNAL_SHORT) {
        base = (uint8_t)(canvas->confidence * 40 / 1000);
        out->reason_canvas = "Canvas AI: SHORT signal";
    } else if (canvas->direction == SIGNAL_HOLD) {
        base = 0;
        out->reason_canvas = "Canvas AI: HOLD (no edge)";
    } else {
        base = 0;
        out->reason_canvas = "Canvas AI: no signal";
    }

    if (base > 40) base = 40;
    out->canvas_score = base;
    out->direction = canvas->direction;
}

/* ── 내부: BB 점수 (max 15) ─────────────────────── */
static void score_bb(BtcSignal *out, const BollingerBands *bb)
{
    uint8_t pos = bb->position;
    uint8_t score = 0;

    if (out->direction == SIGNAL_LONG) {
        if (pos < 80) {
            score = 15;
            out->reason_bb = "BB: near lower band (oversold bounce)";
        } else if (pos < 127) {
            score = 10;
            out->reason_bb = "BB: lower half (support zone)";
        } else if (pos <= 200) {
            score = 5;
            out->reason_bb = "BB: upper half (neutral)";
        } else {
            score = 2;
            out->reason_bb = "BB: near upper band (momentum)";
        }
    } else if (out->direction == SIGNAL_SHORT) {
        if (pos > 180) {
            score = 15;
            out->reason_bb = "BB: near upper band (overbought reversal)";
        } else if (pos > 127) {
            score = 10;
            out->reason_bb = "BB: upper half (resistance zone)";
        } else if (pos >= 60) {
            score = 5;
            out->reason_bb = "BB: lower half (neutral)";
        } else {
            score = 2;
            out->reason_bb = "BB: near lower band (momentum down)";
        }
    } else {
        score = 3;
        out->reason_bb = "BB: no directional bias";
    }

    out->bb_score = score;
}

/* ── 내부: SR7 점수 (max 15) ────────────────────── */
static void score_sr7(BtcSignal *out, const SR7Levels *sr7,
                      const BtcCandle *current)
{
    uint32_t price = current->close_x100;
    /* threshold: 0.5% = price * 5 / 1000 */
    uint32_t thresh = price * 5 / 1000;
    uint8_t  score = 5; /* default: between levels */
    int near_support = 0;
    int near_resist  = 0;

    out->reason_sr7 = "SR7: price between S/R levels";

    for (int i = 0; i < 7; i++) {
        uint32_t lev = sr7->levels[i];
        uint32_t diff;
        if (price >= lev) {
            diff = price - lev;
        } else {
            diff = lev - price;
        }

        if (diff <= thresh) {
            /* Near this level: support if price is at or above, resist if below */
            if (lev <= price) {
                near_support = 1;
            } else {
                near_resist = 1;
            }
        }
    }

    if (out->direction == SIGNAL_LONG && near_support) {
        score = 15;
        out->reason_sr7 = "SR7: at support level (LONG aligned)";
    } else if (out->direction == SIGNAL_SHORT && near_resist) {
        score = 15;
        out->reason_sr7 = "SR7: at resistance level (SHORT aligned)";
    } else if (out->direction == SIGNAL_LONG && near_resist) {
        score = 3;
        out->reason_sr7 = "SR7: at resistance (contradicts LONG)";
    } else if (out->direction == SIGNAL_SHORT && near_support) {
        score = 3;
        out->reason_sr7 = "SR7: at support (contradicts SHORT)";
    }

    out->sr7_score = score;
}

/* ── 내부: RSI 점수 (max 10, premium) ───────────── */
static uint8_t score_rsi(SignalDir dir, const RsiResult *rsi)
{
    if (!rsi) return 0;

    uint8_t val = rsi->value;

    if (dir == SIGNAL_LONG) {
        if (val < 30)      return 10;  /* oversold: strong LONG signal */
        if (val <= 50)     return 7;
        if (val <= 70)     return 3;
        return 0;                      /* overbought: contradicts LONG */
    }
    if (dir == SIGNAL_SHORT) {
        if (val > 70)      return 10;  /* overbought: strong SHORT signal */
        if (val >= 50)     return 7;
        if (val >= 30)     return 3;
        return 0;                      /* oversold: contradicts SHORT */
    }

    /* HOLD or NONE */
    return 2;
}

/* ── 내부: MACD 점수 (max 10, premium) ──────────── */
static uint8_t score_macd(SignalDir dir, const MacdResult *macd)
{
    if (!macd) return 0;

    if (dir == SIGNAL_LONG) {
        if (macd->cross == 1)               return 10; /* golden cross */
        if (macd->histogram_x1000 > 0)      return 5;  /* histogram positive */
        return 2;
    }
    if (dir == SIGNAL_SHORT) {
        if (macd->cross == 2)               return 10; /* dead cross */
        if (macd->histogram_x1000 < 0)      return 5;  /* histogram negative */
        return 2;
    }

    /* HOLD or NONE */
    return 2;
}

/* ── 내부: 패턴 점수 (max 10, premium) ──────────── */
static uint8_t score_pattern(SignalDir dir, const PatternSimilarityResult *pat)
{
    if (!pat) return 0;
    if (pat->count == 0) return 0;

    /* consensus_direction: 1=up, 2=down, 0=unknown */
    int agrees = 0;
    if (dir == SIGNAL_LONG  && pat->consensus_direction == 1) agrees = 1;
    if (dir == SIGNAL_SHORT && pat->consensus_direction == 2) agrees = 1;

    if (agrees) {
        uint8_t s = (uint8_t)(pat->consensus_strength * 10 / 100);
        return (s > 10) ? 10 : s;
    }

    return 0;
}

/* ── 내부: 충돌 해소 ────────────────────────────── */
static void resolve_conflicts(BtcSignal *out, const BtcSignalConfig *cfg,
                              const RsiResult *rsi,
                              const MacdResult *macd,
                              const PatternSimilarityResult *pat)
{
    /*
     * Count components that "agree" vs "disagree" with direction.
     * Threshold for "agreeing": score > half of max.
     * canvas(max40): agree if >20
     * bb(max15): agree if >7
     * sr7(max15): agree if >7
     * rsi(max10): agree if >5 (premium only)
     * macd(max10): agree if >5 (premium only)
     * pattern(max10): agree if >5 (premium only)
     */
    int agree = 0;
    int disagree = 0;

    if (out->canvas_score > 20) agree++; else disagree++;
    if (out->bb_score > 7)      agree++; else disagree++;
    if (out->sr7_score > 7)     agree++; else disagree++;

    if (cfg->premium_enabled) {
        if (rsi) {
            if (out->rsi_score > 5) agree++; else disagree++;
        }
        if (macd) {
            if (out->macd_score > 5) agree++; else disagree++;
        }
        if (pat) {
            if (out->pattern_score > 5) agree++; else disagree++;
        }
    }

    if (disagree > agree) {
        out->total_score = out->total_score / 2;
        out->direction = SIGNAL_HOLD;
        out->strength = STRENGTH_WEAK;
    }
}

/* ── 공개 API ───────────────────────────────────── */
void btc_signal_compute(BtcSignal *out,
                        const BtcPrediction   *canvas,
                        const BollingerBands  *bb,
                        const SR7Levels       *sr7,
                        const RsiResult       *rsi,
                        const MacdResult      *macd,
                        const PatternSimilarityResult *pat,
                        const BtcCandle       *current,
                        const BtcSignalConfig *cfg)
{
    memset(out, 0, sizeof(*out));
    out->reason_canvas = "";
    out->reason_bb     = "";
    out->reason_sr7    = "";

    /* Step 1: Canvas score (sets direction) */
    score_canvas(out, canvas);

    /* Step 2: BB score */
    score_bb(out, bb);

    /* Step 3: SR7 score */
    score_sr7(out, sr7, current);

    /* Step 4: Premium indicators */
    if (cfg->premium_enabled) {
        out->rsi_score     = score_rsi(out->direction, rsi);
        out->macd_score    = score_macd(out->direction, macd);
        out->pattern_score = score_pattern(out->direction, pat);
    } else {
        out->rsi_score     = 0;
        out->macd_score    = 0;
        out->pattern_score = 0;
    }

    /* Step 5: Total */
    uint32_t total = (uint32_t)out->canvas_score
                   + (uint32_t)out->bb_score
                   + (uint32_t)out->sr7_score
                   + (uint32_t)out->rsi_score
                   + (uint32_t)out->macd_score
                   + (uint32_t)out->pattern_score;
    if (total > 100) total = 100;
    out->total_score = (uint8_t)total;

    /* Step 6: Direction fallback for NONE/HOLD */
    if (out->direction == SIGNAL_NONE || out->direction == SIGNAL_HOLD) {
        out->direction = SIGNAL_HOLD;
    }

    /* Step 7: Strength mapping */
    if (out->total_score >= 70) {
        out->strength = STRENGTH_STRONG;
    } else if (out->total_score >= 41) {
        out->strength = STRENGTH_MED;
    } else {
        out->strength = STRENGTH_WEAK;
    }

    /* Step 8: Conflict resolution (may override direction/strength) */
    resolve_conflicts(out, cfg, rsi, macd, pat);
}
