# Lynrix AI Edition v2.0 — Architecture & Migration Guide

## 1. Updated Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Lynrix AI Edition v2.0 Engine                             │
│                     Apple Silicon Optimized (M2/M3/M4)                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌──────────┐   SPSC    ┌──────────┐   SPSC    ┌──────────────────┐       │
│  │ WebSocket │──Queue───▶│  Parser  │──Queue───▶│   OrderBook v2   │       │
│  │  (kqueue) │  4096     │ simdjson │  4096     │  Binary Search   │       │
│  │ Ring Buf  │           │ zero-copy│           │  FixedPrice int64│       │
│  └──────────┘           └──────────┘           │  NEON sum_qty    │       │
│       │                                         └────────┬─────────┘       │
│       │                                                  │                  │
│  ┌────▼─────┐                                   ┌───────▼──────────┐       │
│  │  Trade   │                                   │  Feature Engine  │       │
│  │  Flow    │──────────────────────────────────▶│  SIMD/vDSP/NEON  │       │
│  │  Engine  │                                   │  25 features     │       │
│  └──────────┘                                   │  < 25 µs update  │       │
│                                                  └───────┬──────────┘       │
│                                                          │                  │
│                               ┌──────────────────────────▼───────┐         │
│                               │   CoreML Inference Engine        │         │
│                               │  ┌─────────┬─────────┬────────┐ │         │
│                               │  │ Short   │ Medium  │  Long  │ │         │
│                               │  │ 100ms   │ 500ms   │  1-3s  │ │         │
│                               │  │ INT8    │ INT8    │  INT8  │ │         │
│                               │  └────┬────┴────┬────┴───┬────┘ │         │
│                               │       └─────────┼────────┘      │         │
│                               │        Ensemble │ Combine       │         │
│                               │         ~120 µs │ total         │         │
│                               └─────────────────┼───────────────┘         │
│                                                  │                         │
│  ┌──────────────┐         ┌─────────────────────▼──────────┐              │
│  │ PPO RL Agent │◀───────▶│     Signal Generator           │              │
│  │ Actor-Critic │ actions │  Adaptive Threshold + Regime   │              │
│  │ 16→64→4 MLP │────────▶│  Confidence Gating             │              │
│  └──────────────┘         └─────────────────────┬──────────┘              │
│                                                  │                         │
│                               ┌─────────────────▼──────────┐              │
│                               │   Enhanced Risk Engine     │              │
│                               │  Circuit Breaker (7 cond)  │              │
│                               │  VaR/CVaR Monte-Carlo      │              │
│                               │  Volatility Targeting      │              │
│                               │  Dynamic Leverage           │              │
│                               │  ML Confidence Gate         │              │
│                               └─────────────────┬──────────┘              │
│                                                  │                         │
│                               ┌─────────────────▼──────────┐              │
│                               │  Smart Execution v2        │              │
│                               │  Fill-Prob Order Routing   │              │
│                               │  Adaptive Tick Offset      │              │
│                               │  Auto Re-quote + Amend     │              │
│                               │  Emergency Cancel < 1 ms   │              │
│                               └────────────────────────────┘              │
│                                                                            │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                   Infrastructure Layer                             │    │
│  │  ┌────────────┐ ┌────────────┐ ┌──────────┐ ┌──────────────────┐ │    │
│  │  │   Arena    │ │  SPSC/     │ │ SeqLock  │ │  Triple Buffer   │ │    │
│  │  │ Allocator  │ │ Pipeline   │ │   v2     │ │  (UI Snapshot)   │ │    │
│  │  │  4 MB mmap │ │ Connector  │ │ 128-byte │ │  Zero-tear read  │ │    │
│  │  └────────────┘ └────────────┘ └──────────┘ └──────────────────┘ │    │
│  │  ┌────────────┐ ┌────────────┐ ┌──────────┐ ┌──────────────────┐ │    │
│  │  │ HdrHisto   │ │  Watchdog  │ │ BlackBox │ │  Object Pool     │ │    │
│  │  │ Lock-free  │ │  Thread    │ │ Recorder │ │  CAS-free SPSC   │ │    │
│  │  │ p99 O(1)   │ │ Hot Restart│ │ 64K evts │ │  + Concurrent    │ │    │
│  │  └────────────┘ └────────────┘ └──────────┘ └──────────────────┘ │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                            │
│  ┌──────────────────────────────────────────────────────────────┐         │
│  │  Swift Bridge (trading_core_api.h) — SeqLock Snapshot Read  │         │
│  │  TCFullSnapshot via single atomic read — zero mutex         │         │
│  └──────────────────────────────────────────────────────────────┘         │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Pipeline Data Flow (Zero-Copy Path)

