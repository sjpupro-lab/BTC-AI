# BTC Candle Sentinel

Canvas AI 기반 BTC 가격 예측 엔진 — **C11 정수 전용** (float/double 0건)

## Overview

BTC Candle Sentinel은 해시 테이블 기반 결정론적 AI 엔진(Canvas AI)과 전통 기술적 지표를 결합하여
BTC 캔들 데이터를 분석하고 매매 신호를 생성하는 순수 C 라이브러리입니다.

**핵심 특징:**
- **100% 정수 연산** — float/double 사용 0건 (DK-1 규칙)
- **결정론적** — 동일 입력 → 항상 동일 출력 (DK-2)
- **경량** — BtcCanvasBrain 560 bytes, BtcCandle 28 bytes
- **빠름** — 인코딩 66M ops/sec, 시그널 퓨전 60M ops/sec
- **Android NDK 호환** — 표준 C11, 외부 라이브러리 불필요

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                   Signal Fusion (Phase 7)               │
│  Canvas AI(40) + BB(15) + SR7(15) + RSI(10)            │
│                + MACD(10) + Pattern(10) = 100점          │
├──────────┬──────────┬───────────┬───────────┬───────────┤
│ Canvas AI│ Bollinger│  SR7/Cycle│ RSI/MACD  │  Pattern  │
│ (Phase 3)│ (Phase 4)│ (Phase 5) │ (Phase 4) │ (Phase 6) │
├──────────┴──────────┴───────────┴───────────┴───────────┤
│              v6f Encoding Engine (Phase 2)               │
│         price/volume/body/wick → 4-byte candle           │
├─────────────────────────────────────────────────────────┤
│          BTC Types & API (Phase 0-1)                     │
│     BtcCandle(28B) · BtcTimeframe(7) · TfManager        │
└─────────────────────────────────────────────────────────┘
```

## Project Structure

```
BTC-AI/
├── core/
│   ├── btc_types.h            # 핵심 타입 (BtcCandle, 매크로)
│   ├── btc_api.h/c            # CryptoCompare JSON 파서, TF 매니저
│   ├── btc_encoding.h/c       # v6f 인코딩 엔진 (±5% → 0~254)
│   ├── btc_canvas_brain.h/c   # Canvas AI BTC 래퍼
│   ├── btc_indicators.h/c     # BB, RSI, MACD, Ichimoku, Fib, VP
│   ├── btc_cycle.h/c          # 소인수분해, 주기 탐지, SR7
│   ├── btc_pattern.h/c        # Gear Hash 패턴 유사도 엔진
│   ├── btc_signal.h/c         # 6-컴포넌트 시그널 퓨전
│   └── canvas_ai/             # Canvas AI 코어 엔진
│       ├── canvasos_types.h   # CabCell (8B RGBA), 기본 타입
│       ├── canvas_ai_b.h/c    # 캔버스 초기화/훈련/예측/저장
│       ├── cab_sieve_gear.h   # CabBrain, Gear Chain, OPT-A/C/D
│       ├── cab_brain.c        # 고속 훈련/예측 (cab_train_fast)
│       ├── cab_propagate.h/c  # 활성화 전파 (계층간 확산)
│       ├── cab_propagate_fast.h # OPT-B 희소 전파
│       ├── cab_pattern.h/c    # 후보 평가 5단계 (A~E)
│       ├── canvas_ai_layers.h # 7계층 깊이 매핑
│       └── canvas_determinism.h # DK 규칙, Q8 고정소수점
├── tests/
│   ├── test_p0_types.c        # Phase 0: 타입 검증 (26 assertions)
│   ├── test_p1_api.c          # Phase 1: API/TF 매니저 (19)
│   ├── test_p2_encoding.c     # Phase 2: 인코딩 엔진 (15)
│   ├── test_p3_canvas.c       # Phase 3: Canvas AI 코어 (41)
│   ├── test_p4_indicators.c   # Phase 4: 기술적 지표 (32)
│   ├── test_p5_cycle.c        # Phase 5: 주기/S/R (19)
│   ├── test_p6_pattern.c      # Phase 6: 패턴 유사도 (15)
│   ├── test_p7_signal.c       # Phase 7: 시그널 퓨전 (35)
│   └── bench_all.c            # 전체 벤치마크 스위트
├── Makefile
└── README.md
```

## Determinism Rules (DK)

| Rule | Description |
|------|-------------|
| DK-1 | float/double 사용 금지. 모든 연산은 정수 전용 |
| DK-2 | 동일 입력 → 동일 출력. rand()/time() 사용 금지 |
| DK-3 | 해시 충돌 시 prime sieve elimination |
| DK-4 | 플랫폼 무관 동작 (no endian/alignment dependency) |
| DK-5 | 저장 포맷 CVP v3 (하위 호환) |

## Signal Scoring System

| Component | Max | Tier | Description |
|-----------|-----|------|-------------|
| Canvas AI | 40 | Free | Gear Hash 기반 예측 (confidence → 점수) |
| Bollinger Bands | 15 | Free | 밴드 위치 기반 (position → 점수) |
| SR7 Levels | 15 | Free | 7단계 지지/저항 근접도 |
| RSI | 10 | Premium | 과매수/과매도 반전 감지 |
| MACD | 10 | Premium | 골든/데드 크로스 감지 |
| Pattern | 10 | Premium | 과거 유사 패턴 합의 방향 |
| **Total** | **100** | | |

- **Free**: Canvas AI + BB + SR7 = 최대 70점
- **Premium**: 전체 6개 컴포넌트 = 최대 100점
- **충돌 해소**: 상승/하락 지표 불일치 시 점수 반감, HOLD/WEAK 출력

## Build & Test

```bash
# 전체 테스트 실행 (Phase 0~7, 202 assertions)
make test-all

