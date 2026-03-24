# Lynrix HFT Platform — Full Audit Report
**Date**: 2026-03-19  
**Version**: v2.5.0  
**Scope**: C++23 Engine + macOS Swift UI + Bridge Layer + Build System

---

## Architecture Overview

| Layer | Tech | Files | LOC (est.) |
|-------|------|-------|------------|
| **C++ Engine** | C++23, Boost.Beast/ASIO, simdjson, ONNX Runtime, Accelerate/NEON | ~68 src/ files | ~25k |
| **Bridge** | Pure C API → Obj-C++ wrapper → Swift | 3 files | ~2.5k |
| **Swift UI** | SwiftUI, Combine, Firebase Auth, GoogleSignIn | ~55 files | ~20k |
| **Tests** | GoogleTest, libFuzzer | 23 test files | ~8k |

**Pipeline**: WS → simdjson parse → OrderBook delta → Feature Engine → ML Inference → Signal → Risk Check → Execution → Exchange

---

# 🔴 SECTION 1: CRITICAL BUGS (Fix Immediately)

### C1. `Portfolio::snapshot()` is NOT atomic — torn reads in production
**File**: `src/portfolio/portfolio.h:68-76`  
**Problem**: `snapshot()` reads 6 separate `std::atomic<double>` variables sequentially. Between reads, the WS thread can update position fields, producing an **inconsistent snapshot** (e.g., new `size` with old `entry_price`). This is a **data race by design** — each atomic read is individually safe, but the *combination* is not.  
**Impact**: Incorrect PnL calculation, wrong risk decisions, phantom positions.  
**Fix**: Use a `SeqLock` (you already have one for UI snapshots) or `std::shared_mutex` for the Position struct. The SeqLock pattern is already proven in `ui_seqlock_`.

### C2. SSL verification disabled — MITM vulnerability
**File**: `src/app/application.h:85`  
```cpp
ssl_ctx_.set_verify_mode(ssl::verify_none);
```
**Problem**: **All** WebSocket and REST connections to Bybit have SSL certificate verification disabled. An attacker on the same network can intercept API keys, HMAC secrets, and trading commands.  
**Impact**: Complete account takeover. API keys + secrets travel in plaintext equivalent.  
**Fix**: `ssl_ctx_.set_verify_mode(ssl::verify_peer)` + load system CA bundle. On macOS: `ssl_ctx_.set_default_verify_paths()` is already called but pointless without `verify_peer`.

### C3. `WsClient` reconnect destroys websocket via placement-new UB
**File**: `src/ws_client/ws_client.h:289-291`  
```cpp
self->ws_.~stream();
new (&self->ws_) websocket::stream<...>(net::make_strand(self->ioc_), self->ssl_ctx_);
```
**Problem**: Explicit destructor call + placement new on a member of a `shared_ptr`-managed object is **undefined behavior** if any async operation still holds a reference to the old stream. The old strand/executor may still have pending handlers. This can cause use-after-free crashes during reconnection under load.  
**Fix**: Replace with a `std::unique_ptr<websocket::stream<...>>` member. On reconnect: `ws_ = std::make_unique<...>(...)`. Or use a fresh `WsClient` instance entirely.

### C4. `purgeStaleKeychainItems()` deletes ALL keychain items — destroys user API keys
**File**: `Lunrix/Models/AuthManager.swift:75-89`  
**Problem**: This function deletes **every** `kSecClassGenericPassword` and `kSecClassInternetPassword` item in the app's keychain group. The app's own `KeychainManager` stores API keys/secrets as `kSecClassGenericPassword`. So every time `AuthManager.configure()` runs (app launch), it **wipes the user's saved Bybit API credentials**.  
**Impact**: Users must re-enter API keys on every app launch. Frustrating UX, potential trading interruption.  
**Fix**: Scope the purge to only Firebase/Google keychain items by adding `kSecAttrService` or `kSecAttrAccount` filters matching Firebase's known service names (e.g., `"firebase_auth"`, `"com.google.GIDSignIn"`). Never delete items matching `KeychainManager.serviceName`.

### C5. `WsTradeClient` never calls `start_timeout_checker()`
**File**: `src/networking/ws_trade_client.h:165-171`  
**Problem**: The `start_timeout_checker()` method exists and is well-implemented, but it is **never called** from `start()` or anywhere else. Pending order callbacks can leak indefinitely — if the exchange never responds, the `pending_callbacks_` map grows without bound.  
**Impact**: Memory leak under production load. Stuck order state. Callback never fires → local state diverges from exchange.  
**Fix**: Add `start_timeout_checker()` call at the end of the auth confirmation handler (after `authenticated_.store(true)`).

---

# 🟠 SECTION 2: MANDATORY BUGS (Fix Soon)