```
WebSocket Frame (recv buffer)
   │ memcpy-free parse via simdjson on-demand
   ▼
Parser Output (SPSC queue slot — written in-place)
   │ zero-copy pointer into queue
   ▼
OrderBook Delta (binary search update — O(log n))
   │ FixedPrice int64, CompactLevel 16 bytes
   ▼
Feature Vector (SIMD/vDSP computed — < 25 µs)
   │ 25 doubles aligned to cache line
   ▼
Model Inference (CoreML INT8 ensemble — < 120 µs)
   │ pre-allocated float32 buffers
   ▼
Signal (confidence + direction + fill_prob)
   │ inline risk check (< 10 µs)
   ▼
Execution (PostOnly/IOC decision — < 5 µs)
   │ REST API call (async, off hot path)
   ▼
Done. E2E target: < 200 µs p99
```

---

## 2. New / Rewritten Files

| File | Status | Description |
|------|--------|-------------|
| `src/utils/arena_allocator.h` | **NEW** | mmap-backed bump-pointer arena + ObjectPool + ConcurrentObjectPool. Zero-allocation hot path. |
| `src/utils/lockfree_pipeline.h` | **NEW** | SPSC Queue, TripleBuffer, SeqLock v2, PipelineConnector. All 128-byte aligned for Apple Silicon. |
| `src/feature_engine/simd_indicators.h` | **NEW** | vDSP/NEON vectorized math: sum, mean, variance, dot, EMA, zscore, matvec, sigmoid/tanh batch. |
| `src/model_engine/coreml_inference.h` | **NEW** | CoreML + Metal ensemble engine. 3-model ensemble, INT8 quantization, online learning, hot reload. |
| `src/rl/ppo_actor_critic.h` | **NEW** | Full PPO actor-critic in C++. 16-dim state, 4-dim action, GAE-λ advantage, clipped surrogate loss. |
| `src/monitoring/watchdog.h` | **NEW** | Watchdog thread with HeartbeatRegistry. Per-stage liveness monitoring, hot restart, emergency stop. |
| `src/monitoring/hdr_histogram.h` | **NEW** | Lock-free HdrHistogram. O(1) record, O(1) percentile. 1ns–134s range, 3 significant digits. |
| `src/monitoring/blackbox_recorder.h` | **NEW** | 64K-event ring buffer recorder. 64-byte events, binary dump, text dump, zero-alloc recording. |
| `src/orderbook/orderbook.h` | **REWRITTEN** | Binary search delta (O(log n)), FixedPrice int64, CompactLevel 16B, NEON qty summation, VWAP. |
| `src/execution_engine/smart_execution.h` | **REWRITTEN** | ExecutionStats, emergency_cancel_all, adaptive 4-band tick offset, fill latency tracking. |
| `src/feature_engine/advanced_feature_engine.h` | **MODIFIED** | Added simd_indicators.h include for vectorized computation. |
| `CMakeLists.txt` | **MODIFIED** | C++23, Accelerate + Metal frameworks, `-mcpu=apple-m1 -ffast-math`, version 2.0.0. |
| `docs/V2_ARCHITECTURE.md` | **NEW** | This document — architecture, file list, migration plan, metrics. |

---

## 3. Key Implementation Highlights