# 개별 Phase 테스트
make test-p0   # Types
make test-p1   # API
make test-p2   # Encoding
make test-p3   # Canvas AI
make test-p4   # Indicators
make test-p5   # Cycle/SR
make test-p6   # Pattern
make test-p7   # Signal Fusion

# DK-1 float/double 검사
make dk1-check

# 벤치마크
make bench
```

**요구사항:** GCC (C11), Make

## Test Results

```
Phase 0 (Types):          26 PASS, 0 FAIL
Phase 1 (API):            19 PASS, 0 FAIL
Phase 2 (Encoding):       15 PASS, 0 FAIL
Phase 3 (Canvas AI):      41 PASS, 0 FAIL
Phase 4 (Indicators):     32 PASS, 0 FAIL
Phase 5 (Cycle/SR):       19 PASS, 0 FAIL
Phase 6 (Pattern):        15 PASS, 0 FAIL
Phase 7 (Signal Fusion):  35 PASS, 0 FAIL
────────────────────────────────────
Total:                   202 PASS, 0 FAIL
DK-1 compliance:         PASS (float/double 0건)
```

## Benchmark Results

**Platform:** x86_64 / Linux  
**Compiler:** GCC -O2, C11  

### Throughput

| Component | Iterations | Time (ms) | Ops/sec |
|-----------|-----------|-----------|---------|
| Encoding (10K candles) | 10,000 | <1 | 66,666,666 |
| Canvas AI Train (1K) | 1,000 | 1,939 | 515 |
| Canvas AI Predict (1K) | 1,000 | 8,843 | 113 |
| Bollinger Bands | 1,000 | <1 | 21,276,595 |
| RSI | 1,000 | 3 | 330,797 |
| MACD | 1,000 | 2 | 479,386 |
| Ichimoku | 1,000 | <1 | 9,345,794 |
| Fibonacci | 1,000 | <1 | 10,204,081 |
| Volume Profile | 1,000 | 1 | 678,886 |
| Cycle Detection | 100 | 2 | 49,382 |
| SR7 Levels | 1,000 | 1 | 719,942 |
| Pattern Search | 100 | 1 | 70,621 |
| Signal Fusion | 10,000 | <1 | 60,240,963 |

**Total benchmark time: 10,794 ms**

### Memory Footprint

| Structure | Size |
|-----------|------|
| BtcCandle | 28 bytes |
| BtcCandleBytes | 4 bytes |
| BtcCanvasBrain | 560 bytes |
| BtcPrediction | 52 bytes |
| BtcSignal | 40 bytes |
| BollingerBands | 16 bytes |
| SR7Levels | 36 bytes |
| RsiResult | 3 bytes |
| MacdResult | 16 bytes |
| VolumeProfile | 204 bytes |
| FibResult | 40 bytes |
| IchimokuResult | 24 bytes |
| CycleResult | 360 bytes |
| PatternSimilarityResult | 104 bytes |
| CabBrain | 440 bytes |
| CabCanvas | 280 bytes |
| CabCell | 8 bytes |

## Canvas AI Engine

Canvas AI는 해시 테이블 기반 결정론적 학습 엔진입니다:

- **CabCell (8 bytes):** `[A:uint32 | G:uint16 | R:uint8 | B:uint8]` — RGBA 구조
- **Gear Hash Chain:** FNV-1a 기반 다층 컨텍스트 해싱 (깊이 1~32)
- **B-channel regime:** `sem_B = [layer:3bit][semantic_class:5bit]` — Bull/Bear/Sideways 격리
- **Prime Sieve Elimination:** 해시 충돌 시 소수 기반 건너뛰기 (DK-3)
- **7-Layer Hierarchy:** Byte(1m) → Morpheme(5m) → Word(15m) → Phrase(1h) → Clause(4h) → Sentence(1d) → Topic(1w)

## v6f Encoding

캔들 데이터를 4바이트로 압축:

| Byte | Field | Encoding |
|------|-------|----------|
| 0 | Price | ±5% 가격 변화 → 0~254 (기준 127 = 변화 없음) |
| 1 | Volume | 현재/평균 볼륨 비율 → 0~254 (127 = 평균) |
| 2 | Body | 몸통 크기 비율 → 0~254 |
| 3 | Upper Wick | 윗꼬리 비율 → 0~254 |

## Integer-Only Techniques

- **EMA:** `alpha × 10000` 방식 (EMA20: alpha=952, EMA50: alpha=392)
- **Bollinger Bands:** Newton 정수 제곱근 (`isqrt`)
- **RSI:** Wilder EMA (정수 스케일링)
- **MACD:** EMA × 10000 스케일
- **Jaccard Similarity:** `intersection * 10000 / (union + 1)`
- **Autocorrelation:** `int64_t` 스케일링 (/ 100)

## License

All rights reserved.
