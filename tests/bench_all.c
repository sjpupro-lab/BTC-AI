/*
 * bench_all.c — BTC Candle Sentinel Full Benchmark Suite
 * ======================================================
 * Measures throughput of all core components.
 * DK-1: float/double 0 instances. Integer-only computation.
 *        clock_t / CLOCKS_PER_SEC used for timing (measurement only).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "../core/btc_types.h"
#include "../core/btc_encoding.h"
#include "../core/btc_canvas_brain.h"
#include "../core/btc_indicators.h"
#include "../core/btc_cycle.h"
#include "../core/btc_pattern.h"
#include "../core/btc_signal.h"

/* ── Test data generation (ascending candles, integer only) ── */

static void generate_candles(BtcCandle *out, uint32_t count)
{
    uint32_t base_price = 3000000;  /* $30,000.00 x100 */
    uint32_t base_vol   = 1000;     /* 100.0 BTC x10   */
    uint64_t ts         = 1700000000ULL;

    for (uint32_t i = 0; i < count; i++) {
        /* Ascending price with small oscillation */
        uint32_t offset = i * 100;              /* +$1.00 per candle  */
        uint32_t noise  = (i * 37 + 13) % 500;  /* pseudo-random 0~499 */
        uint32_t open   = base_price + offset;
        uint32_t close  = open + noise;
        uint32_t high   = close + 200;
        uint32_t low    = open > 200 ? open - 200 : 1;

        out[i].timestamp  = ts + (uint64_t)i * 60;
        out[i].open_x100  = open;
        out[i].high_x100  = high;
        out[i].low_x100   = low;
        out[i].close_x100 = close;
        out[i].volume_x10 = base_vol + (i * 7) % 500;
    }
}

/* ── Timing helpers (clock_t based, measurement only) ── */

typedef struct {
    const char *name;
    uint32_t    iterations;
    clock_t     elapsed;
} BenchResult;

#define BENCH_MAX 14

static long bench_ms(clock_t elapsed)
{
    return (long)(elapsed * 1000 / CLOCKS_PER_SEC);
}

static long bench_ops_sec(uint32_t iterations, clock_t elapsed)
{
    if (elapsed == 0) return 0;
    return (long)((clock_t)iterations * CLOCKS_PER_SEC / elapsed);
}

/* ── Individual benchmarks ── */

/* 1. Encoding: encode 10,000 candles to BtcCandleBytes */
static BenchResult bench_encoding(const BtcCandle *candles, uint32_t count)
{
    BenchResult r = { .name = "Encoding (10K candles)", .iterations = count };
    BtcCandleBytes cb;
    uint32_t avg_vol = 1000;

    clock_t start = clock();
    for (uint32_t i = 1; i < count; i++) {
        btc_candle_encode(&cb, &candles[i], &candles[i - 1], avg_vol);
    }
    r.elapsed = clock() - start;
    return r;
}

/* 2. Canvas AI Training: train 1,000 candles on BtcCanvasBrain */
static BenchResult bench_canvas_train(BtcCanvasBrain *brain,
                                      const BtcCandle *candles,
                                      uint32_t count)
{
    BenchResult r = { .name = "Canvas AI Train (1K)", .iterations = count };
    uint32_t avg_vol = 1000;

    clock_t start = clock();
    for (uint32_t i = 1; i < count; i++) {
        btc_brain_train_candle(brain, TF_1M,
                               &candles[i], &candles[i - 1], avg_vol);
    }
    r.elapsed = clock() - start;
    return r;
}

/* 3. Canvas AI Prediction: run 1,000 predictions */
static BenchResult bench_canvas_predict(BtcCanvasBrain *brain, uint32_t count)
{
    BenchResult r = { .name = "Canvas AI Predict (1K)", .iterations = count };
    BtcPrediction pred;

    clock_t start = clock();
    for (uint32_t i = 0; i < count; i++) {
        btc_brain_predict(brain, TF_1M, &pred);
    }
    r.elapsed = clock() - start;
    return r;
}

/* 4. Bollinger Bands: 500 candles x 1,000 iterations */
static BenchResult bench_bollinger(const BtcCandle *candles,
                                   uint32_t candle_count,
                                   uint32_t iterations)
{
    BenchResult r = { .name = "Bollinger Bands", .iterations = iterations };
    BollingerBands bb;

    clock_t start = clock();
    for (uint32_t i = 0; i < iterations; i++) {
        btc_calc_bb(&bb, candles, candle_count, 20);
    }
    r.elapsed = clock() - start;
    return r;
}

