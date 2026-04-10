# BTC Candle Sentinel — Makefile
# DK-check: float/double 0건 강제 검증 포함

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2 \
           -I./core -I./core/canvas_ai

CANVAS_AI_SRCS := \
    core/canvas_ai/canvas_ai_b.c \
    core/canvas_ai/cab_brain.c   \
    core/canvas_ai/cab_propagate.c \
    core/canvas_ai/cab_pattern.c

BTC_CORE_SRCS := \
    core/btc_encoding.c    \
    core/btc_canvas_brain.c \
    core/btc_indicators.c  \
    core/btc_cycle.c       \
    core/btc_pattern.c     \
    core/btc_signal.c      \
    core/btc_multiverse.c  \
    core/btc_branch.c      \
    core/btc_timewarp.c

# ── Phase 단위 테스트 ──────────────────────────────
test-p0: tests/test_p0_types.c core/btc_types.h
	$(CC) $(CFLAGS) tests/test_p0_types.c -o /tmp/test_p0
	/tmp/test_p0

test-p1: tests/test_p1_api.c core/btc_api.c core/btc_types.h
	$(CC) $(CFLAGS) tests/test_p1_api.c core/btc_api.c -o /tmp/test_p1
	/tmp/test_p1

test-p2: tests/test_p2_encoding.c core/btc_encoding.c
	$(CC) $(CFLAGS) tests/test_p2_encoding.c core/btc_encoding.c -o /tmp/test_p2
	/tmp/test_p2

test-p3: tests/test_p3_canvas.c core/btc_canvas_brain.c core/btc_encoding.c $(CANVAS_AI_SRCS)
	$(CC) $(CFLAGS) tests/test_p3_canvas.c core/btc_canvas_brain.c \
	    core/btc_encoding.c $(CANVAS_AI_SRCS) -o /tmp/test_p3
	/tmp/test_p3

test-p4: tests/test_p4_indicators.c core/btc_indicators.c
	$(CC) $(CFLAGS) tests/test_p4_indicators.c core/btc_indicators.c -o /tmp/test_p4
	/tmp/test_p4

test-p5: tests/test_p5_cycle.c core/btc_cycle.c
	$(CC) $(CFLAGS) tests/test_p5_cycle.c core/btc_cycle.c -o /tmp/test_p5
	/tmp/test_p5

test-p6: tests/test_p6_pattern.c core/btc_pattern.c core/btc_canvas_brain.c \
         core/btc_encoding.c $(CANVAS_AI_SRCS)
	$(CC) $(CFLAGS) tests/test_p6_pattern.c core/btc_pattern.c \
	    core/btc_canvas_brain.c core/btc_encoding.c $(CANVAS_AI_SRCS) \
	    -o /tmp/test_p6
	/tmp/test_p6

test-p7: tests/test_p7_signal.c core/btc_signal.c core/btc_indicators.c \
         core/btc_canvas_brain.c core/btc_encoding.c $(CANVAS_AI_SRCS)
	$(CC) $(CFLAGS) tests/test_p7_signal.c core/btc_signal.c \
	    core/btc_indicators.c core/btc_canvas_brain.c core/btc_encoding.c \
	    $(CANVAS_AI_SRCS) -o /tmp/test_p7
	/tmp/test_p7

test-p8: tests/test_p8_multiverse.c core/btc_multiverse.c core/btc_branch.c \
         core/btc_timewarp.c core/btc_canvas_brain.c core/btc_encoding.c $(CANVAS_AI_SRCS)
	$(CC) $(CFLAGS) tests/test_p8_multiverse.c core/btc_multiverse.c \
	    core/btc_branch.c core/btc_timewarp.c core/btc_canvas_brain.c \
	    core/btc_encoding.c $(CANVAS_AI_SRCS) -o /tmp/test_p8
	/tmp/test_p8

