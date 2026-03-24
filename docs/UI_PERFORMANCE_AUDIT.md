# UI Performance Audit — Lynrix v2.5

**Date:** 2026-03-24
**Target:** MacBook Air 15" M2 @ 60 Hz
**Methodology:** Static code analysis of SwiftUI view tree, state propagation, and rendering pipeline

---

## Top Bottlenecks (Priority Ranked)

### 1. CRITICAL — Monolithic ObservableObject with ~40 @Published properties

**File:** `LunrixEngine.swift` lines 485–527
**Impact:** Every @Published mutation fires `objectWillChange.send()`. During a single poll cycle, up to 20 properties may change, producing 20 separate publisher emissions. Since `engine` is injected as `@EnvironmentObject` into ALL 29 views + components, every emission invalidates the entire view tree.

**Why it hurts:** SwiftUI schedules a body re-evaluation for every view that observes the changed `ObservableObject`. Even though only the active tab's view is in the hierarchy, the status bar, sidebar badges, toast overlay, and command palette modifier ALL observe `engine`. Each re-evaluation costs layout + diffing + potential GPU work.

**Submodels exist but are unused:** `EngineSubmodels.swift` defines `MarketDataState`, `PositionState`, `AIState`, etc. — but **zero views** reference them. All views use `@EnvironmentObject var engine`.

**Fix:** Remove `@Published` from high-churn properties (orderBook, position, metrics, regime, prediction, threshold, etc.). Store as plain vars. Send ONE `objectWillChange.send()` at the end of each poll cycle. Keep `@Published` only on rarely-changing state (status, config, paperMode, killSwitch).

**Expected impact:** 15-20x fewer objectWillChange emissions per poll cycle → dramatic reduction in SwiftUI invalidation work.

### 2. HIGH — GlassPanel GPU compositing cost

**File:** `GlassPanel.swift` lines 27–90
**Impact:** Every `GlassPanel` in dark mode stacks: `.ultraThinMaterial` (real-time Gaussian blur), gradient fill overlay, gradient stroke overlay, shadow with dynamic blur radius, hover scale animation. A typical dashboard screen has 6-8 GlassPanels visible simultaneously.

**Why it hurts:** `.ultraThinMaterial` requires the GPU to sample and blur the background content behind each panel on every frame. Combined with shadows and overlays, each panel is ~4 compositing passes. On a MacBook Air M2 (fanless, shared GPU memory), this saturates the GPU compositor.

**Fix:** Replace `.ultraThinMaterial` with a solid semi-transparent background color. Keep the visual identity (dark glass look) but eliminate real-time blur. Remove hover `scaleEffect` animation (costs layout recalc). Keep the neon border for brand identity.

**Expected impact:** ~60-70% reduction in GPU compositing work per GlassPanel.

### 3. HIGH — VisualEffectBackground on sidebar AND content area

**File:** `ContentView.swift` lines 169-175, 260-266
**Impact:** Both the main content background and sidebar use `VisualEffectBackground` (NSVisualEffectView), adding real-time blur compositing to the two largest surfaces in the app.

**Fix:** Remove or reduce VisualEffectBackground usage. Use solid backgrounds.

### 4. MEDIUM — Unbounded history arrays as @Published

**Files:** `LunrixEngine.swift` lines 504-506
**Impact:** `pnlHistory`, `drawdownHistory`, `accuracyHistory` are @Published `[Double]` arrays capped at 500 elements. Each `append()` + `removeFirst()` copies the entire array and fires objectWillChange. Charts that bind to these arrays rebuild paths on every update.

**Fix:** Cap display arrays at 200 points. Use a pre-allocated ring buffer pattern. Only push to @Published when the view needs to update (throttled).

### 5. MEDIUM — String formatting in view bodies

**Files:** `UnifiedStatusBar.swift`, `DashboardView.swift`, many views
**Impact:** `String(format: "%.2f", engine.orderBook.midPrice)` runs on every body evaluation. NumberFormatter and String(format:) are expensive relative to a 16ms frame budget.

**Fix:** Pre-format strings during poll and store as plain Strings, or use a lightweight cached formatter.

### 6. LOW — Infinite repeat animations

**Files:** `GlassHaltBadge`, `GlassChaosBadge`, `GlassReconnectBadge`, `StatusDot`
**Impact:** `repeatForever` animations force the render server to wake every frame (16.6ms at 60Hz) even when content is static. Multiple pulsing badges compound this.

**Fix:** Use `transaction { $0.animation = nil }` for static states. Only animate when truly needed.

### 7. LOW — Sidebar ForEach with .filter{}

**File:** `ContentView.swift` lines 223-225
**Impact:** `SidebarTab.allCases.filter { $0.section == section }` creates a new array allocation on every sidebar body evaluation.

**Fix:** Pre-compute filtered tab arrays as static constants.

---

## Summary

| # | Bottleneck | Impact | Effort | Priority |
|---|-----------|--------|--------|----------|
| 1 | 40 @Published → 20 emissions/poll | Critical | Medium | P0 |
| 2 | GlassPanel .ultraThinMaterial | High | Low | P1 |
| 3 | VisualEffectBackground × 2 | High | Low | P1 |
| 4 | Unbounded history arrays | Medium | Low | P2 |
| 5 | String formatting in body | Medium | Low | P2 |
| 6 | Infinite repeat animations | Low | Low | P3 |
| 7 | Sidebar filter allocations | Low | Low | P3 |

**Expected overall impact:** Reducing objectWillChange emissions by 15-20x and eliminating real-time blur compositing should make the app feel dramatically smoother on MacBook Air M2. Frame drops during active trading should be virtually eliminated.