/* 5. RSI: 500 candles x 1,000 iterations */
static BenchResult bench_rsi(const BtcCandle *candles,
                             uint32_t candle_count,
                             uint32_t iterations)
{
    BenchResult r = { .name = "RSI", .iterations = iterations };
    RsiResult rsi;

    clock_t start = clock();
    for (uint32_t i = 0; i < iterations; i++) {
        btc_calc_rsi(&rsi, candles, candle_count, 14);
    }
    r.elapsed = clock() - start;
    return r;
}

/* 6. MACD: 500 candles x 1,000 iterations */
static BenchResult bench_macd(const BtcCandle *candles,
                              uint32_t candle_count,
                              uint32_t iterations)
{
    BenchResult r = { .name = "MACD", .iterations = iterations };
    MacdResult macd;

    clock_t start = clock();
    for (uint32_t i = 0; i < iterations; i++) {
        btc_calc_macd(&macd, candles, candle_count, 12, 26, 9);
    }
    r.elapsed = clock() - start;
    return r;
}

/* 7. Ichimoku: 500 candles x 1,000 iterations */
static BenchResult bench_ichimoku(const BtcCandle *candles,
                                  uint32_t candle_count,
                                  uint32_t iterations)
{
    BenchResult r = { .name = "Ichimoku", .iterations = iterations };
    IchimokuResult ich;

    clock_t start = clock();
    for (uint32_t i = 0; i < iterations; i++) {
        btc_calc_ichimoku(&ich, candles, candle_count);
    }
    r.elapsed = clock() - start;
    return r;
}

/* 8. Fibonacci: 500 candles x 1,000 iterations */
static BenchResult bench_fibonacci(const BtcCandle *candles,
                                   uint32_t candle_count,
                                   uint32_t iterations)
{
    BenchResult r = { .name = "Fibonacci", .iterations = iterations };
    FibResult fib;

    clock_t start = clock();
    for (uint32_t i = 0; i < iterations; i++) {
        btc_calc_fib(&fib, candles, candle_count, 100);
    }
    r.elapsed = clock() - start;
    return r;
}

/* 9. Volume Profile: 500 candles x 1,000 iterations */
static BenchResult bench_volume_profile(const BtcCandle *candles,
                                        uint32_t candle_count,
                                        uint32_t iterations)
{
    BenchResult r = { .name = "Volume Profile", .iterations = iterations };
    VolumeProfile vp;

    clock_t start = clock();
    for (uint32_t i = 0; i < iterations; i++) {
        btc_calc_vp(&vp, candles, candle_count);
    }
    r.elapsed = clock() - start;
    return r;
}

/* 10. Cycle Detection: 200 candles x 100 iterations */
static BenchResult bench_cycle(const BtcCandle *candles,
                               uint32_t candle_count,
                               uint32_t iterations)
{
    BenchResult r = { .name = "Cycle Detection", .iterations = iterations };
    CycleResult cyc;

    clock_t start = clock();
    for (uint32_t i = 0; i < iterations; i++) {
        btc_detect_cycles(&cyc, candles, candle_count);
    }
    r.elapsed = clock() - start;
    return r;
}

/* 11. SR7 Levels: 500 candles x 1,000 iterations */
static BenchResult bench_sr7(const BtcCandle *candles,
                             uint32_t candle_count,
                             uint32_t iterations)
{
    BenchResult r = { .name = "SR7 Levels", .iterations = iterations };
    SR7Levels sr7;

    clock_t start = clock();
    for (uint32_t i = 0; i < iterations; i++) {
        btc_calc_sr7(&sr7, candles, candle_count, 100);
    }
    r.elapsed = clock() - start;
    return r;
}

/* 12. Pattern Search: 100 iterations */
static BenchResult bench_pattern(BtcCanvasBrain *brain,
                                 const BtcCandle *candles,
                                 uint32_t candle_count,
                                 uint32_t iterations)
{
    BenchResult r = { .name = "Pattern Search", .iterations = iterations };
    PatternSimilarityResult pat;

    clock_t start = clock();
    for (uint32_t i = 0; i < iterations; i++) {
        btc_pattern_search(&pat, brain, TF_1M, candles, candle_count);
    }
    r.elapsed = clock() - start;
    return r;
}