# ── DK-1 float/double 검사 ─────────────────────────
dk1-check:
	@echo "=== DK-1: float/double 검사 ==="
	@if grep -rn '\bfloat\b\|\bdouble\b' core/*.c core/*.h 2>/dev/null \
	   | grep -v '//.*float\|//.*double' \
	   | grep -v '\*.*DK-1.*float' \
	   | grep -v '\*.*float/double.*0건' \
	   | grep -v '\*.*DK-2.*float'; then \
	    echo "[FAIL] float/double 발견!"; exit 1; \
	else \
	    echo "[PASS] float/double 0건"; \
	fi

# ── 벤치마크 ─────────────────────────────────────
bench: tests/bench_all.c $(BTC_CORE_SRCS) $(CANVAS_AI_SRCS)
	$(CC) $(CFLAGS) tests/bench_all.c $(BTC_CORE_SRCS) $(CANVAS_AI_SRCS) \
	    -o /tmp/bench_all
	/tmp/bench_all

# ── 전체 테스트 ───────────────────────────────────
test-all: test-p0 test-p1 test-p2 test-p3 test-p4 test-p5 test-p6 test-p7 test-p8
	@echo "=== 전체 테스트 완료 ==="

# ══════════════════════════════════════════════════
# ── Binance 실시간 전체 앙상블 ────────────────────
# ══════════════════════════════════════════════════
#
# 의존 라이브러리: libcurl, json-c, pthread
#   Ubuntu/Debian:  sudo apt-get install libcurl4-openssl-dev libjson-c-dev
#   Arch:           sudo pacman -S curl json-c
#
# DK-1/DK-2 준수: float/double 0건 (Q16.16 정수 전용)

ENSEMBLE_SRCS := \
    btc_binance_realtime.c \
    btc_full_ensemble.c    \
    $(BTC_CORE_SRCS)       \
    $(CANVAS_AI_SRCS)

# -std=gnu11: mkdir(), mktime() 등 POSIX 함수를 위해 c11 대신 gnu11 사용
# -I.:        프로젝트 루트 헤더 (btc_binance_realtime.h, btc_full_ensemble.h)
REALTIME_CFLAGS := -std=gnu11 -Wall -Wextra -O2 \
                   -I. -I./core -I./core/canvas_ai
REALTIME_LIBS   := -lcurl -ljson-c -pthread

# ── BTC-AI 단독 빌드 (SJ-CANVAOS 없이) ───────────
btc_full_realtime: main_realtime_full.c $(ENSEMBLE_SRCS)
	$(CC) $(REALTIME_CFLAGS) \
	    main_realtime_full.c $(ENSEMBLE_SRCS) \
	    -o btc_full_realtime \
	    $(REALTIME_LIBS)
	@echo "✅ btc_full_realtime 빌드 완료"

# ── SJ-CANVAOS 통합 빌드 ─────────────────────────
# 선행 조건: make init-submodule (git submodule update --init --recursive)
CANVAOS_SRCS := $(shell find sj-canvaos/src -name "*.c" 2>/dev/null)

btc_full_realtime_canvaos: main_realtime_full.c $(ENSEMBLE_SRCS)
	@if [ ! -f sj-canvaos/include/bt_canvas.h ]; then \
	    echo "[ERROR] sj-canvaos 서브모듈 미초기화. 'make init-submodule' 먼저 실행"; \
	    exit 1; \
	fi
	$(CC) $(REALTIME_CFLAGS) \
	    -Isj-canvaos/include \
	    -DBTC_CANVAOS_ENABLED \
	    main_realtime_full.c $(ENSEMBLE_SRCS) $(CANVAOS_SRCS) \
	    -o btc_full_realtime_canvaos \
	    $(REALTIME_LIBS)
	@echo "✅ btc_full_realtime_canvaos (SJ-CANVAOS 통합) 빌드 완료"

# ── 서브모듈 초기화 안내 ──────────────────────────
init-submodule:
	git submodule update --init --recursive
	@echo "✅ SJ-CANVAOS 서브모듈 초기화 완료"
	@echo "   이제 'make btc_full_realtime_canvaos' 실행 가능"

# ── DK-1/DK-2 루트 파일 포함 전체 검사 ──────────
dk1-check-all: dk1-check
	@echo "=== DK-1 루트 파일 검사 (btc_binance_realtime / btc_full_ensemble / main) ==="
	@if grep -n '\bfloat\b\|\bdouble\b' \
	   btc_binance_realtime.c btc_binance_realtime.h \
	   btc_full_ensemble.c btc_full_ensemble.h \
	   main_realtime_full.c 2>/dev/null \
	   | grep -v '//.*float\|//.*double' \
	   | grep -v '\* .*float\|Q16\.16.*float'; then \
	    echo "[FAIL] float/double 발견!"; exit 1; \
	else \
	    echo "[PASS] 루트 앙상블 파일 float/double 0건"; \
	fi

.PHONY: test-p0 test-p1 test-p2 test-p3 test-p4 test-p5 test-p6 test-p7 test-p8 \
        dk1-check bench test-all \
        btc_full_realtime btc_full_realtime_canvaos \
        init-submodule dk1-check-all