### Arena Allocator (`arena_allocator.h`)
- **mmap-backed**: 4 MB default, lazy page commit, `MADV_FREE` on reset
- **Bump pointer**: O(1) allocation, O(1) bulk deallocation
- **ArenaCheckpoint**: RAII save/restore for per-tick scratch space
- **ObjectPool**: single-thread freelist for hot objects
- **ConcurrentObjectPool**: tagged-pointer CAS for cross-thread patterns

### Lock-Free Pipeline (`lockfree_pipeline.h`)
- **SPSCQueue**: power-of-2 ring buffer, head/tail on separate 128B cache lines
- **Zero-copy**: `acquire_write_slot()` + `commit_write()` pattern
- **TripleBuffer**: atomic state word packing (writer/middle/reader + new_data flag)
- **SeqLock v2**: 128B aligned, ARM `yield` hint, retry budget with fallback
- **PipelineConnector**: SPSC + drop counting + batch drain

### OrderBook v2 (`orderbook.h`)
- **FixedPrice**: int64 × 1e8 — eliminates epsilon comparisons
- **CompactLevel**: 16 bytes (8B price + 8B qty) — 8 levels per cache line
- **Binary search**: O(log n) find + `memmove` insert/remove
- **NEON**: `float64x2_t` vectorized qty summation
- **Legacy compat**: `bids()`/`asks()` return `PriceLevel*` via conversion buffer

### SIMD Indicators (`simd_indicators.h`)
- **vDSP**: `vDSP_sveD`, `vDSP_meanvD`, `vDSP_dotprD`, `vDSP_vmulD`, `vDSP_vclipD`
- **BLAS**: `cblas_dgemv` for matrix-vector multiply in GRU forward pass
- **NEON fallback**: `vld1q_f64`, `vfmaq_f64`, `vaddq_f64` for non-Accelerate paths
- **Batch ops**: sigmoid, tanh, z-score normalization, EMA

### CoreML Inference (`coreml_inference.h`)
- **3-model ensemble**: short (100ms), medium (500ms), long (1-3s) horizons
- **Dynamic weighting**: regime-aware ensemble weights
- **Online learning**: per-model output bias adjustment via cross-entropy gradient
- **Hot reload**: load new model without stopping inference
- **Agreement metric**: ensemble consistency for confidence scaling

### PPO Actor-Critic (`ppo_actor_critic.h`)
- **State**: 16 dims (volatility, trend, imbalance, spread, position, PnL, drawdown, etc.)
- **Action**: 4 dims (threshold delta, size scale, offset bps, requote freq)
- **Network**: 2-layer MLP (16→64→4 actor, 16→64→1 critic), Xavier init
- **Training**: GAE-λ advantage, clipped surrogate, normalized advantages
- **Reward**: multi-objective (PnL, drawdown penalty, fill rate, latency)

---

## 4. Migration Plan — Zero Downtime Rollout

### Phase 1: Infrastructure (Week 1)
1. **Merge new utility headers** (`arena_allocator.h`, `lockfree_pipeline.h`, `simd_indicators.h`)
2. **Update CMakeLists.txt** to C++23 + Accelerate
3. **Verify build** on all CI targets (macOS ARM64)
4. **Run existing tests** — all must pass unchanged

### Phase 2: OrderBook + Features (Week 2)
1. **Deploy OrderBook v2** — backward-compatible via `bids()`/`asks()` legacy accessors
2. **Enable SIMD indicators** via `simd_indicators.h` include in `advanced_feature_engine.h`
3. **Benchmark**: verify OB delta < 5 µs, feature computation < 25 µs
4. **A/B test**: run v1 and v2 OrderBook side-by-side, compare OB state consistency

### Phase 3: ML Engine Upgrade (Week 3)
1. **Deploy CoreML inference engine** alongside existing ONNX/GRU engines
2. **Feature flag**: `ml_backend` config to select {NativeGRU, ONNX, CoreML}
3. **Train quantized INT8 models** for all 3 horizons
4. **Enable ensemble** with dynamic weighting
5. **Verify**: inference latency < 120 µs, accuracy ≥ existing