/* 13. Signal Fusion: 10,000 iterations */
static BenchResult bench_signal_fusion(uint32_t iterations)
{
    BenchResult r = { .name = "Signal Fusion", .iterations = iterations };

    /* Prepare fixed inputs */
    BtcPrediction canvas;
    memset(&canvas, 0, sizeof(canvas));
    canvas.direction  = SIGNAL_LONG;
    canvas.confidence = 750;

    BollingerBands bb;
    memset(&bb, 0, sizeof(bb));
    bb.middle_x100 = 3500000;
    bb.upper_x100  = 3600000;
    bb.lower_x100  = 3400000;
    bb.position    = 80;

    SR7Levels sr7;
    memset(&sr7, 0, sizeof(sr7));
    for (int i = 0; i < 7; i++) {
        sr7.levels[i]   = 3400000 + (uint32_t)i * 30000;
        sr7.strength[i] = 128;
    }

    RsiResult rsi;
    memset(&rsi, 0, sizeof(rsi));
    rsi.value = 55;

    MacdResult macd;
    memset(&macd, 0, sizeof(macd));
    macd.macd_x1000      = 500;
    macd.signal_x1000    = 200;
    macd.histogram_x1000 = 300;
    macd.cross           = 1;

    PatternSimilarityResult pat;
    memset(&pat, 0, sizeof(pat));
    pat.count               = 3;
    pat.consensus_direction = 1;
    pat.consensus_strength  = 80;

    BtcCandle cur;
    memset(&cur, 0, sizeof(cur));
    cur.open_x100  = 3480000;
    cur.high_x100  = 3510000;
    cur.low_x100   = 3470000;
    cur.close_x100 = 3500000;

    BtcSignalConfig cfg = { .premium_enabled = 1 };
    BtcSignal sig;

    clock_t start = clock();
    for (uint32_t i = 0; i < iterations; i++) {
        btc_signal_compute(&sig, &canvas, &bb, &sr7,
                           &rsi, &macd, &pat, &cur, &cfg);
    }
    r.elapsed = clock() - start;
    return r;
}

/* ── Output formatting ── */