### M1. Hardcoded `TICK_SIZE_BTCUSDT` and `LOT_SIZE_BTCUSDT` — breaks non-BTC symbols
**File**: `src/config/types.h:45-46`  
```cpp
static constexpr double TICK_SIZE_BTCUSDT = 0.1;
static constexpr double LOT_SIZE_BTCUSDT  = 0.001;
```
Used in `smart_execution.h`, `fill_probability.h`, `trade_flow_engine.h`.  
**Problem**: The UI allows changing the symbol (e.g., `ETHUSDT`, `SOLUSDT`), but the engine uses hardcoded BTC tick/lot sizes. ETHUSDT has tick=0.01, lot=0.01. SOLUSDT has tick=0.01, lot=0.1. Wrong tick size → wrong slippage calculations, wrong fill probability bands, wrong order prices.  
**Fix**: Fetch instrument info from Bybit REST API on startup. Store tick_size/lot_size in `AppConfig`. Pass to all consumers.

### M2. `application.h` is a 1190-line God Header
**Problem**: `application.h` contains the entire Application class including `strategy_tick()` (hot path), all timer scheduling, reconciliation, shutdown, UI snapshot publishing, cold drain — all in one header. This is:
- Impossible to unit-test (everything is private)
- Forces full recompilation on any change
- Violates single responsibility  

**Fix**: Extract into separate compilation units: `strategy_pipeline.cpp`, `timer_manager.cpp`, `reconciliation.cpp`, `ui_publisher.cpp`.

### M3. `CircuitBreaker::is_tripped()` calls `Clock::now_ns()` — hot-path side effect
**File**: `src/risk_engine/enhanced_risk_engine.h:40-51`  
**Problem**: `is_tripped()` is called from the hot path every tick (signal gating). It calls `Clock::now_ns()` to check cooldown. This is a **const method with a syscall side-effect** in the hot path. On most ticks the CB isn't tripped, but the clock call still executes.  
**Fix**: Cache the cooldown-expiry timestamp when tripping. `is_tripped()` becomes: `return tripped_ && Clock::now_ns() < cooldown_expires_ns_`.

### M4. Two theme systems co-exist — `LunrixTheme.swift` + `LxTheme.swift`
**Files**: `Lunrix/Components/LunrixTheme.swift` (7.4KB) + `Lunrix/Components/LxTheme.swift` (7.5KB)  
**Problem**: Both files define complete color/font systems. Views use `LxTheme` via `@Environment(\.theme)` while some older code references `LunrixTheme`. Duplicate tokens, divergent behavior.  
**Fix**: Delete `LunrixTheme.swift`. Migrate any remaining references to `LxTheme`.

### M5. `SettingsView.swift` — 788 lines, all `@State` not synced with engine config
**File**: `Lunrix/Views/SettingsView.swift:25-51`  
**Problem**: 27 `@State` variables are initialized with hardcoded defaults (e.g., `@State private var symbol: String = "BTCUSDT"`), not from `engine.config`. If the engine config changes elsewhere (profile load, CLI), the settings UI shows stale values.  
**Fix**: Initialize all `@State` from `engine.config` in `.onAppear {}` or use `@Binding`.

### M6. `LynrixEngine` has 40+ `@Published` properties — triggers excessive SwiftUI redraws
**File**: `Lunrix/Models/LunrixEngine.swift:485-528`  
**Problem**: Every `@Published` change triggers `objectWillChange` for ALL subscribers. With 40+ properties updating at 10 FPS, this causes massive view recomputation even for views that don't use the changed property.  
**Fix**: Split into focused `ObservableObject` submodels: `OrderBookState`, `PositionState`, `AIState`, `SystemState`. Views subscribe only to what they need.

### M7. Version string mismatch
**Files**: `CMakeLists.txt:18` says `2.5.0`, `application.h:112` says `"v2.4"`, `LunrixEngine.swift:584` says `"v2.5.0"`.  
**Fix**: Single source of truth. Define version in CMake, pass via compile definition to C++, bridge to Swift.

### M8. `reconcile_orders()` iterates JSON array just to count elements
**File**: `src/app/application.h:983-987`  
```cpp
size_t exchange_count = 0;
for (auto item : list.get_array()) {
    (void)item;
    ++exchange_count;
}
```
**Problem**: simdjson `ondemand` arrays can only be iterated once. After counting, the array is consumed — no data extracted. The reconciliation only checks count mismatch but never identifies *which* orders differ.  
**Fix**: Parse order IDs during iteration. Cross-reference with local `active_orders_`. Cancel orphaned local orders, log missing exchange orders.

---

# 🔧 SECTION 3: ENGINE — What to Improve, Remove, Rework