### Phase 4: Execution + Risk (Week 4)
1. **Deploy SmartExecution v2** — backward-compatible API
2. **Deploy PPO RL agent** in observation-only mode (log actions, don't apply)
3. **Enable RL** after 48h of observation data collection
4. **Deploy enhanced risk engine** with VaR/CVaR (paper trading first)

### Phase 5: Observability (Week 5)
1. **Deploy Watchdog** with conservative timeouts (30s stage, 60s critical)
2. **Deploy HdrHistogram** for all pipeline stages
3. **Deploy BlackBox Recorder** — always-on, 4MB ring buffer
4. **Tighten watchdog timeouts** to production values (5s stage, 30s critical)

### Phase 6: Full Production (Week 6)
1. **Enable all v2 features** simultaneously
2. **Monitor**: E2E p99 < 200 µs, fill rate, PnL
3. **Tune PPO RL** hyperparameters based on live data
4. **Remove v1 code paths** after 2 weeks of stable production

### Rollback Strategy
- Each phase has an independent feature flag
- Rollback = disable flag + restart (< 5 seconds)
- State files (positions, PnL) are version-compatible
- BlackBox recorder dumps state on every restart for forensics

---

## 5. Expected Performance Metrics (Apple Silicon M3 Pro)

| Metric | v1 (Current) | v2 (Target) | Improvement |
|--------|-------------|-------------|-------------|
| **E2E Latency p50** | ~800 µs | < 80 µs | 10x |
| **E2E Latency p99** | ~2,500 µs | < 200 µs | 12.5x |
| **OB Delta Update** | ~15 µs (O(n)) | < 3 µs (O(log n)) | 5x |
| **Feature Computation** | ~80 µs | < 25 µs (vDSP) | 3.2x |
| **Model Inference** | ~500 µs (ONNX) | < 120 µs (CoreML INT8) | 4x |
| **Risk Check** | ~20 µs | < 5 µs | 4x |
| **Signal → Order** | ~50 µs | < 10 µs | 5x |
| **Memory (hot path)** | ~2 MB heap alloc/s | 0 alloc/s (arena) | ∞ |
| **Cache Misses** | ~5% L1 miss | < 1% (128B aligned) | 5x |
| **Fill Rate** | ~60% | ~75% (RL-tuned) | +25% |
| **Max Symbols** | 1 | 10 (multi-book) | 10x |
| **Model Confidence** | Single model | Ensemble (3) + agreement | +reliability |
| **Observability** | Logs only | HdrHist + BlackBox + Watchdog | Complete |

### Memory Budget (Steady State)

| Component | Allocation | Strategy |
|-----------|-----------|----------|
| Arena (per-tick scratch) | 4 MB | mmap, reset each tick |
| OrderBook (per symbol) | ~10 KB | Stack, CompactLevel arrays |
| Feature history | ~200 KB | Ring buffer, preallocated |
| SPSC queues (4 stages) | ~1 MB | Heap once at startup |
| BlackBox recorder | 4 MB | Stack array |
| HdrHistogram (8 stages) | ~256 KB | Stack arrays |
| ML input buffers | ~100 KB | Pre-allocated in model state |
| **Total steady-state** | **~10 MB** | **Zero hot-path alloc** |

---

## 6. C++23 Features Used

| Feature | Where | Why |
|---------|-------|-----|
| `std::expected` | Error returns from arena/pool | No exceptions in hot path |
| `alignas(128)` | All cache-line-sensitive data | Apple Silicon 128B L1 line |
| Concepts (future) | Template constraints | Compile-time validation |
| `std::clamp` | Action bounds, feature clipping | Branchless clamping |
| Structured bindings | CAS unpack in ConcurrentPool | Clean tagged-pointer code |
| `[[nodiscard]]` | All allocation/query functions | Prevent ignored returns |
| `constexpr` everything | Sizes, masks, constants | Zero runtime overhead |
| `__builtin_expect` | Branch prediction hints | Hot-path optimization |