static void print_table_separator(void)
{
    printf("\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80"
           "  "
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "    "
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "    "
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\n");
}

static void print_result(const BenchResult *r)
{
    long ms  = bench_ms(r->elapsed);
    long ops = bench_ops_sec(r->iterations, r->elapsed);

    printf("%-27s %10u    %9ld    %8ld\n",
           r->name, r->iterations, ms, ops);
}

/* ── Main ── */

int main(void)
{
    printf("\n");
    printf("=== BTC Candle Sentinel \xe2\x80\x94 Benchmark Results ===\n");

    /* Platform info */
    printf("Platform: ");
#if defined(__aarch64__)
    printf("ARM64");
#elif defined(__x86_64__) || defined(_M_X64)
    printf("x86_64");
#elif defined(__i386__) || defined(_M_IX86)
    printf("x86");
#else
    printf("unknown");
#endif

#if defined(__ANDROID__)
    printf(" / Android");
#elif defined(__linux__)
    printf(" / Linux");
#elif defined(__APPLE__)
    printf(" / macOS");
#elif defined(_WIN32)
    printf(" / Windows");
#endif
    printf("\n");

    /* Date */
    {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        printf("Date: %04d-%02d-%02d %02d:%02d:%02d\n",
               t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
               t->tm_hour, t->tm_min, t->tm_sec);
    }

    printf("CLOCKS_PER_SEC: %ld\n\n", (long)CLOCKS_PER_SEC);

    /* Generate test candle data */
    uint32_t max_candles = 10000;
    BtcCandle *candles = (BtcCandle *)malloc(max_candles * sizeof(BtcCandle));
    if (!candles) {
        printf("ERROR: failed to allocate candle array\n");
        return 1;
    }
    generate_candles(candles, max_candles);

    /* Initialize Canvas AI brain */
    BtcCanvasBrain brain;
    int rc = btc_brain_init(&brain);
    if (rc != BTC_OK) {
        printf("ERROR: btc_brain_init failed (%d)\n", rc);
        free(candles);
        return 1;
    }

    /* Pre-train brain with some data so predictions are meaningful */
    {
        uint32_t avg_vol = 1000;
        for (uint32_t i = 1; i < 500; i++) {
            btc_brain_train_candle(&brain, TF_1M,
                                   &candles[i], &candles[i - 1], avg_vol);
        }
    }

    clock_t total_start = clock();

    /* Collect results */
    BenchResult results[BENCH_MAX];
    int n = 0;

    /*  1. Encoding: 10,000 candles */
    results[n++] = bench_encoding(candles, 10000);

    /*  2. Canvas AI Training: 1,000 candles */
    results[n++] = bench_canvas_train(&brain, candles, 1000);

    /*  3. Canvas AI Prediction: 1,000 predictions */
    results[n++] = bench_canvas_predict(&brain, 1000);

    /*  4. Bollinger Bands: 500 candles x 1,000 */
    results[n++] = bench_bollinger(candles, 500, 1000);

    /*  5. RSI: 500 candles x 1,000 */
    results[n++] = bench_rsi(candles, 500, 1000);

    /*  6. MACD: 500 candles x 1,000 */
    results[n++] = bench_macd(candles, 500, 1000);

    /*  7. Ichimoku: 500 candles x 1,000 */
    results[n++] = bench_ichimoku(candles, 500, 1000);

    /*  8. Fibonacci: 500 candles x 1,000 */
    results[n++] = bench_fibonacci(candles, 500, 1000);

    /*  9. Volume Profile: 500 candles x 1,000 */
    results[n++] = bench_volume_profile(candles, 500, 1000);

    /* 10. Cycle Detection: 200 candles x 100 */
    results[n++] = bench_cycle(candles, 200, 100);

    /* 11. SR7 Levels: 500 candles x 1,000 */
    results[n++] = bench_sr7(candles, 500, 1000);

    /* 12. Pattern Search: 100 iterations */
    results[n++] = bench_pattern(&brain, candles, 500, 100);

    /* 13. Signal Fusion: 10,000 iterations */
    results[n++] = bench_signal_fusion(10000);

    clock_t total_elapsed = clock() - total_start;

    /* Print results table */
    printf("%-27s %10s    %9s    %8s\n",
           "Component", "Iterations", "Time (ms)", "Ops/sec");
    print_table_separator();

    for (int i = 0; i < n; i++) {
        print_result(&results[i]);
    }

    /* 14. Memory footprint */
    printf("\n=== Memory Footprint ===\n");
    printf("BtcCandle:                  %4zu bytes\n", sizeof(BtcCandle));
    printf("BtcCandleBytes:             %4zu bytes\n", sizeof(BtcCandleBytes));
    printf("BtcByteStream:              %4zu bytes\n", sizeof(BtcByteStream));
    printf("BtcCanvasBrain:             %4zu bytes\n", sizeof(BtcCanvasBrain));
    printf("BtcPrediction:              %4zu bytes\n", sizeof(BtcPrediction));
    printf("BtcSignal:                  %4zu bytes\n", sizeof(BtcSignal));
    printf("BollingerBands:             %4zu bytes\n", sizeof(BollingerBands));
    printf("SR7Levels:                  %4zu bytes\n", sizeof(SR7Levels));
    printf("RsiResult:                  %4zu bytes\n", sizeof(RsiResult));
    printf("MacdResult:                 %4zu bytes\n", sizeof(MacdResult));
    printf("VolumeProfile:              %4zu bytes\n", sizeof(VolumeProfile));
    printf("FibResult:                  %4zu bytes\n", sizeof(FibResult));
    printf("IchimokuResult:             %4zu bytes\n", sizeof(IchimokuResult));
    printf("CycleResult:                %4zu bytes\n", sizeof(CycleResult));
    printf("PatternSimilarityResult:    %4zu bytes\n", sizeof(PatternSimilarityResult));
    printf("CabBrain:                   %4zu bytes\n", sizeof(CabBrain));
    printf("CabCanvas:                  %4zu bytes\n", sizeof(CabCanvas));
    printf("CabCell:                    %4zu bytes\n", sizeof(CabCell));
    printf("CabActivation:              %4zu bytes\n", sizeof(CabActivation));

    /* Summary */
    printf("\n=== Summary ===\n");
    printf("Total benchmark time: %ld ms\n", bench_ms(total_elapsed));
    printf("DK-1 compliance: integer-only (zero float/double)\n");

    /* Cleanup */
    btc_brain_free(&brain);
    free(candles);

    printf("\n");
    return 0;
}