### E1. IMPROVE: OrderBook v2 vs v3 coexistence
**Problem**: `orderbook.h` (v2, production) and `orderbook_v3.h` both define `class OrderBook` — can't include both. v3 has O(1) Robin Hood hashmap but is unused in production.  
**Recommendation**: Either promote v3 to production (rename v2 → `OrderBookLegacy`) or delete v3. Dead code is maintenance debt.

### E2. IMPROVE: GRU model is toy-grade
**File**: `src/model_engine/gru_model.h`  
The native GRU is a single-layer 64-hidden-unit model with random initialization. It's a proof-of-concept, not production ML.  
**Recommendation**: Focus on ONNX Runtime path. Train a proper model offline (Python/PyTorch), export ONNX, load at runtime. The native GRU should be clearly marked as "fallback/demo" or removed.

### E3. IMPROVE: `FeatureImportanceAnalyzer` uses permutation importance at runtime
**Problem**: Permutation importance requires repeated model evaluation with shuffled features — O(features × samples) per computation. Running this every 500 ticks in the cold path is expensive.  
**Recommendation**: Compute offline. Store SHAP values from training. Display static feature importance. Remove runtime permutation.

### E4. REWORK: RL optimizer (PPO) has no replay buffer persistence
The `RLOptimizer` trains online from scratch every session. No saved policy weights, no experience replay across restarts.  
**Recommendation**: Add policy checkpoint save/load (JSON weights). Or honestly assess if online RL is premature and replace with a simpler Bayesian optimizer for threshold/position-size tuning.

### E5. IMPROVE: `TradeFlowEngine::evict_expired()` can drift negative
**File**: `src/trade_flow/trade_flow_engine.h:139-140`  
```cpp
if (acc.buy_volume < 0.0) acc.buy_volume = 0.0;
```
The clamping after drift is a band-aid. Floating-point accumulation error grows over time.  
**Fix**: Periodically rebuild accumulators from the ring buffer (e.g., every 1000 trades).

### E6. REMOVE: `BybitTrader/` directory
31 items in `/Users/slavaivanov/CLionProjects/bybit/BybitTrader/` — appears to be legacy code pre-refactor. If not used, remove to avoid confusion.

### E7. IMPROVE: `SmartExecutionEngine` paper fill is simplistic
Paper mode immediately fills orders at the order price. Real fills depend on queue position, time priority, and whether the price was actually crossed. This makes paper-mode backtesting unrealistically optimistic.  
**Recommendation**: Simulate queue position. Fill only when the opposite side crosses the order price. Model partial fills.

### E8. IMPROVE: No rate limiter for Bybit REST API
`RestClient` has no request rate limiter. Bybit enforces 120 req/min for order endpoints. Under reconnection storms or heavy reconciliation, the app can hit rate limits → IP ban.  
**Fix**: Add a token-bucket rate limiter (10 req/s burst, 2 req/s sustained).

---

# 🎨 SECTION 4: UI/DESIGN — What to Remove, Add, Fix

### D1. REMOVE: Duplicate/Redundant Views
The sidebar has **24 tabs** across 5 sections. This is overwhelming for any user. Recommendations:
- **Merge** `AIDashboardView` into `DashboardView` — they show overlapping AI metrics
- **Merge** `MLDiagnosticsView` into `ModelGovernanceView` — both are about ML model state
- **Merge** `DiagnosticsView` into `SystemMonitorView` — both are about system health
- **Hide** `ChaosControlView` and `ReplayControlView` behind a "Developer Tools" section (most users should never see chaos monkey)
- Target: **15-16 tabs max**, ideally organized as: Trading (5) | Analytics (4) | Risk (3) | Settings (3)

### D2. ADD: Onboarding/First-Run Wizard
`OnboardingView.swift` exists (14KB) but there's no guided flow for:
- API key setup (what permissions to grant on Bybit)
- Risk parameter explanation (what does "max drawdown 0.1" mean in dollars)
- Paper mode warning before live trading
- Symbol selection with instrument info

### D3. ADD: Real-time P&L notification/sound
The app has `NotificationManager.swift` but no audible/visual alert for significant events:
- Large loss (>X% of daily budget)
- Circuit breaker trip
- WebSocket disconnect during active position
- Fill notification

### D4. FIX: Localization is incomplete
Three localization files exist (en, ru, zh-Hans) but many views use hardcoded English strings alongside `loc.t()` calls. Audit all views for raw strings.

### D5. FIX: No loading/empty state for most views
Views like `OrderBookView`, `PortfolioView`, `SignalsView` render empty grids when no data is available. There should be clear "Connecting...", "Waiting for data...", or "Start engine to see data" states.

### D6. ADD: Dark/Light mode toggle is buried
`ThemeManager` supports both modes but there's no visible toggle in the sidebar or top bar. Most trading apps have a prominent theme switcher.

