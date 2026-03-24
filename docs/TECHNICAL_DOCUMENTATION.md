# Lynrix v2.5 — Full Technical Documentation

> **High-Precision AI Trading Engine for Bybit Linear Perpetuals**
>
> Platform: macOS (Apple Silicon M1–M4) · Language: C++23 / Swift 5.9 / Objective-C++
> Build: CMake 3.20+ / Xcode 15+ · License: Proprietary

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Project Structure](#2-project-structure)
3. [C++ Trading Engine](#3-c-trading-engine)
   - 3.1 [Application Lifecycle](#31-application-lifecycle)
   - 3.2 [Core Primitives](#32-core-primitives)
   - 3.3 [Order Book (L2)](#33-order-book-l2)
   - 3.4 [Feature Engine](#34-feature-engine)
   - 3.5 [Model Engine (ML/AI)](#35-model-engine-mlai)
   - 3.6 [Regime Detection](#36-regime-detection)
   - 3.7 [Execution Engine](#37-execution-engine)
   - 3.8 [Risk Engine](#38-risk-engine)
   - 3.9 [VaR Engine](#39-var-engine)
   - 3.10 [Reinforcement Learning](#310-reinforcement-learning)
   - 3.11 [Networking Layer](#311-networking-layer)
   - 3.12 [Monitoring & Diagnostics](#312-monitoring--diagnostics)
   - 3.13 [Persistence & Research](#313-persistence--research)
   - 3.14 [Control-Plane FSMs](#314-control-plane-fsms)
4. [ObjC++ Bridge Layer](#4-objc-bridge-layer)
   - 4.1 [C API (trading_core_api.h)](#41-c-api)
   - 4.2 [UI Snapshot System](#42-ui-snapshot-system)
   - 4.3 [ObjC Bridge (LynrixCoreBridge)](#43-objc-bridge)
5. [macOS SwiftUI Frontend (Lunrix)](#5-macos-swiftui-frontend-lunrix)
   - 5.1 [App Architecture](#51-app-architecture)
   - 5.2 [LynrixEngine (Swift)](#52-lynrixengine-swift)
   - 5.3 [Views & Components](#53-views--components)
   - 5.4 [Theme System](#54-theme-system)
   - 5.5 [Localization](#55-localization)
   - 5.6 [Authentication & Profiles](#56-authentication--profiles)
   - 5.7 [Command Palette & Keyboard Shortcuts](#57-command-palette--keyboard-shortcuts)
   - 5.8 [Toast Notifications](#58-toast-notifications)
6. [Configuration](#6-configuration)
7. [Build System](#7-build-system)
8. [Testing](#8-testing)
9. [Performance Benchmarks](#9-performance-benchmarks)
10. [Security](#10-security)
11. [Deployment](#11-deployment)

---

## 1. Architecture Overview

Lynrix is a **dual-process** trading system:

```
┌─────────────────────────────────────────────────────────────────────┐
│                     macOS Application (Lunrix)                      │
│  ┌──────────────────┐    ┌───────────────────────────────────────┐  │
│  │   SwiftUI Views   │◄──│  LynrixEngine (ObservableObject)      │  │
│  │   17 sidebar tabs  │   │  - Polls UISnapshot via SeqLock       │  │
│  │   Glassmorphism UI │   │  - @Published state properties        │  │
│  └──────────────────┘    │  - Adaptive 10-100 FPS polling         │  │
│                           └───────────────┬───────────────────────┘  │
│                                           │ ObjC++ Bridge            │
│  ┌────────────────────────────────────────▼───────────────────────┐  │
│  │              LynrixCoreBridge.mm (Objective-C++)                │  │
│  │  TCConfigObjC → TCConfig → EngineWrapper → bybit::Application  │  │
│  └────────────────────────────────────────┬───────────────────────┘  │
│                                           │ Pure C API               │
│  ┌────────────────────────────────────────▼───────────────────────┐  │
│  │                 C++ Trading Engine (liblynrix)                  │  │
│  │  ┌─────┐  ┌─────┐  ┌──────┐  ┌─────┐  ┌──────┐  ┌──────────┐ │  │
│  │  │ WS  │→│ OB  │→│ Feat │→│ ML  │→│ Risk │→│ Execution│ │  │
│  │  │ Mgr │  │ v2  │  │ Eng  │  │ Eng │  │ Eng  │  │ Engine   │ │  │
│  │  └─────┘  └─────┘  └──────┘  └─────┘  └──────┘  └──────────┘ │  │
│  │                     Hot Path: ~3-5 µs E2E                       │  │
│  └─────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
                    │                               │
                    ▼                               ▼
        Bybit WebSocket API              Bybit REST / WS Trade API
        (orderbook, trades)              (order placement, cancel)
```

**Key design principles:**
- **Zero-allocation hot path** — no heap, no logging, no exceptions in the signal pipeline
- **Lock-free inter-thread communication** — SeqLock for UI snapshot, SPSC queues for events
- **Apple Silicon native** — NEON SIMD, Accelerate framework, TSC clock, P/E-core affinity
- **Snapshot-only bridge** — C++ engine is fully autonomous; Swift polls read-only snapshots

---

## 2. Project Structure

```
bybit/
├── main.cpp                          # CLI entry point (standalone C++ engine)
├── config.json                       # Default configuration
├── CMakeLists.txt                    # C++ build (v2.5.0, C++23, Apple Silicon)
│
├── src/                              # C++ trading engine
│   ├── app/                          # Application orchestrator
│   │   ├── application.h             # Main event loop, component wiring
│   │   ├── strategy_pipeline.inl     # Hot-path strategy tick pipeline
│   │   ├── timer_manager.inl         # Periodic timer management
│   │   ├── reconciliation.inl        # Position reconciliation
│   │   └── ui_publisher.inl          # UISnapshot publication
│   ├── core/                         # Foundational primitives
│   │   ├── strong_types.h            # Price, Qty, Notional, TimestampNs, etc.
│   │   ├── hot_path.h                # Hot-path contract, annotations, pipeline stages
│   │   ├── system_control.h          # Control-plane FSMs (Risk, Exec, System)
│   │   ├── market_data.h             # BookState, DeltaResult, SequenceGapInfo
│   │   ├── memory_policy.h           # Memory budget, watermark tracking
│   │   ├── clock_source.h            # Clock abstraction
│   │   └── instrument_manager.h      # Instrument metadata (tick/lot size)
│   ├── orderbook/                    # L2 order book
│   │   ├── orderbook.h               # Production OB v2 (binary search, NEON)
│   │   └── orderbook_v3.h            # OB v3 (Robin Hood hash, intrusive list)
│   ├── feature_engine/               # Real-time feature extraction
│   │   ├── advanced_feature_engine.h # 25 features + temporal ring buffer
│   │   └── simd_indicators.h         # vDSP/NEON vectorized math
│   ├── model_engine/                 # ML inference
│   │   ├── gru_model.h               # Native GRU cell (fallback)
│   │   ├── onnx_inference.h          # ONNX Runtime (production)
│   │   ├── coreml_inference.h        # CoreML ensemble (3 models)
│   │   └── accuracy_tracker.h        # Online accuracy monitoring
│   ├── execution_engine/             # Order execution
│   │   ├── smart_execution.h         # Smart Execution Engine v3
│   │   ├── order_state_machine.h     # 8-state FSM, iceberg, TWAP/VWAP
│   │   └── paper_fill_simulator.h    # Realistic paper fill simulation
│   ├── risk_engine/                  # Risk management
│   │   ├── enhanced_risk_engine.h    # Circuit breaker, position limits
│   │   └── var_engine.h              # VaR (parametric, historical, Monte-Carlo)
│   ├── regime/                       # Market regime detection
│   │   └── regime_detector.h         # 5-regime classifier (k-means online)
│   ├── strategy/                     # Trading strategy components
│   │   ├── adaptive_threshold.h      # Dynamic signal threshold
│   │   ├── fill_probability.h        # Fill probability model
│   │   └── adaptive_position_sizer.h # Regime-aware position sizing
│   ├── rl/                           # Reinforcement learning
│   │   ├── rl_optimizer.h            # PPO actor-critic optimizer
│   │   └── ppo_actor_critic.h        # PPO network (16-dim state, 4-dim action)
│   ├── networking/                   # WebSocket management
│   │   ├── ws_manager.h              # Public/private WS, resync state machine
│   │   └── ws_trade_client.h         # WS Trade API (low-latency order submission)
│   ├── rest_client/                  # REST API client
│   │   └── rest_client.h             # HMAC-SHA256 authenticated REST
│   ├── trade_flow/                   # Trade flow analysis
│   │   └── trade_flow_engine.h       # Aggression ratio, velocity, acceleration
│   ├── portfolio/                    # Portfolio state
│   │   └── portfolio.h               # Position tracking, PnL, funding
│   ├── analytics/                    # Strategy analytics
│   │   ├── strategy_metrics.h        # Sharpe, Sortino, drawdown, profit factor
│   │   ├── strategy_health.h         # Real-time strategy health scoring
│   │   └── feature_importance.h      # SHAP-like feature contribution
│   ├── monitoring/                   # System monitoring
│   │   ├── watchdog.h                # Heartbeat + jitter auto-restart
│   │   ├── hdr_histogram.h           # Lock-free HdrHistogram
│   │   ├── blackbox_recorder.h       # 64K-event ring buffer
│   │   └── system_monitor.h          # CPU, memory, thread monitoring
│   ├── persistence/                  # Data recording
│   │   └── research_recorder.h       # Async ML training data recorder
│   ├── metrics/                      # Latency metrics
│   │   └── latency_histogram.h       # Per-stage latency + counters
│   ├── config/                       # Configuration
│   │   ├── types.h                   # AppConfig, enums, branchless LUTs
│   │   └── config_loader.h           # JSON config loading
│   ├── bridge/                       # Swift interop
│   │   ├── trading_core_api.h        # Pure C API (opaque handle)
│   │   ├── trading_core_api.cpp      # C API implementation
│   │   └── ui_snapshot.h             # UISnapshot struct + SeqLock publisher
│   └── utils/                        # Shared utilities
│       ├── tsc_clock.h               # mach_absolute_time TSC clock (~2 ns)
│       ├── arena_allocator.h         # mmap-backed bump-pointer arena
│       ├── lockfree_pipeline.h       # SPSC queue, triple buffer, SeqLock
│       ├── thread_affinity.h         # P/E-core affinity, real-time QoS
│       ├── ring_buffer.h             # Fixed-size ring buffer
│       └── fast_double.h             # Fast double parsing
│
├── Lunrix/                           # macOS SwiftUI application
│   ├── App/
│   │   ├── LunrixApp.swift           # @main entry, environment setup
│   │   └── ContentView.swift         # Root view, sidebar, keyboard shortcuts
│   ├── Bridge/
│   │   ├── LynrixCoreBridge.h        # ObjC header (TCConfigObjC, delegates)
│   │   └── LynrixCoreBridge.mm       # ObjC++ implementation (C++ ↔ ObjC)
│   ├── Models/                       # Swift models & managers
│   │   ├── LunrixEngine.swift        # Main engine wrapper (ObservableObject)
│   │   ├── EngineSubmodels.swift     # PositionState, pnlHistory, drawdown
│   │   ├── TradeJournal.swift        # Trade recording, auto-rating, CSV export
│   │   ├── ExecutionIntelligence.swift  # Execution quality analytics
│   │   ├── MarketRegimeIntelligence.swift # Regime analysis
│   │   ├── AllocationIntelligence.swift   # Portfolio allocation
│   │   ├── ModelGovernance.swift     # ML model lifecycle management
│   │   ├── ProfileManager.swift      # Trading config profiles
│   │   ├── KeychainManager.swift     # Secure API key storage
│   │   ├── AuthManager.swift         # Firebase authentication
│   │   ├── NotificationManager.swift # macOS notification manager
│   │   ├── WidgetLayoutManager.swift # Dashboard widget layout
│   │   ├── OnboardingManager.swift   # First-run onboarding
│   │   ├── TradingMode.swift         # Paper/live mode management
│   │   └── UserProfile.swift         # User profile model
│   ├── Views/                        # 29 SwiftUI views
│   │   ├── DashboardView.swift       # Main dashboard with widgets
│   │   ├── AIDashboardView.swift     # AI model monitoring
│   │   ├── OrderBookView.swift       # Real-time L2 orderbook
│   │   ├── PortfolioView.swift       # Position & PnL
│   │   ├── RiskCommandCenterView.swift    # Risk management center
│   │   ├── VarDashboardView.swift    # VaR visualization
│   │   ├── ExecutionIntelligenceView.swift # Execution analytics
│   │   ├── MarketRegimeCenterView.swift    # Regime detection dashboard
│   │   ├── TradeReviewStudioView.swift     # Trade journal & review
│   │   ├── StrategyPerformanceView.swift   # Strategy metrics
│   │   ├── StrategyResearchView.swift      # Research & backtesting
│   │   ├── SystemMonitorView.swift   # System diagnostics
│   │   ├── SettingsView.swift        # Settings (4 tabs: General/Trading/AI/Prefs)
│   │   ├── RLTrainingView.swift      # RL training visualization
│   │   ├── ModelGovernanceView.swift  # Model governance dashboard
│   │   └── ... (14 more views)
│   ├── Components/                   # Reusable UI components
│   │   ├── GlassPanel.swift          # Glassmorphism panel
│   │   ├── NeonButton.swift          # Neon-glow button
│   │   ├── GlowText.swift            # Glowing text
│   │   ├── AnimatedEquityCurve.swift # Equity curve chart
│   │   ├── PnlSparkline.swift        # Compact PnL sparkline
│   │   ├── CommandPalette.swift      # Cmd+K command palette
│   │   ├── ToastNotification.swift   # Toast notification system
│   │   ├── UnifiedStatusBar.swift    # Bottom status bar
│   │   ├── LxTheme.swift             # Theme colors, fonts, materials
│   │   ├── ThemeManager.swift        # Appearance mode management
│   │   └── LocalizationManager.swift # i18n manager
│   ├── Resources/                    # Localization
│   │   ├── en.lproj/Localizable.strings
│   │   ├── ru.lproj/Localizable.strings
│   │   └── zh-Hans.lproj/Localizable.strings
│   └── Lynrix.xcodeproj/            # Xcode project
│
├── tests/                            # C++ test suites
│   ├── test_order_state_machine.cpp  # 55 tests (FSM, VaR, iceberg, TWAP)
│   ├── test_stage3_md_correctness.cpp # 33 tests (gap detection, resync)
│   ├── test_all_modules.cpp          # 69 integration tests
│   └── ... (8 test suites, 388+ total tests)
│
├── docs/                             # Documentation
├── ml/                               # ML model files
├── logs/                             # Runtime logs
└── scripts/                          # Utility scripts
    └── isolate_cores.sh              # macOS CPU isolation
```

---

## 3. C++ Trading Engine

### 3.1 Application Lifecycle

**Entry point:** `src/app/application.h` → `class Application`

The application orchestrates the full trading pipeline:

```
Application::run()
  ├── setup_logging()              — spdlog: console + rotating file
  ├── setup_signal_handling()      — SIGINT/SIGTERM via self-pipe
  ├── recorder_.start()            — async research data recording
  ├── ws_mgr_.start()              — WebSocket connections (public + private)
  ├── ws_trade_.start()            — WS Trade API (live mode only)
  ├── setup_strategy_tick()        — event-driven: OB update → strand → strategy_tick()
  ├── start_fallback_timer()       — 10ms fallback if no WS updates
  ├── ioc_.run()                   — boost::asio event loop (N IO threads)
  └── shutdown()                   — graceful stop, cancel orders, flush logs
```

**Strategy tick pipeline** (hot path, `strategy_pipeline.inl`):

```
strategy_tick() → ~3-5 µs total
  ├── [1] OB validation + best_bid/ask extraction        ~5 ns
  ├── [2] Feature computation (25 features)               ~200 ns
  ├── [3] Regime detection                                ~50 ns
  ├── [4] ML inference (ONNX or native GRU)               ~500 ns - 2 µs
  ├── [5] Signal generation + adaptive threshold           ~20 ns
  ├── [6] Risk check (pre-trade)                          ~30 ns
  ├── [7] Position sizing (regime-aware)                   ~15 ns
  ├── [8] Execution (limit/market, paper/live)             ~100 ns
  └── [9] UISnapshot publish via SeqLock                   ~10 ns
```

### 3.2 Core Primitives

**Strong types** (`core/strong_types.h`):
- `Price`, `Qty`, `Notional`, `BasisPoints` — prevent unit confusion at compile time
- `TimestampNs` (uint64), `DurationNs` (int64) — monotonic timing
- `SequenceNumber` — typed sequence IDs for gap detection
- Cross-type operations via explicit free functions: `notional(Price, Qty) → Notional`

**Hot-path contract** (`core/hot_path.h`):
- `BYBIT_HOT` — function attribute for hot-path code
- `HotResult` — replaces exceptions (static error strings only)
- `ScopedStageTimer` — per-stage latency measurement via TSC
- Rules: no heap allocation, no logging, no exceptions, no virtual dispatch, no mutexes

**Memory policy** (`core/memory_policy.h`):
- Memory budget tracking per subsystem
- Watermark monitoring for arena allocators
- Cache-line alignment enforcement (128 bytes for Apple Silicon)

**Clock** (`utils/tsc_clock.h`):
- `mach_absolute_time()` → single ARM instruction read of CNTPCT_EL0
- ~2 ns overhead (vs ~20-50 ns for `std::chrono::steady_clock`)
- `ScopedTscTimer` for automatic scope timing

### 3.3 Order Book (L2)

**Production: `orderbook/orderbook.h` (v2)**
- Fixed-point price representation (`FixedPrice` = int64, `price * 1e8`)
- `CompactLevel` = 16 bytes (FixedPrice + double qty), 8 levels per cache line
- Binary search O(log n) delta updates
- NEON-vectorized imbalance and VWAP computation
- 500-level depth per side (configurable)
- Stage 3 market-data correctness:
  - `BookState` enum: Empty, Valid, InvalidGap, InvalidDisconnect, PendingResync
  - Typed `SequenceNumber` with strict gap detection (`seq > book.seq + 1`)
  - `MDIngressCounters` — diagnostic counters for deltas, gaps, invalidations

**Benchmark:** OB modify existing ~19 ns, best_bid/ask ~5 ns, imbalance(5) ~7 ns

### 3.4 Feature Engine

**`feature_engine/advanced_feature_engine.h`** — 25 real-time features:

| Category | Features | Count |
|----------|----------|-------|
| **Order Book** | imbalance(1/5/20), slope, depth_concentration, cancel_spike, liquidity_wall | 7 |
| **Trade Flow** | aggression_ratio, avg_trade_size, velocity, acceleration, volume_accel | 5 |
| **Price** | microprice, spread_bps, spread_change_rate, mid_momentum, volatility | 5 |
| **Derived** | microprice_dev, short_term_pressure, bid/ask_depth_total | 4 |
| **Temporal** | d_imbalance_dt, d²_imbalance_dt², d_volatility_dt, d_momentum_dt | 4 |

**SIMD acceleration** (`simd_indicators.h`):
- `vDSP_sveD` — vectorized sum
- `vDSP_meanvD` — vectorized mean
- `vDSP_normalizeD` — vectorized variance
- NEON intrinsics for EMA, batch sigmoid/tanh, matrix-vector multiply

**Temporal ring buffer** stores last `FEATURE_SEQ_LEN` (default 100) snapshots for sequence-based ML models and derivative computation.

### 3.5 Model Engine (ML/AI)

Three inference backends, in order of preference:

1. **ONNX Runtime** (`model_engine/onnx_inference.h`) — **production path**
   - Input: `[1, seq_len, 25]` float32 tensor
   - Output: `[1, NUM_HORIZONS * 3]` — (up, down, flat) probabilities per horizon
   - Backends: CPU, CUDA, TensorRT, CoreML
   - Target: < 1 ms inference for 100-step sequence

2. **CoreML Ensemble** (`model_engine/coreml_inference.h`)
   - 3-model ensemble with weighted averaging
   - INT8 quantization, online learning, hot reload
   - Branchless `REGIME_ENSEMBLE_WEIGHTS` 2D LUT

3. **Native GRU** (`model_engine/gru_model.h`) — fallback/demo only
   - Hand-coded GRU cell (C++, no external deps)
   - `GRUCell<FEATURE_COUNT, GRU_HIDDEN_SIZE>`
   - Xavier random init (meaningless predictions unless trained offline)

**Accuracy tracker** (`model_engine/accuracy_tracker.h`):
- Online accuracy monitoring across time horizons
- Directional accuracy, calibration error, Brier score

### 3.6 Regime Detection

**`regime/regime_detector.h`** — 5-regime online classifier:

| Regime | Characteristics | Strategy Adjustment |
|--------|----------------|---------------------|
| **LowVolatility** | Tight spread, low vol | Tighter threshold (0.55), full size |
| **HighVolatility** | Wide spread, high vol | Wide threshold (0.70), half size |
| **Trending** | Sustained direction | Follow momentum (0.50), 1.2x size |
| **MeanReverting** | Oscillation | Moderate threshold (0.65), 0.8x size |
| **LiquidityVacuum** | Thin book, wide spread | Very conservative (0.80), 0.3x size |

Implementation: rolling EMA statistics (volatility, momentum, spread, depth) + autocorrelation + k-means clustering. Branchless regime-dependent parameters via compile-time LUTs.

### 3.7 Execution Engine

**`execution_engine/smart_execution.h`** — v3:

- **Order State Machine** — 8-state FSM with compile-time LUT (transition ~5 ns)
  - States: Idle, PendingNew, Open, PendingAmend, PendingCancel, Filled, Cancelled, Rejected
  - 10 events: NewAck, Fill, PartialFill, CancelAck, AmendAck, Reject, Timeout, etc.
- **Fill probability tracking** — 32-band EMA per price distance from mid
- **Adaptive cancel** — drift detection, cooldown, automatic cancel/replace
- **Iceberg orders** — hidden slice management with auto-refill
- **TWAP/VWAP scheduling** — TSC-timed slice execution
- **Market impact model** — Almgren-Chriss sqrt-law for slippage estimation
- **Emergency cancel** — batch REST cancel < 300 µs
- **Paper fill simulator** (`paper_fill_simulator.h`):
  - Queue position model, slippage (normal distribution), partial fills
  - **R3: Paper fill gate** — configurable `paper_fill_rate` (0.0–1.0) probability gate
- **Order routing**: PostOnly (passive) vs IOC (aggressive) decision < 5 µs
- **WS Trade API**: low-latency order submission via WebSocket (live mode)

### 3.8 Risk Engine

**`risk_engine/enhanced_risk_engine.h`**:

- **Pre-trade risk checks**: position limit, leverage limit, order rate limit
- **Circuit breaker** (`CircuitBreaker`):
  - Trip conditions: daily loss, drawdown, consecutive losses, loss rate ($/min)
  - O(1) loss-rate computation via sliding-window ring buffer
  - TSC-timed `is_tripped(now_ns)` — no syscall on hot path
  - Configurable cooldown period
- **Regime-aware position scaling**: branchless LUT `REGIME_POSITION_SCALE_LUT`

### 3.9 VaR Engine

**`risk_engine/var_engine.h`** — real-time Value at Risk:

- **Parametric VaR** — variance-covariance (single-asset)
- **Historical VaR** — from 4096-entry rolling return ring buffer
- **Monte-Carlo VaR** — 10k scenarios in ~735 µs (NEON-vectorized)
  - xoshiro256+ PRNG, Box-Muller normal generation
  - `vDSP_vsortD` vectorized percentile extraction
- **CVaR** (Expected Shortfall) at 95% and 99%
- **8 stress scenarios**: fat-tail (Student-t), regime-conditional

### 3.10 Reinforcement Learning

**`rl/rl_optimizer.h` + `rl/ppo_actor_critic.h`**:

- **PPO (Proximal Policy Optimization)** actor-critic
- **State space** (10 dims): volatility, spread, liquidity, confidence, PnL, drawdown, win rate, Sharpe, fill rate, regime stability
- **Action space** (4 dims): signal threshold delta, position size scale, order offset bps, requote frequency scale
- **Training**: online experience collection, GAE-λ advantage estimation, clipped objective
- **Inference**: actor network outputs continuous actions, clipped to safe ranges

### 3.11 Networking Layer

**WebSocket Manager** (`networking/ws_manager.h`):
- **Public WS**: orderbook (500-level delta), trades, instrument info
- **Private WS**: position, wallet, execution updates (authenticated HMAC)
- Resync state machine: Normal → PendingResync (on gap) → Normal (on snapshot)
- Exponential backoff reconnection (1s–30s)
- Ping/pong keep-alive (20s interval, 30s stale timeout)
- `simdjson` for zero-copy JSON parsing

**WS Trade Client** (`networking/ws_trade_client.h`):
- Low-latency order submission via Bybit WS Trade API
- Live mode only (paper mode skips)

**REST Client** (`rest_client/rest_client.h`):
- HMAC-SHA256 authenticated requests
- Order placement, amendment, cancellation
- Instrument info, position queries
- Configurable timeout (5s default), retry (3x)

### 3.12 Monitoring & Diagnostics

| Component | File | Function |
|-----------|------|----------|
| **Watchdog** | `monitoring/watchdog.h` | 8-stage heartbeat monitoring, jitter detection (>3µs → restart), E-core affinity |
| **HdrHistogram** | `monitoring/hdr_histogram.h` | Lock-free O(1) record, percentile extraction |
| **BlackBox Recorder** | `monitoring/blackbox_recorder.h` | 64K-event ring buffer (64B events) |
| **System Monitor** | `monitoring/system_monitor.h` | CPU, memory, thread count, FD usage |
| **Latency Histogram** | `metrics/latency_histogram.h` | Per-stage latency + atomic counters for seq gaps, OB invalidations, dropped deltas |

### 3.13 Persistence & Research

**`persistence/research_recorder.h`**:
- Async recording via lock-free ring buffer (32768 entries)
- Record types: Trade, Feature, Signal, PnL, OB Snapshot, Model Prediction, Regime Change, Order Event
- Background flush thread (configurable `batch_flush_ms`)
- Output: binary records for ML training pipeline

### 3.14 Control-Plane FSMs

**`core/system_control.h`** — branchless LUT state machines:

**RiskControlFSM** (5 states × 10 events):
- Normal → Cautious → Restricted → CircuitBreaker → Halted
- Events: PnlNormal, DrawdownWarning/Breached, ConsecutiveLosses, LossRateExceeded, CooldownExpired, ManualReset/Halt

**ExecControlFSM** (6 states × 9 events):
- Normal → ReduceOnly → CancelOnly → EmergencyCancel → Paused → Shutdown

**SystemModeResolver**: combines Risk + Exec + Health FSMs into unified operating mode.

**ControlAuditTrail**: fixed-size ring of timestamped transition records, zero-allocation.

---

## 4. ObjC++ Bridge Layer

### 4.1 C API

**`bridge/trading_core_api.h`** — pure C interface (no C++ types exposed):

```c
// Lifecycle
TCEngineHandle tc_engine_create(const TCConfig* config);
void           tc_engine_destroy(TCEngineHandle engine);
bool           tc_engine_start(TCEngineHandle engine);
void           tc_engine_stop(TCEngineHandle engine);
void           tc_engine_set_api_key(TCEngineHandle engine, const char* key, const char* secret);

// State query (snapshot-based, lock-free)
TCEngineStatus tc_engine_get_status(TCEngineHandle engine);
bool           tc_engine_get_snapshot(TCEngineHandle engine, TCSnapshot* out);
uint64_t       tc_engine_get_snapshot_version(TCEngineHandle engine);

// Runtime config (atomic writes)
void tc_engine_set_paper_mode(TCEngineHandle engine, bool paper);
void tc_engine_set_signal_threshold(TCEngineHandle engine, double threshold);
void tc_engine_set_order_qty(TCEngineHandle engine, double qty);
// ... 15+ more runtime parameters

// Control
void tc_engine_emergency_stop(TCEngineHandle engine);
void tc_engine_reset_circuit_breaker(TCEngineHandle engine);
```

**TCConfig** struct maps 1:1 to `AppConfig` with C-safe types (no `std::string`).

**TCSnapshot** — flat C struct (~4KB) containing complete engine state:
- Order book (20 bids + 20 asks)
- Position (size, entry, PnL, funding)
- Metrics (counters, latency percentiles)
- Features (25 doubles)
- Regime, prediction, threshold, execution stats
- Strategy metrics, health, RL state, system monitor
- Control-plane state (risk, exec, system FSMs)

### 4.2 UI Snapshot System

**`bridge/ui_snapshot.h`** — SeqLock-based lock-free data transfer:

```
C++ Engine Thread          SeqLock (UISnapshot ~4KB)          Swift UI Thread
    │                                                              │
    ├─ strategy_tick() ──► publish_snapshot() ─┐                   │
    │                      (memcpy under seq)  │                   │
    │                                          └──► read attempt ──┤
    │                                              (retry if torn) │
    │                                                              ▼
    │                                                  LynrixEngine polls
    │                                                  at 10-100 FPS
```

The SeqLock uses `std::memory_order_release/acquire` fences. Readers retry if the sequence counter is odd (write in progress) or changed between start/end of read.

### 4.3 ObjC Bridge

**`Bridge/LynrixCoreBridge.mm`** — Objective-C++ adapter:

- `TCConfigObjC` — NSObject with properties mapping to `TCConfig`
- `LynrixCoreBridge` — wraps `tc_engine_*` C API calls
  - `initWithConfig:` → `tc_engine_create()`
  - `start` / `stop` / `emergencyStop`
  - `getSnapshot` → reads `TCSnapshot` and converts to ObjC-compatible types
  - `setAPIKey:secret:` — D-F1: unified key loading
  - Delegate pattern (`LynrixCoreBridgeDelegate`) for async callbacks

---

## 5. macOS SwiftUI Frontend (Lunrix)

### 5.1 App Architecture

```swift
@main struct LunrixApp: App {
    @StateObject var engine = LynrixEngine()
    @StateObject var themeManager = ThemeManager()
    @StateObject var loc = LocalizationManager()
    
    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(engine)
                .environmentObject(themeManager)
                .environmentObject(loc)
                .environment(\.theme, themeManager.current)
        }
    }
}
```

**Environment objects** propagated to all views:
- `LynrixEngine` — main engine wrapper, ObservableObject
- `ThemeManager` — dark/light/system appearance
- `LocalizationManager` — English, Russian, Chinese Simplified

### 5.2 LynrixEngine (Swift)

**`Models/LunrixEngine.swift`** (~58K lines) — central state manager:

**State machine:**
```
enum EngineStatus: Int {
    case idle = 0
    case connecting = 1
    case connected = 2
    case trading = 3
    case error = 4
    case stopping = 5
}
```

**Polling system** (adaptive frequency):
- Active trading: 100ms interval (10 FPS)
- Idle: 1000ms interval (1 FPS)
- Transition: exponential ramp-up/down
- Version-gated: skip poll if `snapshotVersion` unchanged

**Published properties** (~100 @Published):
- `status`, `paperMode`, `config`
- `positionState` (pnlHistory, drawdownHistory)
- `orderBook` (bids, asks, spread, mid)
- `features` (25 feature values)
- `metrics` (counters, latencies)
- `regime`, `prediction`, `threshold`
- `executionStats`, `strategyMetrics`, `strategyHealth`
- `rlState`, `systemMonitor`, `controlPlane`

**Submodels** (`EngineSubmodels.swift`):
```swift
final class PositionState: ObservableObject {
    @Published var position: PositionSnapshot = .init()
    @Published var pnlHistory: [Double] = []
    @Published var drawdownHistory: [Double] = []
}
```

**D-F1: Unified key loading** — `start()` auto-loads API keys from Keychain if not explicitly provided.

### 5.3 Views & Components

**17 sidebar tabs** (D-R1/D-R2 consolidated from 22):

| Section | Tab | View |
|---------|-----|------|
| **Overview** | Dashboard | `DashboardView` — widget grid with customizable layout |
| | AI Dashboard | `AIDashboardView` — model monitoring |
| **Market** | Order Book | `OrderBookView` — real-time L2 depth |
| | Signals & Tape | `MergedViews` — signal feed + trade tape |
| **Portfolio** | Portfolio | `PortfolioView` — position, PnL, equity |
| | Execution Intelligence | `ExecutionIntelligenceView` |
| | Trade Journal | `TradeReviewStudioView` — trade review + auto-rating |
| **Strategy** | Strategy Performance | `StrategyPerformanceView` |
| | Strategy Research | `StrategyResearchView` |
| **Risk** | Risk Command Center | `RiskCommandCenterView` |
| | VaR Dashboard | `VarDashboardView` |
| **AI/ML** | Market Regimes | `MarketRegimeCenterView` |
| | Model Governance | `ModelGovernanceView` |
| | RL Training | `RLTrainingView` |
| **System** | System Monitor | `SystemMonitorView` |
| | Diagnostics & Logs | `MergedViews` — diagnostics + log panel |
| | Settings | `SettingsView` — 4-tab restructured |

**Reusable components:**
- `GlassPanel` — glassmorphism container with neon accent
- `NeonButton` — glowing action button with icon
- `GlowText` — text with color glow effect
- `AnimatedEquityCurve` — animated PnL chart
- `PnlSparkline` — compact inline PnL mini-chart (D-A3)
- `UnifiedStatusBar` — bottom bar with connection, latency, mode indicators
- `RegimeBadge` — regime indicator with color coding
- `EmptyStateView` — placeholder for empty data states

### 5.4 Theme System

**`Components/LxTheme.swift`** — comprehensive theme:

```swift
struct LxTheme {
    // Background
    let backgroundPrimary, backgroundSecondary: Color
    let inputBackground: Color
    
    // Text
    let textPrimary, textSecondary, textTertiary: Color
    
    // Borders
    let borderSubtle, borderAccent: Color
    
    // Materials
    let hudMaterial: NSVisualEffectView.Material
    let isDark: Bool
}
```

**Named colors** (`LxColor`):
- `electricCyan` — primary accent
- `neonLime` — success/start
- `bloodRed` — danger/stop
- `magentaPink` — execution
- `gold` — API/credentials
- `amber` — warnings
- `coolSteel` — neutral/info

**Appearance modes**: Dark, Light, System (auto-follows macOS)

**Typography** (`LxFont`):
- `mono(size, weight)` — SF Mono for data
- `label` — small labels
- `micro` — tiny annotations

### 5.5 Localization

Three languages with full coverage (~400+ keys each):

| Language | File | Coverage |
|----------|------|----------|
| English | `en.lproj/Localizable.strings` | Full |
| Russian | `ru.lproj/Localizable.strings` | Full |
| Chinese Simplified | `zh-Hans.lproj/Localizable.strings` | Full |

Key categories: common, settings, dashboard, risk, strategy, signals, execution, AI, notifications, profiles, onboarding, command palette.

### 5.6 Authentication & Profiles

**AuthManager** (`Models/AuthManager.swift`):
- Firebase Authentication (email/password, Google, Apple Sign-In)
- Persistent sessions via Keychain
- User profile sync with Firestore

**KeychainManager** (`Models/KeychainManager.swift`):
- Secure storage for Bybit API key/secret
- macOS Keychain Services API
- `hasCredentials`, `loadAPIKey()`, `loadAPISecret()`, `saveCredentials()`, `deleteCredentials()`

**ProfileManager** (`Models/ProfileManager.swift`):
- Multiple trading configuration profiles
- Built-in presets (Conservative, Balanced, Aggressive, Scalping)
- Save/load/apply with engine integration

### 5.7 Command Palette & Keyboard Shortcuts

**D-A1: Command Palette** (`Components/CommandPalette.swift`):
- Activated via **Cmd+K**
- Fuzzy search across all commands
- Categories: Navigation (tab switching), Engine (start/stop/emergency), Theme
- Keyboard navigation: ↑/↓ arrows, Enter to execute, Escape to dismiss
- macOS 14+ uses `KeyPress`; macOS 13 fallback via `NSEvent.addLocalMonitorForEvents`

**D-A2: Keyboard shortcuts:**
- **Cmd+1 through Cmd+9** — direct tab switching
- **Cmd+E** — emergency stop
- **Cmd+K** — toggle command palette

### 5.8 Toast Notifications

**D-A4: Toast System** (`Components/ToastNotification.swift`):
- `ToastManager` — singleton, observable
- Severity levels: `.info`, `.success`, `.warning`, `.error`
- Auto-dismiss (configurable, default 3s)
- Manual dismiss on hover
- Stacks vertically with animation

---

## 6. Configuration

### 6.1 C++ Config (`config.json`)

```json
{
    "symbol": "BTCUSDT",
    "paper_trading": true,
    "paper_fill_rate": 0.85,
    
    "ws": {
        "public_url": "wss://stream.bybit.com/v5/public/linear",
        "private_url": "wss://stream.bybit.com/v5/private",
        "ping_interval_sec": 20,
        "stale_timeout_sec": 30,
        "reconnect_base_ms": 1000,
        "reconnect_max_ms": 30000
    },
    
    "rest": {
        "base_url": "https://api.bybit.com",
        "timeout_ms": 5000,
        "max_retries": 3
    },
    
    "trading": {
        "order_qty": 0.001,
        "signal_threshold": 0.6,
        "signal_ttl_ms": 300,
        "entry_offset_bps": 1.0
    },
    
    "risk": {
        "max_position_size": 0.1,
        "max_leverage": 10.0,
        "max_daily_loss": 100.0,
        "max_drawdown": 0.05,
        "max_orders_per_sec": 10
    },
    
    "performance": {
        "ob_levels": 500,
        "io_threads": 2,
        "feature_tick_ms": 10
    }
}
```

### 6.2 Swift Config (TradingConfig)

Settings view provides 4-tab UI (D-A5):
- **General** — Account, API keys, trading mode (paper/live), profiles, about
- **Trading** — Symbol, order qty, thresholds, risk limits, circuit breaker
- **AI/ML** — ML model toggle, adaptive threshold, regime detection, ONNX config
- **Preferences** — Language, appearance, notifications

### 6.3 Runtime Configuration

C2: Atomic runtime-mutable parameters (UI writes, engine reads):
- `paper_trading`, `signal_threshold`, `order_qty`, `max_position`
- `max_leverage`, `max_daily_loss`, `max_drawdown`, `entry_offset_bps`
- `requote_enabled`, `adaptive_threshold`, `adaptive_sizing`, `cb_enabled`

---

## 7. Build System

### 7.1 C++ Build (CMake)

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DAPPLE_TARGET_CPU=apple-m4 \
      -DENABLE_LTO=ON

# Build
cmake --build build -j$(nproc)

# Run
./build/lynrix config.json
```

**Compiler flags:**
- C++23, `-O3 -mcpu=apple-m4 -ffast-math`
- ThinLTO (cross-module optimization)
- Optional PGO (profile-guided optimization)

**Dependencies** (FetchContent):
- Boost 1.84 (asio, beast, json)
- spdlog 1.13
- simdjson 3.6
- Google Test 1.14
- Google Benchmark 1.8
- ONNX Runtime (optional, for production ML)

**Frameworks**: Accelerate (vDSP/BLAS), Metal (GPU), Security, SystemConfiguration

### 7.2 Swift Build (Xcode)

```bash
cd Lunrix
xcodebuild -scheme Lynrix -configuration Debug -destination 'platform=macOS' build
```

- Deployment target: macOS 13.0
- Swift 5.9, Xcode 15+
- Bundle ID: `com.lynrix.trader`
- Mixed ObjC++/Swift via bridging header

---

## 8. Testing

### 8.1 C++ Test Suites

| Suite | Tests | Coverage |
|-------|-------|----------|
| `test_order_state_machine` | 55 | FSM transitions, fill tracker, iceberg, TWAP/VWAP, market impact, adaptive cancel, VaR engine |
| `test_stage3_md_correctness` | 33 | Sequence gap detection, resync state machine, book state transitions, diagnostic counters |
| `test_all_modules` | 69 | Integration tests across all modules |
| `test_ai_modules` | 61 | ML model, feature engine, regime detection |
| `test_comprehensive` | 55 | End-to-end pipeline tests |
| `test_orderbook_v2` | 23 | OB v2 operations, binary search, imbalance |
| `test_orderbook_v3` | 26 | OB v3 Robin Hood hash, intrusive list |
| Other suites | 66+ | Various unit/integration tests |
| **Total** | **388+** | |

```bash
# Run all tests
cd build && ctest --output-on-failure

# Run specific suite
./build/test_order_state_machine
```

### 8.2 Benchmarks

```bash
# OB v3 benchmark
./build/bench_orderbook_v3

# Execution engine benchmark
./build/bench_execution

# TSC clock benchmark
./build/bench_tsc
```

---

## 9. Performance Benchmarks

All benchmarks on Apple M3 Pro:

### Hot-Path Latency

| Operation | Mean | p99 |
|-----------|------|-----|
| OB delta update (binary search) | ~19 ns | ~25 ns |
| OB best_bid/ask | ~5 ns | ~7 ns |
| OB imbalance(5) | ~7 ns | ~10 ns |
| FSM transition (LUT) | ~5.2 ns | ~6 ns |
| Fill prob EMA update | ~5.4 ns | ~7 ns |
| Market impact compute | ~5.2 ns | ~7 ns |
| Adaptive cancel decision | ~5.3 ns | ~7 ns |
| Feature extraction (25 features) | ~200 ns | ~350 ns |
| ONNX inference (100-step seq) | ~500 µs | ~800 µs |
| GRU inference (native) | ~2.8 µs | ~4 µs |
| **E2E strategy tick** | **~3-5 µs** | **~8 µs** |

### Risk Computation

| Operation | Mean | p99 |
|-----------|------|-----|
| VaR 10k Monte-Carlo | ~735 µs | ~912 µs |
| VaR 1k Monte-Carlo | ~64 µs | ~80 µs |
| Parametric VaR | ~2 µs | ~3 µs |

### Clock

| Operation | Mean |
|-----------|------|
| `TscClock::now()` | ~2 ns |
| `std::chrono::steady_clock::now()` | ~20-50 ns |

---

## 10. Security

### API Key Management
- **Storage**: macOS Keychain Services (encrypted at rest)
- **Transport**: HMAC-SHA256 signed requests, TLS 1.2+ WebSocket
- **Never logged**: API keys never appear in spdlog output or research recordings
- **Environment variables**: CLI mode reads from `BYBIT_API_KEY` / `BYBIT_API_SECRET`
- **D-F1: Unified loading**: Engine auto-loads from Keychain; explicit key passing optional

### Network Security
- TLS 1.2+ for all WebSocket and REST connections
- Certificate verification enabled (`ssl::verify_peer`)
- HMAC-SHA256 request signing with timestamp + recv_window

### Paper Trading Safety
- **R3: Paper fill gate** — configurable `paper_fill_rate` (0–100%) prevents unrealistic paper fills
- Paper mode confirmed via dialog before switching to live
- Private WS and WS Trade connections skipped in paper mode

---

## 11. Deployment

### macOS Application

```bash
# Build and run via Xcode
cd Lunrix
xcodebuild -scheme Lynrix -configuration Release build

# Run built app
open ~/Library/Developer/Xcode/DerivedData/Lynrix-*/Build/Products/Release/Lynrix.app
```

### Standalone C++ Engine

```bash
# Set API keys (live mode)
export BYBIT_API_KEY="your-key"
export BYBIT_API_SECRET="your-secret"

# Run with config
./build/lynrix config.json
```

### CPU Isolation (Optional)

```bash
# Optimize for low-latency (disable App Nap, Spotlight, timer coalescing)
sudo bash scripts/isolate_cores.sh
```

### Recommended Hardware
- Apple Silicon M2 Pro or better
- 16 GB+ RAM (engine uses ~50 MB, OB + features + VaR fit in L2 cache)
- Low-latency network connection to Bybit servers

---

## Appendix A: Key Type Definitions

```cpp
// Strong types (src/core/strong_types.h)
struct Price    { double v; };     // Signal-layer price
struct Qty      { double v; };     // Order quantity
struct Notional { double v; };     // Price × Qty
struct BasisPoints { double v; };  // Price difference in bps
struct TimestampNs { uint64_t v; }; // Monotonic nanoseconds
struct DurationNs  { int64_t v; };  // Signed duration
struct SequenceNumber { uint64_t v; }; // Typed sequence ID

// Fixed-point OB price (src/orderbook/orderbook.h)
struct FixedPrice { int64_t raw; }; // price * 1e8

// Market regimes (src/config/types.h)
enum class MarketRegime : uint8_t {
    LowVolatility = 0, HighVolatility = 1,
    Trending = 2, MeanReverting = 3, LiquidityVacuum = 4
};

// Engine status (Swift)
enum EngineStatus: Int {
    case idle=0, connecting=1, connected=2,
         trading=3, error=4, stopping=5
}
```

## Appendix B: Feature Vector Layout

```
Index  Name                    Source
─────  ──────────────────────  ────────────
0      imbalance_1             OrderBook
1      imbalance_5             OrderBook
2      imbalance_20            OrderBook
3      ob_slope                OrderBook
4      depth_concentration     OrderBook
5      cancel_spike            OrderBook
6      liquidity_wall          OrderBook
7      aggression_ratio        TradeFlow
8      avg_trade_size          TradeFlow
9      trade_velocity          TradeFlow
10     trade_acceleration      TradeFlow
11     volume_accel            TradeFlow
12     microprice              Price
13     spread_bps              Price
14     spread_change_rate      Price
15     mid_momentum            Price
16     volatility              Price
17     microprice_dev          Derived
18     short_term_pressure     Derived
19     bid_depth_total         Derived
20     ask_depth_total         Derived
21     d_imbalance_dt          Temporal
22     d2_imbalance_dt2        Temporal
23     d_volatility_dt         Temporal
24     d_momentum_dt           Temporal
```

## Appendix C: Control-Plane State Diagram

```
RiskControlFSM:
  Normal ──DrawdownWarning──► Cautious
  Cautious ──DrawdownBreached──► Restricted
  Restricted ──ConsecutiveLosses──► CircuitBreaker
  CircuitBreaker ──CooldownExpired──► Normal
  Any ──ManualHalt──► Halted
  Halted ──ManualReset──► Normal

ExecControlFSM:
  Normal ──RiskRestricted──► ReduceOnly
  ReduceOnly ──RiskCircuitBreaker──► CancelOnly
  CancelOnly ──EmergencySignal──► EmergencyCancel
  Any ──PauseRequested──► Paused
  Any ──ShutdownRequested──► Shutdown
```

---

*Document generated: 2026-03-24 · Lynrix v2.5.0 · 388+ tests passing*
