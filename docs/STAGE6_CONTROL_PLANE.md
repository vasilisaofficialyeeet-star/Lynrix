# Stage 6: Control-Plane Formalization

## Overview

Stage 6 formalizes control-plane behavior by introducing explicit finite state machines (FSMs) for risk and execution control, overload detection with backpressure policies, composite system operating modes, and a structured audit trail for deterministic replay.

**Design principles:**
- Zero allocation, branchless LUT-based FSM transitions
- No heavy control logic in the hot path — gates are simple boolean checks
- Overload/risk evaluation runs in the warm path (~20ns total)
- Full backward compatibility with existing risk checks and circuit breaker
- 64-byte audit records in a 256-entry ring buffer for replay

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    ControlPlane                          │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ RiskControlFSM│  │ExecControlFSM│  │OverloadDetect │  │
│  │ 5 states     │  │ 5 states     │  │ severity+hyst │  │
│  │ 10 events    │  │ 8 events     │  │ escalate/deesc│  │
│  └──────┬───────┘  └──────┬───────┘  └───────┬───────┘  │
│         │ propagate        │                  │          │
│         └──────────┼───────┘──────────────────┘          │
│                    ▼                                     │
│           SystemModeResolver                             │
│    ┌─────────────────────────────┐                       │
│    │ FullTrading | ReducedActivity│                      │
│    │ CancelOnly | ViewOnly       │                       │
│    │ EmergencyShutdown           │                       │
│    └─────────────────────────────┘                       │
│                    │                                     │
│           ControlAuditTrail                              │
│    ┌─────────────────────────────┐                       │
│    │ 256 × 64B ring buffer       │                       │
│    │ domain, from→to, reason     │                       │
│    └─────────────────────────────┘                       │
└─────────────────────────────────────────────────────────┘
```

## FSM Definitions

### Risk Control FSM

| State | Position Scale | New Orders | Increase |
|-------|---------------|------------|----------|
| Normal | 1.0 | ✓ | ✓ |
| Cautious | 0.7 | ✓ | ✓ |
| Restricted | 0.3 | ✓ | ✗ (reduce-only) |
| CircuitBreaker | 0.0 | ✗ | ✗ |
| Halted | 0.0 | ✗ | ✗ |

**Events:** PnlNormal, DrawdownWarning, DrawdownBreached, ConsecutiveLosses, LossRateExceeded, CooldownExpired, HealthDegraded, HealthRecovered, ManualHalt, ManualReset

**Transition table:** 5×10 LUT in `RISK_TRANSITION[][]`. All transitions are a single array lookup — O(1), branchless.

### Execution Control FSM

| State | New Orders | Amend | Cancel | Throttle |
|-------|-----------|-------|--------|----------|
| Active | ✓ | ✓ | ✓ | 1.0 |
| Throttled | ✓ | ✓ | ✓ | 0.5 |
| CancelOnly | ✗ | ✗ | ✓ | 0.0 |
| Suspended | ✗ | ✗ | ✗ | 0.0 |
| EmergencyFlat | ✗ | ✗ | ✓ | 0.0 |

**Events:** RiskEscalation, RiskDeescalation, OverloadDetected, OverloadCleared, ManualSuspend, ManualResume, EmergencyStop, FlatComplete

### System Mode (Composite)

Resolved from `max(risk_severity, exec_severity, health_severity)`:

| Mode | Condition |
|------|-----------|
| FullTrading | All nominal |
| ReducedActivity | Any component cautious/throttled/warning |
| CancelOnly | Risk restricted + exec cancel-only, or health critical |
| ViewOnly | Risk CB/halted, or health halted |
| EmergencyShutdown | Exec emergency flat |

## Overload Detection

`OverloadDetector` evaluates per-tick signals:
- `tick_budget_exceeded` (severity 2)
- `latency_spike` (severity 3)
- `order_rate_high` (severity 1)
- `queue_depth_high` (severity 1)
- `memory_pressure` (severity 2)

Escalation when: single-tick severity ≥ threshold OR consecutive overload ticks ≥ limit.
Deescalation after: N consecutive clean ticks (hysteresis).

## Integration Points

### application.h — strategy_tick()

1. **Signal gate (HOT):** `ctrl_ok = risk_fsm.allows_new_orders() && exec_fsm.allows_new_orders()` — 2 boolean reads, zero branches beyond existing gate
2. **Position scale (HOT):** `qty *= risk_fsm.position_scale() * health.activity_scale` — 2 multiplies
3. **Overload + risk eval (WARM, step 10b):** Builds `OverloadSignals`, calls `evaluate_overload()` and `evaluate_risk_state()`, resolves `SystemMode` — ~20ns total
4. **UI publish (WARM):** Writes 9 control fields to `UISnapshot`
5. **Health events (COLD, every 200 ticks):** Fires `HealthDegraded`/`HealthRecovered` into control plane

### enhanced_risk_engine.h

- `evaluate_risk_state(ControlPlane&, tick_id)` — maps CB trip, drawdown, loss rate to `RiskEvent` enums
- `check_order_controlled(signal, pos, regime, RiskControlFSM&)` — respects FSM `allows_increase` flag

### smart_execution.h

- `on_signal_controlled(signal, ..., ExecControlFSM&, RiskControlFSM&)` — gates on FSM state, applies throttle
- `emergency_cancel_all_controlled(ControlPlane&, tick_id)` — notifies FSM on flatten complete

### ui_snapshot.h

New fields: `risk_state`, `exec_state`, `system_mode`, `ctrl_position_scale`, `ctrl_throttle_factor`, `ctrl_allows_new_orders`, `ctrl_allows_increase`, `ctrl_total_transitions`, `ctrl_audit_depth`

## Files Modified

| File | Changes |
|------|---------|
| `src/core/system_control.h` | **NEW** — All FSM enums, LUT transition tables, OverloadDetector, SystemModeResolver, ControlAuditTrail, ControlPlane orchestrator (~730 lines) |
| `src/risk_engine/enhanced_risk_engine.h` | Added `evaluate_risk_state()`, `check_order_controlled()` |
| `src/execution_engine/smart_execution.h` | Added `on_signal_controlled()`, `emergency_cancel_all_controlled()` |
| `src/bridge/ui_snapshot.h` | Added 9 control plane state fields |
| `src/app/application.h` | Added `ControlPlane` member, wired into strategy_tick (signal gate, position scale, warm-path eval, UI publish, health events), updated `emergency_stop()` and `reset_circuit_breaker()` |
| `CMakeLists.txt` | Added `test_stage6_system_control` and `bench_stage6_control` targets |

## Benchmark Results (Apple M4)

| Operation | Mean | p99 |
|-----------|------|-----|
| RiskFSM::apply (LUT transition) | ~21 ns | ~41 ns |
| RiskFSM::apply (no-op) | ~10 ns | ~41 ns |
| ExecFSM::apply (LUT transition) | ~24 ns | ~41 ns |
| OverloadDetector::evaluate (clean) | ~6 ns | ~41 ns |
| OverloadDetector::evaluate (overload) | ~5 ns | ~41 ns |
| SystemModeResolver::resolve | ~14 ns | ~41 ns |
| AuditTrail::record (64B) | ~6 ns | ~41 ns |
| ControlPlane::risk_event (full) | ~9 ns | ~41 ns |
| ControlPlane::evaluate_overload | ~8 ns | ~41 ns |
| ControlPlane::snapshot | ~14 ns | ~41 ns |

**Total warm-path overhead:** <50 ns per tick for full control plane evaluation.

## Test Results

- **83 new Stage 6 tests** across 11 suites: ALL PASS
- **16/16 total test suites**: ALL PASS, zero regressions
- Test suites: RiskFSM (16), ExecFSM (12), AuditTrail (7), OverloadDetector (5), SystemMode (10), ControlPlane (11), TransitionTable (3), NameFunctions (6), Layout (7), Determinism (2), Scenario (4)

## Determinism Guarantees

- All FSM transitions are pure functions of `(current_state, event) → new_state`
- No time-dependent behavior in transition logic (timestamps are metadata only)
- Overload detector uses integer counters, not floating-point EMA
- Identical event sequences produce identical state trajectories (verified by DeterminismTest)
- Audit trail records are ordered by insertion, enabling bit-exact replay