### D7. FIX: Font/spacing inconsistency across views
Some views use `LxFont.mono(14, weight: .bold)`, others use `.font(.system(size: 13, weight: .medium, design: .monospaced))`. Standardize on `LxFont` everywhere.

### D8. ADD: Connection status indicator
There's no persistent visual indicator showing:
- Public WS connected/disconnected
- Private WS connected/disconnected  
- Last heartbeat age
- Current latency to exchange

The `UnifiedStatusBar` exists but doesn't show WS connection state explicitly.

### D9. FIX: `AnimatedEquityCurve` redraws constantly
**File**: `Lunrix/Components/AnimatedEquityCurve.swift` (7.5KB)  
SwiftUI Canvas + animation on every PnL update at 10 FPS creates jank on older Macs. Use `drawingGroup()` for Metal-backed rendering, or throttle curve updates to 1 FPS.

---

# 📋 SECTION 5: PRIORITIZED ACTION PLAN

## Phase 1 — Critical (This Week)
| # | Task | Effort | Files |
|---|------|--------|-------|
| C1 | Fix Portfolio torn reads (SeqLock) | 2h | portfolio.h |
| C2 | Enable SSL verification | 30min | application.h |
| C3 | Fix WsClient reconnect UB | 2h | ws_client.h |
| C4 | Scope keychain purge to Firebase only | 1h | AuthManager.swift |
| C5 | Call start_timeout_checker() | 5min | ws_trade_client.h |

## Phase 2 — Mandatory (This Sprint)
| # | Task | Effort | Files |
|---|------|--------|-------|
| M1 | Dynamic tick/lot size from API | 4h | types.h, config_loader.h, smart_execution.h |
| M4 | Delete LunrixTheme.swift, unify to LxTheme | 1h | Components/ |
| M5 | Sync SettingsView @State with engine.config | 2h | SettingsView.swift |
| M6 | Split LynrixEngine into submodels | 4h | LunrixEngine.swift |
| M7 | Fix version string mismatch | 30min | 3 files |

## Phase 3 — Engine Improvements (Next Sprint)
| # | Task | Effort |
|---|------|--------|
| E1 | Resolve OB v2/v3 coexistence | 2h |
| E4 | RL policy persistence | 4h |
| E7 | Improve paper fill simulation | 4h |
| E8 | REST rate limiter | 2h |
| M2 | Break up application.h | 8h |
| M8 | Proper order reconciliation | 4h |

## Phase 4 — UI Polish (Ongoing)
| # | Task | Effort |
|---|------|--------|
| D1 | Consolidate 24 tabs → 16 | 4h |
| D2 | First-run wizard | 8h |
| D5 | Loading/empty states | 4h |
| D8 | Connection status indicator | 2h |
| D9 | Optimize equity curve rendering | 2h |

---

# 💭 SECTION 6: MY OPINION

## Strengths
1. **Excellent hot/warm/cold path separation** in `strategy_tick()`. The Stage 4 pipeline design with budget-based load shedding is genuinely HFT-grade.
2. **SeqLock for UI snapshots** — zero-mutex, zero-allocation data transfer from C++ to Swift. This is the right pattern.
3. **Comprehensive feature set** — 25 features, multi-horizon predictions, regime detection, adaptive thresholds, fill probability, market impact modeling. The breadth is impressive for a solo project.
4. **Strong type system** — `Price`, `Qty`, `Notional`, `BasisPoints`, `SequenceNumber` prevent unit confusion at compile time.
5. **Apple Silicon optimization** — NEON intrinsics, `vDSP` vectorization, P-core/E-core affinity, cache-line alignment. Shows deep platform knowledge.

## Weaknesses
1. **Over-engineering without production validation**. The app has chaos monkey, deterministic replay, VaR engine, RL optimizer, but has never executed a real trade on Bybit mainnet. Building sophisticated observability before the basic trading loop is proven is premature.
2. **ML model is untrained**. The GRU model uses random weights. The ONNX path assumes a pre-trained model that doesn't exist yet. All AI features (regime detection, adaptive threshold, fill probability) are running on synthetic/random outputs.
3. **Auth system is over-complicated for a desktop app**. Firebase Auth + Google Sign-In + Firestore for a locally-running trading bot is unnecessary complexity. A simple local password or Keychain-based auth would suffice. The keychain issues stem entirely from this choice.
4. **Too many sidebar tabs**. 24 views is information overload. A professional trading terminal shows 4-5 views. Most of these views would be better as tabs within fewer master views.

## Strategic Recommendation
**Stop adding features. Start trading.**

1. Fix the 5 critical bugs
2. Get a single live trade working on testnet with real WS data
3. Train an actual ML model on historical data, export to ONNX
4. Then iterate on UI/UX based on real usage patterns

The engine architecture is solid. The risk is shipping complexity without validation.
