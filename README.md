# Lynrix v2.5 — AI-Powered HFT Trading Platform

**Version:** 2.5.0 "Glassmorphism 2026"  
**Platform:** macOS 13.0+ (Apple Silicon optimized)  
**Languages:** C++23 (Core) · Swift 5.9 (UI) · Objective-C++ (Bridge)  
**Optimization:** Apple M2/M3/M4 · `-mcpu=apple-m4` · ThinLTO · PGO  
**UI Design:** Glassmorphism 2026 — Deep Space Black + Neon Accents  
**Tests:** 611+ C++ tests · Swift BUILD SUCCEEDED

---

## Table of Contents

1. [Overview](#1-overview)
2. [Architecture](#2-architecture)
3. [C++ Trading Core](#3-c-trading-core)
4. [SwiftUI Frontend](#4-swiftui-frontend)
5. [ObjC++ Bridge](#5-objc-bridge)
6. [Build Instructions](#6-build-instructions)
7. [Performance](#7-performance)
8. [Testing](#8-testing)
9. [Security](#9-security)
10. [Project Structure](#10-project-structure)
11. [Changelog](#11-changelog)

---

## 1. Overview

**Lynrix** is a high-frequency trading platform for Bybit with integrated machine learning, built natively for macOS on Apple Silicon.

- **Ultra-low latency** — E2E < 100 us p99, deterministic pipeline
- **AI prediction** — CoreML ensemble + ONNX + GRU, regime-aware dynamic routing
- **Adaptive risk** — Circuit Breaker, Monte-Carlo VaR, PPO+SAC RL optimizer
- **Native macOS UI** — SwiftUI Glassmorphism with zero-tear updates via ObjC++ bridge
- **Production-grade** — Chaos testing, deterministic replay, fuzzing, 611+ tests

### Key Modules

| Module | Description |
|--------|-------------|
| **WebSocket Client** | Bybit V5 API orderbook + trades subscription |
| **OrderBook Engine** | O(1) lock-free L2 orderbook (Robin Hood hashmap, ~19 ns updates) |
| **Feature Engine** | 100+ technical indicators via vDSP/NEON SIMD (< 25 us) |
| **ML Model Engine** | CoreML ensemble (3 models, INT8) + ONNX + native GRU |
| **Regime Detector** | 5 market regimes (trending, ranging, volatile, quiet, choppy) |
| **RL Optimizer** | Hybrid PPO+SAC (32-dim state, 4-dim action, twin critics) |
| **Risk Engine** | Multi-layer: position limits, drawdown, circuit breaker, VaR |
| **Execution Engine** | Order FSM, fill probability, market impact, Iceberg/TWAP/VWAP |
| **Control Plane** | Dual FSM (Risk + Exec), overload detection, audit trail |
| **Trade Journal** | Auto-rating, CSV export, position-flip detection |
| **Chaos Engine** | 6 fault types, branchless fast path, nightly profiles |
| **Deterministic Replay** | 100% bit-exact replay with CRC32 verification |

### Technology Stack

**C++ Core:** C++23, Boost.Asio/Beast, OpenSSL 3.x, spdlog, fmt, simdjson, nlohmann/json, GoogleTest  
**Apple Frameworks:** Accelerate (vDSP/BLAS/BNNS), Metal, MetalPerformanceShaders  
**Swift UI:** Swift 5.9, SwiftUI (macOS 13+), Combine, Firebase Auth, Keychain  
**Bridge:** Objective-C++ (.mm), C API (`trading_core_api.h`)

---

## 2. Architecture

```
+-------------------------------------------------------------+
|                    SwiftUI macOS App                         |
|  +--------------+  +--------------+  +--------------+       |
|  | Dashboard    |  | AI/ML Views  |  | Risk/Exec    |       |
|  | OrderBook    |  | RL Training  |  | Trade Review  |       |
|  | Portfolio    |  | Regime Ctr   |  | Settings      |       |
|  +------+-------+  +------+-------+  +------+-------+       |
|         +----------------+-----------------+                 |
|                          v                                   |
|              +---------------------+                         |
|              |   LynrixEngine      | (ObservableObject)      |
|              |   Coalesced updates |                         |
|              |   Adaptive polling  |                         |
|              +---------+-----------+                         |
|                        v                                     |
|              +---------------------+                         |
|              |  LynrixCoreBridge   | (Objective-C++)         |
|              +---------+-----------+                         |
+------------------------+------------------------------------+
                         v
+-------------------------------------------------------------+
|                    C++ Trading Core                          |
|  +-------------------------------------------------------+  |
|  |  trading_core_api.cpp (C API for bridge)              |  |
|  +-------------------+-----------------------------------+  |
|                      v                                       |
|  +-------------------------------------------------------+  |
|  |  Application (orchestrator)                           |  |
|  |  +-> WebSocket --> OrderBook (O(1), lock-free)       |  |
|  |  +-> Features (vDSP/NEON) --> ML (CoreML ensemble)  |  |
|  |  +-> Risk (VaR/CB) --> ControlPlane (dual FSM)      |  |
|  |  +-> Execution (OSM, fill prob, market impact)       |  |
|  |  +-> RL (PPO+SAC hybrid, safe online training)       |  |
|  |  +-> Monitoring (watchdog, blackbox, chaos, replay)  |  |
|  +-------------------------------------------------------+  |
+-------------------------------------------------------------+
                         |
                         v
               +------------------+
               |  Bybit Exchange  |
               |  V5 API (WS+REST)|
               +------------------+
```

### Data Flow

```
Bybit WS -> Parser -> OrderBook -> Features -> Model -> Signal -> Risk -> Execution
   |          |          |            |          |         |        |         |
Ring Buf   SPSC Q    O(1) Robin   vDSP/NEON  CoreML   Inline  VaR/CVaR  Adaptive
           4096      Hood ~19ns    <25us      INT8     <5us   Monte-C   Routing
```

### Threading Model

| Thread | Purpose | Priority |
|--------|---------|----------|
| **Main** | SwiftUI rendering + user interaction | Normal |
| **WS** | WebSocket I/O (Boost.Asio) | P-core, High |
| **Feature** | Indicator computation (vDSP) | P-core, High |
| **Model** | ML inference (CoreML/ONNX) | P-core, High |
| **Execution** | Order placement (REST) | P-core, High |
| **RL Trainer** | Safe online PPO+SAC training | E-core, Background |
| **Monitoring** | Watchdog, histograms, blackbox | E-core, Low |

---

## 3. C++ Trading Core

### Core Benchmarks (Apple M3 Pro / M4)

```
OrderBook v3 (O(1)):
  Modify existing:    ~19 ns mean
  best_bid/ask:       ~5 ns
  imbalance(5):       ~7 ns

Execution:
  FSM transition:     ~5 ns (LUT)
  Fill prob update:   ~5 ns (EMA)
  Market impact:      ~5 ns
  Emergency cancel:   <300 us

Risk:
  VaR 10k MC:         ~735 us
  VaR 1k MC:          ~64 us

RL Inference:
  PPO+SAC action:     ~5 us (deterministic)
  State build (32d):  ~10 ns

Control Plane:
  Risk FSM apply:     ~21 ns
  Exec FSM apply:     ~24 ns
  Total overhead:     <50 ns/tick

Timing:
  TscClock::rdtsc():  ~16 ns
  Jitter enforcement: < 3 us
```

### Key Components

- **OrderBook v3** (`orderbook_v3.h`) — O(1) via Robin Hood hashmap + intrusive linked list, 500-level pool
- **Feature Engine** (`advanced_feature_engine.h`) — 100+ indicators, vDSP/NEON vectorized, <25 us
- **CoreML Inference** (`coreml_inference.h`) — 3-model ensemble, INT8, online learning, regime-aware routing
- **PPO+SAC Hybrid RL** (`ppo_sac_hybrid.h`) — 32-dim state, 4-dim action, twin critics, entropy auto-tuning
- **Safe Online Trainer** (`safe_online_trainer.h`) — Background thread, KL check, auto-rollback
- **Order State Machine** (`order_state_machine.h`) — 8-state FSM (LUT 8x10), fill tracker, iceberg/TWAP/VWAP
- **VaR Engine** (`var_engine.h`) — Parametric/Historical/Monte-Carlo, CVaR, 8 stress scenarios
- **Control Plane** (`system_control.h`) — Dual FSM, overload detection, audit trail, <50 ns overhead
- **Semantic Types** (`strong_types.h`) — Price, Qty, Notional, BasisPoints — zero-overhead type safety
- **Market Data Correctness** (`market_data.h`) — BookState FSM, gap detection, resync state machine
- **Chaos Engine** (`chaos_engine.h`) — 6 fault types, branchless fast path, nightly/flash-crash profiles
- **Deterministic Replay** (`deterministic_replay.h`) — Bit-exact replay, CRC32, configurable speed
- **Watchdog** (`watchdog.h`) — Per-stage HdrHistogram, jitter detection, hot restart

---

## 4. SwiftUI Frontend

### 17-Tab Glassmorphism Interface

| Section | Tabs |
|---------|------|
| **Trading** | Dashboard, OrderBook, Trades, Portfolio |
| **Analytics** | Strategy Performance, Risk Command Center, Execution Intelligence, Trade Review Studio |
| **Research** | RL Training, Model Governance, Market Regime Center, Strategy Research |
| **System** | System Monitor, Settings |
| **Dev Tools** | Chaos Control, Replay Control, Logs |

### Design System

- **Color Palette:** Deep Space Black (#050505), Electric Cyan (#00F0FF), Neon Lime (#39FF14), Magenta Pink (#FF10F0), Amber (#FFB000), Gold (#FFD700), Blood Red (#FF0040)
- **Components:** GlassPanel, GlassCard, NeonButton, GlowText, RegimeBadge, StatusBadge, AnimatedEquityCurve
- **Theme:** Adaptive Light/Dark/System with semantic color tokens
- **Localization:** English, Russian, Chinese Simplified (585+ keys each)
- **Typography:** SF Mono for metrics, SF Pro for labels

### UI Performance Optimizations (v2.5.1)

| Optimization | Impact |
|-------------|--------|
| **Coalesced objectWillChange** | 30+ high-churn properties moved from `@Published` to plain vars. Single `objectWillChange.send()` per poll cycle instead of 15-20 individual emissions. ~15x fewer SwiftUI invalidations. |
| **Eliminated .ultraThinMaterial** | Replaced real-time Gaussian blur with solid semi-transparent fill across 6 files. ~60-70% GPU compositing reduction per panel. |
| **Removed VisualEffectBackground** | Replaced 3 behind-window blur surfaces (main content, sidebar, status bar) with solid backgrounds. Eliminates full-surface blur compositing every frame. |
| **History array cap 500 to 200** | Reduced chart path rebuilding cost and array copy overhead for pnlHistory, drawdownHistory, accuracyHistory. |
| **Pre-computed sidebar arrays** | Static tab-by-section lookup replaces `.filter{}` allocation on every body evaluation. |
| **Adaptive polling** | 10 FPS active (position/signals), 3 FPS idle. Version-based snapshot skip prevents redundant polls. |
| **Throttled slow state** | History arrays, strategy metrics, system monitor, RL state poll every 3rd cycle (~300ms). Execution analytics every 5th (~500ms). |

See [`docs/UI_PERFORMANCE_AUDIT.md`](docs/UI_PERFORMANCE_AUDIT.md) for the full technical audit.

### State Architecture

```
LynrixEngine (ObservableObject)
|-- @Published (rare changes): status, tradingMode, killSwitch, paperMode, isReconnecting
|-- Plain vars (high-churn): orderBook, position, metrics, regime, prediction, ...
|-- Single objectWillChange.send() per poll cycle
|-- Submodels: MarketDataState, PositionState, AIState, ExecutionSubstate, StrategySubstate
+-- Adaptive polling: 100ms active / 333ms idle, version-based skip
```

---

## 5. ObjC++ Bridge

```
Swift (LynrixEngine.swift)  -->  ObjC++ (LynrixCoreBridge.mm)  -->  C API (trading_core_api.h)  -->  C++ Core
```

- **Snapshot-only boundary** — UI reads immutable snapshots, never touches C++ state directly
- **Zero-tear updates** — Triple Buffer (SeqLock) for UI snapshot publishing
- **C API** — `tc_engine_create()`, `tc_engine_start()`, `tc_get_orderbook()`, etc.
- **v2.4+ extensions** — 17 additional C functions for chaos, replay, VaR, OSM, RL state

---

## 6. Build Instructions

### Prerequisites

- macOS 13.0+ with Xcode 15+
- CMake 3.20+
- Homebrew packages: `boost openssl@3 spdlog fmt simdjson nlohmann-json googletest`
- Optional: ONNX Runtime for GPU inference

### Build C++ Core

```bash
cd /path/to/bybit
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)

# Run all tests (611+)
ctest --output-on-failure

# PGO workflow (5-15% additional speedup)
cmake .. -DENABLE_PGO_GENERATE=ON -DENABLE_LTO=OFF
cmake --build . -j8 && ./bench_orderbook_v3
cmake .. -DENABLE_PGO_GENERATE=OFF -DENABLE_PGO_USE=ON -DENABLE_LTO=ON
cmake --build . -j8
```

### Build SwiftUI App

```bash
cd Lunrix
xcodebuild -project Lynrix.xcodeproj -scheme Lynrix -configuration Debug -destination 'platform=macOS' build
open ~/Library/Developer/Xcode/DerivedData/Lynrix-*/Build/Products/Debug/Lynrix.app
```

### CPU Isolation (before trading)

```bash
sudo ./scripts/isolate_cores.sh $(pgrep lynrix_hft)
```

---

## 7. Performance

### C++ Core Latency (Apple M3 Pro)

| Metric | v1.x | v2.5 | Improvement |
|--------|------|------|-------------|
| **E2E Latency p50** | ~800 us | **< 80 us** | **10x** |
| **E2E Latency p99** | ~2,500 us | **< 200 us** | **12.5x** |
| **OB Delta Update** | ~15 us | **~19 ns** | **800x** |
| **Feature Calc** | ~80 us | **< 25 us** | **3.2x** |
| **ML Inference** | ~500 us | **< 120 us** | **4x** |
| **Hot-path allocs** | ~2 MB/s | **0 alloc/s** | **zero** |

### UI Performance (MacBook Air M2, 60 Hz)

| Metric | Before v2.5.1 | After v2.5.1 |
|--------|---------------|--------------|
| **objectWillChange/poll** | 15-20 emissions | **1 emission** |
| **GPU blur surfaces** | 3 full-surface + per-panel | **0 (solid fills)** |
| **Chart data points** | 500 | **200** |
| **Sidebar filter allocs** | Per-render | **Static (zero)** |

---

## 8. Testing

### C++ Test Suites (611+ tests)

```bash
./test_orderbook             # 23 tests - OB v2
./test_orderbook_v3          # 26 tests - OB v3
./test_order_state_machine   # 55 tests - FSM, VaR, impact
./test_chaos_engine          # 33 tests - Chaos + signposts
./test_deterministic_replay  # 27 tests - Replay + CRC32
./test_stage3_md_correctness # 33 tests - Market data correctness
./test_stage5_semantic_types # 55 tests - Semantic type safety
./test_stage6_system_control # 83 tests - Control plane FSMs
./test_ppo_sac_hybrid        # 39 tests - RL agent + trainer
./test_all_modules           # 69 tests
./test_ai_modules            # 61 tests
./test_comprehensive         # 55 tests
./test_config_persistence    # 12 tests
```

### Benchmarks

```bash
./bench_orderbook_v3    # OB v3 latency
./bench_tsc             # TSC vs std::chrono
./bench_execution       # Execution engine
./bench_rl              # RL inference + training
./bench_stage6_control  # Control plane overhead
./bench_features        # Feature engine E2E
```

### Fuzzing

```bash
cmake .. -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build . --target fuzz_orderbook_parser -j8
./fuzz_orderbook_parser corpus/orderbook -max_len=4096 -max_total_time=600
```

---

## 9. Security

- **Keychain** — API keys stored in macOS Keychain (Security.framework), never on disk
- **Firebase Auth** — Google Sign-In + Email/Password, unified session management
- **Snapshot boundary** — UI cannot mutate C++ engine state; read-only snapshots only
- **Config validation** — Real-time validation with severity levels before engine start
- **Kill switches** — Global and per-strategy halt, keyboard shortcut (Shift+Cmd+K)
- **No secrets in repo** — API keys loaded at runtime from Keychain

---

## 10. Project Structure

```
bybit/
+-- src/                          # C++ Trading Core
|   +-- app/                      #   Application orchestrator
|   +-- bridge/                   #   C API for Swift bridge
|   +-- config/                   #   Config loader + types
|   +-- core/                     #   Strong types, market data, system control
|   +-- execution_engine/         #   Smart execution, order FSM
|   +-- feature_engine/           #   100+ indicators, SIMD
|   +-- metrics/                  #   Latency histogram
|   +-- model_engine/             #   CoreML, ONNX, GRU
|   +-- monitoring/               #   Watchdog, chaos, replay, blackbox, signpost
|   +-- networking/               #   WebSocket manager
|   +-- orderbook/                #   OB v2 (production) + v3 (O(1))
|   +-- persistence/              #   Async writer, research recorder
|   +-- portfolio/                #   Position + PnL tracking
|   +-- regime/                   #   Market regime detector
|   +-- rest_client/              #   HTTP REST client
|   +-- risk_engine/              #   Risk engine + VaR
|   +-- rl/                       #   PPO+SAC hybrid, safe trainer
|   +-- strategy/                 #   Threshold, position sizer, fill prob
|   +-- trade_flow/               #   Trade flow analysis
|   +-- utils/                    #   Arena, SPSC, TSC clock, thread affinity
+-- tests/                        # 611+ C++ tests + benchmarks + fuzzer
+-- Lunrix/                       # SwiftUI macOS App
|   +-- App/                      #   LynrixApp, ContentView
|   +-- Bridge/                   #   ObjC++ bridge (LynrixCoreBridge)
|   +-- Models/                   #   LynrixEngine, submodels, trade journal
|   +-- Views/                    #   17 view files
|   +-- Components/               #   GlassPanel, theme, animations
|   +-- Resources/                #   Localization (en/ru/zh-Hans), assets
|   +-- Lynrix.xcodeproj/        #   Xcode project
+-- docs/                         # Architecture docs, audit reports
+-- scripts/                      # CPU isolation, build helpers
+-- CMakeLists.txt                # C++ build (v2.5)
```

---

## 11. Changelog

### v2.5.1 — UI Performance Optimization (March 2026)

- **P0: Coalesced state updates** — Moved 30+ high-churn `@Published` properties to plain vars. Single `objectWillChange.send()` per poll cycle. Manual notifications for addLog/addSignal/addTrade.
- **P1: Eliminated GPU blur** — Replaced `.ultraThinMaterial` with solid `Color(white: 0.11, opacity: 0.92)` across GlassPanel, GlassCard, GlassModifier, OrderBookView, ChaosControlView, DashboardView.
- **P1: Removed VisualEffectBackground** — Replaced 3 NSVisualEffectView surfaces (main content, sidebar, status bar) with solid theme backgrounds.
- **P2: History cap 500 to 200** — Reduced pnlHistory, drawdownHistory, accuracyHistory max size.
- **P3: Pre-computed sidebar** — Static `tabsBySection` dictionary replaces per-render `.filter{}`.
- **Audit:** Full technical audit in `docs/UI_PERFORMANCE_AUDIT.md`.

### v2.5 — Glassmorphism 2026 + Sprint 4

- Trade Review Studio with auto-rating and journal
- Execution Intelligence Center (scoring, slippage, latency decomposition)
- Risk Command Center with kill switches and VaR summary
- Incident Center with timeline and severity filtering
- Merged views (Dashboard+Signals, Risk+VaR, Execution+OSM, SystemMonitor+Incidents)
- Onboarding wizard, config profiles, notification system
- Firebase Auth + Firestore user profiles
- Full localization (EN/RU/ZH-Hans, 585+ keys)

### v2.4 — The Ideal Edition

- Chaos Engine (6 fault types), Deterministic Replay, Fuzzer
- os_signpost integration, per-stage HdrHistogram
- BlackBox Recorder v2 with CRC32

### v2.3 — Production-Grade RL and ML

- Hybrid PPO+SAC (32-dim state, twin critics, entropy auto-tuning)
- Safe Online Trainer (background thread, KL check, auto-rollback)
- Dynamic model routing (regime-aware CoreML/GRU selection)

### v2.2 — Execution and Risk

- Order State Machine (8-state FSM, LUT transitions)
- Monte-Carlo VaR Engine (10k scenarios, CVaR, stress tests)
- Market impact model, Iceberg/TWAP/VWAP, emergency cancel <300 us

### v2.1 — Deterministic Latency

- O(1) OrderBook v3 (Robin Hood hashmap, ~19 ns)
- TSC-only timing, thread affinity, branchless LUTs
- Jitter enforcement < 3 us

### v2.0 — Core Refactoring

- Arena allocator, SPSC queues, Triple Buffer
- vDSP/NEON SIMD indicators, CoreML ensemble
- PPO actor-critic RL, HdrHistogram, Watchdog

---

**License:** Proprietary  
**Author:** Slava Ivanov  
**Repository:** [github.com/vasilisaofficialyeeet-star/Lynrix](https://github.com/vasilisaofficialyeeet-star/Lynrix.git)
