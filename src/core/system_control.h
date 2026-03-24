#pragma once
// ─── Stage 6: Control-Plane FSMs, Overload Policy, Mode Resolver, Audit Trail ─
//
// Design principles:
//   1. Branchless LUT transitions (same pattern as order_state_machine.h)
//   2. Zero allocation on hot path — all types are trivially copyable
//   3. Deterministic: same sequence of events → same state trajectory
//   4. Auditable: every transition produces a ControlTransitionRecord
//   5. Composable: system operating mode derived from risk + exec + health
//
// Integration points:
//   - RiskControlFSM:     driven by EnhancedRiskEngine
//   - ExecControlFSM:     driven by SmartExecutionEngine / Application
//   - SystemModeResolver: combines all FSMs into a single operating mode
//   - OverloadDetector:   feeds overload events into ExecControlFSM
//   - ControlAuditTrail:  fixed-size ring of ControlTransitionRecords

#include "strong_types.h"
#include "memory_policy.h"
#include "../utils/clock.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <type_traits>

namespace bybit {

// ─── Control-Plane Domains ──────────────────────────────────────────────────

enum class ControlDomain : uint8_t {
    Risk       = 0,
    Execution  = 1,
    System     = 2,
    Overload   = 3,
    COUNT      = 4,
};

inline const char* domain_name(ControlDomain d) noexcept {
    constexpr const char* names[] = {"Risk", "Execution", "System", "Overload"};
    auto idx = static_cast<size_t>(d);
    return idx < static_cast<size_t>(ControlDomain::COUNT) ? names[idx] : "Unknown";
}

// ═══════════════════════════════════════════════════════════════════════════════
// RISK CONTROL FSM
// ═══════════════════════════════════════════════════════════════════════════════
//
// States:
//   Normal         – full risk capacity, all order types allowed
//   Cautious       – minor degradation, tighter limits applied
//   Restricted     – significant risk, reduce-only + smaller sizes
//   CircuitBreaker – auto-tripped, no new orders, cooldown active
//   Halted         – manual halt or unrecoverable, requires manual reset
//
// Transitions are driven by PnL updates, drawdown breaches, health signals.

enum class RiskState : uint8_t {
    Normal         = 0,
    Cautious       = 1,
    Restricted     = 2,
    CircuitBreaker = 3,
    Halted         = 4,
    COUNT          = 5,
};

enum class RiskEvent : uint8_t {
    PnlNormal           = 0,   // PnL within acceptable range
    DrawdownWarning      = 1,   // drawdown approaching threshold
    DrawdownBreached     = 2,   // drawdown exceeded threshold
    ConsecutiveLosses    = 3,   // N consecutive losing trades
    LossRateExceeded     = 4,   // $/min loss rate too high
    CooldownExpired      = 5,   // circuit breaker cooldown finished
    ManualReset          = 6,   // operator manual reset
    ManualHalt           = 7,   // operator manual halt
    HealthDegraded       = 8,   // strategy health dropped to Warning+
    HealthRecovered      = 9,   // strategy health back to Good+
    COUNT                = 10,
};

inline const char* risk_state_name(RiskState s) noexcept {
    constexpr const char* names[] = {
        "Normal", "Cautious", "Restricted", "CircuitBreaker", "Halted"
    };
    auto idx = static_cast<size_t>(s);
    return idx < static_cast<size_t>(RiskState::COUNT) ? names[idx] : "Unknown";
}

inline const char* risk_event_name(RiskEvent e) noexcept {
    constexpr const char* names[] = {
        "PnlNormal", "DrawdownWarning", "DrawdownBreached", "ConsecutiveLosses",
        "LossRateExceeded", "CooldownExpired", "ManualReset", "ManualHalt",
        "HealthDegraded", "HealthRecovered"
    };
    auto idx = static_cast<size_t>(e);
    return idx < static_cast<size_t>(RiskEvent::COUNT) ? names[idx] : "Unknown";
}

// Branchless LUT: RISK_TRANSITION[state][event] → new state
// Invalid transitions map to same state (no-op).
static constexpr RiskState RISK_TRANSITION
    [static_cast<size_t>(RiskState::COUNT)]
    [static_cast<size_t>(RiskEvent::COUNT)] = {
    // Normal
    {
        RiskState::Normal,          // PnlNormal         → stay
        RiskState::Cautious,        // DrawdownWarning    → Cautious
        RiskState::Restricted,      // DrawdownBreached   → Restricted
        RiskState::CircuitBreaker,  // ConsecutiveLosses  → CB
        RiskState::CircuitBreaker,  // LossRateExceeded   → CB
        RiskState::Normal,          // CooldownExpired    → no-op
        RiskState::Normal,          // ManualReset        → no-op
        RiskState::Halted,          // ManualHalt         → Halted
        RiskState::Cautious,        // HealthDegraded     → Cautious
        RiskState::Normal,          // HealthRecovered    → no-op
    },
    // Cautious
    {
        RiskState::Normal,          // PnlNormal         → recover
        RiskState::Cautious,        // DrawdownWarning    → stay
        RiskState::Restricted,      // DrawdownBreached   → escalate
        RiskState::CircuitBreaker,  // ConsecutiveLosses  → CB
        RiskState::CircuitBreaker,  // LossRateExceeded   → CB
        RiskState::Cautious,        // CooldownExpired    → no-op
        RiskState::Normal,          // ManualReset        → Normal
        RiskState::Halted,          // ManualHalt         → Halted
        RiskState::Restricted,      // HealthDegraded     → escalate
        RiskState::Normal,          // HealthRecovered    → recover
    },
    // Restricted
    {
        RiskState::Cautious,        // PnlNormal         → deescalate (one step)
        RiskState::Restricted,      // DrawdownWarning    → stay
        RiskState::Restricted,      // DrawdownBreached   → stay
        RiskState::CircuitBreaker,  // ConsecutiveLosses  → CB
        RiskState::CircuitBreaker,  // LossRateExceeded   → CB
        RiskState::Restricted,      // CooldownExpired    → no-op
        RiskState::Normal,          // ManualReset        → Normal
        RiskState::Halted,          // ManualHalt         → Halted
        RiskState::Restricted,      // HealthDegraded     → stay
        RiskState::Cautious,        // HealthRecovered    → deescalate
    },
    // CircuitBreaker
    {
        RiskState::CircuitBreaker,  // PnlNormal         → stay (must cooldown)
        RiskState::CircuitBreaker,  // DrawdownWarning    → stay
        RiskState::CircuitBreaker,  // DrawdownBreached   → stay
        RiskState::CircuitBreaker,  // ConsecutiveLosses  → stay
        RiskState::CircuitBreaker,  // LossRateExceeded   → stay
        RiskState::Cautious,        // CooldownExpired    → Cautious (not Normal)
        RiskState::Normal,          // ManualReset        → Normal
        RiskState::Halted,          // ManualHalt         → Halted
        RiskState::CircuitBreaker,  // HealthDegraded     → stay
        RiskState::CircuitBreaker,  // HealthRecovered    → stay (must cooldown)
    },
    // Halted
    {
        RiskState::Halted,          // PnlNormal         → stay
        RiskState::Halted,          // DrawdownWarning    → stay
        RiskState::Halted,          // DrawdownBreached   → stay
        RiskState::Halted,          // ConsecutiveLosses  → stay
        RiskState::Halted,          // LossRateExceeded   → stay
        RiskState::Halted,          // CooldownExpired    → stay (manual only)
        RiskState::Normal,          // ManualReset        → Normal
        RiskState::Halted,          // ManualHalt         → stay
        RiskState::Halted,          // HealthDegraded     → stay
        RiskState::Halted,          // HealthRecovered    → stay
    },
};

// ═══════════════════════════════════════════════════════════════════════════════
// EXECUTION CONTROL FSM
// ═══════════════════════════════════════════════════════════════════════════════
//
// States:
//   Active        – full execution: new orders, amends, cancels all allowed
//   Throttled     – new order rate reduced, existing orders managed normally
//   CancelOnly    – only cancel/reduce operations; no new orders
//   Suspended     – no execution operations at all; orders frozen
//   EmergencyFlat – cancel all + flatten position, then → Suspended
//

enum class ExecState : uint8_t {
    Active        = 0,
    Throttled     = 1,
    CancelOnly    = 2,
    Suspended     = 3,
    EmergencyFlat = 4,
    COUNT         = 5,
};

enum class ExecEvent : uint8_t {
    RiskEscalation    = 0,   // risk FSM escalated (e.g. → Restricted/CB)
    RiskDeescalation  = 1,   // risk FSM deescalated
    OverloadDetected  = 2,   // system overload signal
    OverloadCleared   = 3,   // overload resolved
    ManualSuspend     = 4,   // operator suspend
    ManualResume      = 5,   // operator resume
    EmergencyStop     = 6,   // emergency flatten + stop
    FlatComplete      = 7,   // position flatten completed
    COUNT             = 8,
};

inline const char* exec_state_name(ExecState s) noexcept {
    constexpr const char* names[] = {
        "Active", "Throttled", "CancelOnly", "Suspended", "EmergencyFlat"
    };
    auto idx = static_cast<size_t>(s);
    return idx < static_cast<size_t>(ExecState::COUNT) ? names[idx] : "Unknown";
}

inline const char* exec_event_name(ExecEvent e) noexcept {
    constexpr const char* names[] = {
        "RiskEscalation", "RiskDeescalation", "OverloadDetected", "OverloadCleared",
        "ManualSuspend", "ManualResume", "EmergencyStop", "FlatComplete"
    };
    auto idx = static_cast<size_t>(e);
    return idx < static_cast<size_t>(ExecEvent::COUNT) ? names[idx] : "Unknown";
}

// Branchless LUT: EXEC_TRANSITION[state][event] → new state
static constexpr ExecState EXEC_TRANSITION
    [static_cast<size_t>(ExecState::COUNT)]
    [static_cast<size_t>(ExecEvent::COUNT)] = {
    // Active
    {
        ExecState::Throttled,       // RiskEscalation    → Throttled
        ExecState::Active,          // RiskDeescalation  → no-op
        ExecState::Throttled,       // OverloadDetected  → Throttled
        ExecState::Active,          // OverloadCleared   → no-op
        ExecState::Suspended,       // ManualSuspend     → Suspended
        ExecState::Active,          // ManualResume      → no-op
        ExecState::EmergencyFlat,   // EmergencyStop     → EmergencyFlat
        ExecState::Active,          // FlatComplete      → no-op
    },
    // Throttled
    {
        ExecState::CancelOnly,      // RiskEscalation    → escalate
        ExecState::Active,          // RiskDeescalation  → recover
        ExecState::CancelOnly,      // OverloadDetected  → escalate
        ExecState::Active,          // OverloadCleared   → recover
        ExecState::Suspended,       // ManualSuspend     → Suspended
        ExecState::Active,          // ManualResume      → Active
        ExecState::EmergencyFlat,   // EmergencyStop     → EmergencyFlat
        ExecState::Throttled,       // FlatComplete      → no-op
    },
    // CancelOnly
    {
        ExecState::CancelOnly,      // RiskEscalation    → stay
        ExecState::Throttled,       // RiskDeescalation  → deescalate
        ExecState::CancelOnly,      // OverloadDetected  → stay
        ExecState::Throttled,       // OverloadCleared   → deescalate
        ExecState::Suspended,       // ManualSuspend     → Suspended
        ExecState::Active,          // ManualResume      → Active
        ExecState::EmergencyFlat,   // EmergencyStop     → EmergencyFlat
        ExecState::CancelOnly,      // FlatComplete      → no-op
    },
    // Suspended
    {
        ExecState::Suspended,       // RiskEscalation    → stay
        ExecState::Suspended,       // RiskDeescalation  → stay (manual only)
        ExecState::Suspended,       // OverloadDetected  → stay
        ExecState::Suspended,       // OverloadCleared   → stay
        ExecState::Suspended,       // ManualSuspend     → stay
        ExecState::Active,          // ManualResume      → Active
        ExecState::EmergencyFlat,   // EmergencyStop     → EmergencyFlat
        ExecState::Suspended,       // FlatComplete      → stay
    },
    // EmergencyFlat
    {
        ExecState::EmergencyFlat,   // RiskEscalation    → stay
        ExecState::EmergencyFlat,   // RiskDeescalation  → stay
        ExecState::EmergencyFlat,   // OverloadDetected  → stay
        ExecState::EmergencyFlat,   // OverloadCleared   → stay
        ExecState::EmergencyFlat,   // ManualSuspend     → stay
        ExecState::EmergencyFlat,   // ManualResume      → stay (must flatten first)
        ExecState::EmergencyFlat,   // EmergencyStop     → stay
        ExecState::Suspended,       // FlatComplete      → Suspended
    },
};

// ═══════════════════════════════════════════════════════════════════════════════
// SYSTEM OPERATING MODE (derived, not directly transitioned)
// ═══════════════════════════════════════════════════════════════════════════════

enum class SystemMode : uint8_t {
    FullTrading      = 0,  // everything nominal
    ReducedActivity  = 1,  // cautious risk or throttled exec
    CancelOnly       = 2,  // only cancel operations
    ViewOnly         = 3,  // no execution at all, monitoring only
    EmergencyShutdown = 4, // flattening and shutting down
    COUNT            = 5,
};

inline const char* system_mode_name(SystemMode m) noexcept {
    constexpr const char* names[] = {
        "FullTrading", "ReducedActivity", "CancelOnly", "ViewOnly", "EmergencyShutdown"
    };
    auto idx = static_cast<size_t>(m);
    return idx < static_cast<size_t>(SystemMode::COUNT) ? names[idx] : "Unknown";
}

// ═══════════════════════════════════════════════════════════════════════════════
// OVERLOAD SIGNALS AND POLICY
// ═══════════════════════════════════════════════════════════════════════════════

struct OverloadSignals {
    bool tick_budget_exceeded   = false;  // hot path > budget
    bool order_rate_high        = false;  // near max orders/sec
    bool queue_depth_high       = false;  // deferred work queue saturated
    bool latency_spike          = false;  // e2e latency > 3x budget
    bool memory_pressure        = false;  // arena nearing capacity
    uint64_t timestamp_ns       = 0;

    bool any() const noexcept {
        return tick_budget_exceeded || order_rate_high ||
               queue_depth_high || latency_spike || memory_pressure;
    }

    int severity() const noexcept {
        int s = 0;
        if (tick_budget_exceeded) s += 2;
        if (order_rate_high) s += 1;
        if (queue_depth_high) s += 1;
        if (latency_spike) s += 3;
        if (memory_pressure) s += 2;
        return s;
    }
};

struct OverloadConfig {
    uint64_t tick_budget_ns       = 100'000;   // 100µs total hot budget
    double   latency_spike_mult   = 3.0;       // e2e > 3x budget = spike
    size_t   queue_depth_threshold = 48;        // 48/64 = 75% capacity
    int      order_rate_threshold  = 4;         // near max_orders_per_sec
    int      escalation_threshold  = 3;         // severity >= 3 → escalate
    int      consecutive_overload_limit = 5;    // N consecutive → escalate harder
    int      recovery_ticks        = 20;        // clean ticks before deescalation
};

// ═══════════════════════════════════════════════════════════════════════════════
// CONTROL TRANSITION RECORD (audit trail entry)
// ═══════════════════════════════════════════════════════════════════════════════
//
// Fixed-size, trivially copyable. Fits 2 per Apple Silicon cache line.

struct alignas(64) ControlTransitionRecord {
    uint64_t      timestamp_ns  = 0;         // 8
    uint64_t      tick_id       = 0;         // 8
    ControlDomain domain        = ControlDomain::Risk; // 1
    uint8_t       from_state    = 0;         // 1
    uint8_t       to_state      = 0;         // 1
    uint8_t       event         = 0;         // 1
    uint8_t       severity      = 0;         // 1  (0=info, 1=warn, 2=error, 3=critical)
    uint8_t       _pad[3]       = {};        // 3
    char          reason[40]    = {};        // 40  static string, null-terminated
    // Total: 64 bytes

    void set_reason(const char* r) noexcept {
        if (r) {
            size_t len = 0;
            while (len < 39 && r[len]) { reason[len] = r[len]; ++len; }
            reason[len] = '\0';
        } else {
            reason[0] = '\0';
        }
    }
};
static_assert(sizeof(ControlTransitionRecord) == 64,
              "ControlTransitionRecord must be 64 bytes");
static_assert(std::is_trivially_copyable_v<ControlTransitionRecord>);

// ═══════════════════════════════════════════════════════════════════════════════
// CONTROL AUDIT TRAIL (fixed-size ring buffer)
// ═══════════════════════════════════════════════════════════════════════════════

class ControlAuditTrail {
public:
    static constexpr size_t CAPACITY = 256;
    static constexpr size_t MASK = CAPACITY - 1;

    ControlAuditTrail() noexcept = default;

    void record(const ControlTransitionRecord& rec) noexcept {
        entries_[head_ & MASK] = rec;
        ++head_;
        if (count_ < CAPACITY) ++count_;
    }

    // Most recent record (head - 1)
    const ControlTransitionRecord& last() const noexcept {
        return entries_[(head_ - 1) & MASK];
    }

    // Read record at index (0 = oldest available)
    const ControlTransitionRecord& at(size_t idx) const noexcept {
        size_t start = (count_ < CAPACITY) ? 0 : (head_ - CAPACITY);
        return entries_[(start + idx) & MASK];
    }

    size_t count() const noexcept { return count_; }
    size_t head() const noexcept { return head_; }

    // Count transitions in the last N nanoseconds for a given domain
    size_t count_recent(ControlDomain domain, uint64_t window_ns,
                        uint64_t now_ns) const noexcept {
        size_t n = 0;
        uint64_t cutoff = (now_ns > window_ns) ? (now_ns - window_ns) : 0;
        for (size_t i = 0; i < count_; ++i) {
            const auto& e = at(i);
            if (e.timestamp_ns < cutoff) continue;
            if (e.domain == domain) ++n;
        }
        return n;
    }

    void clear() noexcept {
        head_ = 0;
        count_ = 0;
    }

private:
    std::array<ControlTransitionRecord, CAPACITY> entries_{};
    size_t head_  = 0;
    size_t count_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// RISK CONTROL FSM ENGINE
// ═══════════════════════════════════════════════════════════════════════════════

struct RiskControlSnapshot {
    RiskState     state             = RiskState::Normal;
    uint64_t      state_enter_ns    = 0;
    uint64_t      last_transition_ns = 0;
    uint32_t      transitions_total = 0;
    double        position_scale    = 1.0;   // multiplier applied to position sizing
    bool          allows_new_orders = true;
    bool          allows_increase   = true;  // can increase position
};

class RiskControlFSM {
public:
    RiskControlFSM() noexcept {
        snap_.state_enter_ns = Clock::now_ns();
    }

    // Apply an event. Returns true if state changed.
    bool apply(RiskEvent event, uint64_t tick_id,
               ControlAuditTrail& trail,
               const char* reason = nullptr) noexcept {
        RiskState old_state = snap_.state;
        RiskState new_state = RISK_TRANSITION
            [static_cast<size_t>(old_state)]
            [static_cast<size_t>(event)];

        if (new_state == old_state) return false;

        uint64_t now = Clock::now_ns();
        snap_.state = new_state;
        snap_.last_transition_ns = now;
        snap_.state_enter_ns = now;
        ++snap_.transitions_total;

        // Update derived flags
        update_derived();

        // Record transition
        ControlTransitionRecord rec{};
        rec.timestamp_ns = now;
        rec.tick_id = tick_id;
        rec.domain = ControlDomain::Risk;
        rec.from_state = static_cast<uint8_t>(old_state);
        rec.to_state = static_cast<uint8_t>(new_state);
        rec.event = static_cast<uint8_t>(event);
        rec.severity = compute_severity(new_state);
        rec.set_reason(reason ? reason : risk_event_name(event));
        trail.record(rec);

        return true;
    }

    const RiskControlSnapshot& snapshot() const noexcept { return snap_; }
    RiskState state() const noexcept { return snap_.state; }

    // Compatibility: query flags directly
    bool allows_new_orders() const noexcept { return snap_.allows_new_orders; }
    bool allows_increase() const noexcept { return snap_.allows_increase; }
    double position_scale() const noexcept { return snap_.position_scale; }

private:
    void update_derived() noexcept {
        switch (snap_.state) {
            case RiskState::Normal:
                snap_.position_scale = 1.0;
                snap_.allows_new_orders = true;
                snap_.allows_increase = true;
                break;
            case RiskState::Cautious:
                snap_.position_scale = 0.7;
                snap_.allows_new_orders = true;
                snap_.allows_increase = true;
                break;
            case RiskState::Restricted:
                snap_.position_scale = 0.3;
                snap_.allows_new_orders = true;
                snap_.allows_increase = false;  // reduce-only
                break;
            case RiskState::CircuitBreaker:
                snap_.position_scale = 0.0;
                snap_.allows_new_orders = false;
                snap_.allows_increase = false;
                break;
            case RiskState::Halted:
                snap_.position_scale = 0.0;
                snap_.allows_new_orders = false;
                snap_.allows_increase = false;
                break;
            default:
                break;
        }
    }

    static uint8_t compute_severity(RiskState s) noexcept {
        switch (s) {
            case RiskState::Normal:         return 0;
            case RiskState::Cautious:       return 1;
            case RiskState::Restricted:     return 2;
            case RiskState::CircuitBreaker: return 3;
            case RiskState::Halted:         return 3;
            default: return 0;
        }
    }

    RiskControlSnapshot snap_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// EXECUTION CONTROL FSM ENGINE
// ═══════════════════════════════════════════════════════════════════════════════

struct ExecControlSnapshot {
    ExecState     state               = ExecState::Active;
    uint64_t      state_enter_ns      = 0;
    uint64_t      last_transition_ns  = 0;
    uint32_t      transitions_total   = 0;
    double        throttle_factor     = 1.0;   // 0.0 = no orders, 1.0 = full rate
    bool          allows_new_orders   = true;
    bool          allows_amend        = true;
    bool          allows_cancel       = true;
};

class ExecControlFSM {
public:
    ExecControlFSM() noexcept {
        snap_.state_enter_ns = Clock::now_ns();
    }

    // Apply an event. Returns true if state changed.
    bool apply(ExecEvent event, uint64_t tick_id,
               ControlAuditTrail& trail,
               const char* reason = nullptr) noexcept {
        ExecState old_state = snap_.state;
        ExecState new_state = EXEC_TRANSITION
            [static_cast<size_t>(old_state)]
            [static_cast<size_t>(event)];

        if (new_state == old_state) return false;

        uint64_t now = Clock::now_ns();
        snap_.state = new_state;
        snap_.last_transition_ns = now;
        snap_.state_enter_ns = now;
        ++snap_.transitions_total;

        // Update derived flags
        update_derived();

        // Record transition
        ControlTransitionRecord rec{};
        rec.timestamp_ns = now;
        rec.tick_id = tick_id;
        rec.domain = ControlDomain::Execution;
        rec.from_state = static_cast<uint8_t>(old_state);
        rec.to_state = static_cast<uint8_t>(new_state);
        rec.event = static_cast<uint8_t>(event);
        rec.severity = compute_severity(new_state);
        rec.set_reason(reason ? reason : exec_event_name(event));
        trail.record(rec);

        return true;
    }

    const ExecControlSnapshot& snapshot() const noexcept { return snap_; }
    ExecState state() const noexcept { return snap_.state; }

    // Compatibility: query flags directly
    bool allows_new_orders() const noexcept { return snap_.allows_new_orders; }
    bool allows_amend() const noexcept { return snap_.allows_amend; }
    bool allows_cancel() const noexcept { return snap_.allows_cancel; }
    double throttle_factor() const noexcept { return snap_.throttle_factor; }

private:
    void update_derived() noexcept {
        switch (snap_.state) {
            case ExecState::Active:
                snap_.throttle_factor = 1.0;
                snap_.allows_new_orders = true;
                snap_.allows_amend = true;
                snap_.allows_cancel = true;
                break;
            case ExecState::Throttled:
                snap_.throttle_factor = 0.5;
                snap_.allows_new_orders = true;
                snap_.allows_amend = true;
                snap_.allows_cancel = true;
                break;
            case ExecState::CancelOnly:
                snap_.throttle_factor = 0.0;
                snap_.allows_new_orders = false;
                snap_.allows_amend = false;
                snap_.allows_cancel = true;
                break;
            case ExecState::Suspended:
                snap_.throttle_factor = 0.0;
                snap_.allows_new_orders = false;
                snap_.allows_amend = false;
                snap_.allows_cancel = false;
                break;
            case ExecState::EmergencyFlat:
                snap_.throttle_factor = 0.0;
                snap_.allows_new_orders = false;
                snap_.allows_amend = false;
                snap_.allows_cancel = true;  // must cancel to flatten
                break;
            default:
                break;
        }
    }

    static uint8_t compute_severity(ExecState s) noexcept {
        switch (s) {
            case ExecState::Active:        return 0;
            case ExecState::Throttled:     return 1;
            case ExecState::CancelOnly:    return 2;
            case ExecState::Suspended:     return 2;
            case ExecState::EmergencyFlat: return 3;
            default: return 0;
        }
    }

    ExecControlSnapshot snap_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// OVERLOAD DETECTOR
// ═══════════════════════════════════════════════════════════════════════════════
//
// Evaluates overload signals and determines whether to escalate/deescalate
// the execution control FSM. Tracks consecutive overload ticks for hysteresis.

class OverloadDetector {
public:
    explicit OverloadDetector(const OverloadConfig& cfg = {}) noexcept : cfg_(cfg) {}

    struct OverloadResult {
        bool should_escalate    = false;
        bool should_deescalate  = false;
        int  severity           = 0;
        int  consecutive_count  = 0;
    };

    OverloadResult evaluate(const OverloadSignals& signals) noexcept {
        OverloadResult result;
        result.severity = signals.severity();

        if (signals.any()) {
            ++consecutive_overload_;
            clean_ticks_ = 0;
        } else {
            consecutive_overload_ = 0;
            ++clean_ticks_;
        }

        result.consecutive_count = consecutive_overload_;

        // Escalate if severity is high enough or consecutive overloads exceed limit
        if (result.severity >= cfg_.escalation_threshold ||
            consecutive_overload_ >= cfg_.consecutive_overload_limit) {
            result.should_escalate = true;
        }

        // Deescalate only after enough consecutive clean ticks
        if (clean_ticks_ >= cfg_.recovery_ticks && !signals.any()) {
            result.should_deescalate = true;
        }

        last_signals_ = signals;
        return result;
    }

    const OverloadSignals& last_signals() const noexcept { return last_signals_; }
    int consecutive_overload() const noexcept { return consecutive_overload_; }
    int clean_ticks() const noexcept { return clean_ticks_; }
    const OverloadConfig& config() const noexcept { return cfg_; }

    void reset() noexcept {
        consecutive_overload_ = 0;
        clean_ticks_ = 0;
        last_signals_ = {};
    }

private:
    OverloadConfig cfg_;
    OverloadSignals last_signals_;
    int consecutive_overload_ = 0;
    int clean_ticks_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// SYSTEM MODE RESOLVER
// ═══════════════════════════════════════════════════════════════════════════════
//
// Derives the composite SystemMode from:
//   - RiskControlFSM state
//   - ExecControlFSM state
//   - StrategyHealthLevel (imported as uint8_t to avoid circular dependency)
//
// The mode is the WORST (most restrictive) of all inputs.
// Zero-cost: pure function, no state.

struct SystemModeSnapshot {
    SystemMode    mode              = SystemMode::FullTrading;
    RiskState     risk_state        = RiskState::Normal;
    ExecState     exec_state        = ExecState::Active;
    uint8_t       health_level      = 0;   // StrategyHealthLevel cast
    double        position_scale    = 1.0; // combined scale from risk + health
    double        throttle_factor   = 1.0; // from exec FSM
    bool          allows_new_orders = true;
    bool          allows_increase   = true;
    bool          allows_cancel     = true;
    uint64_t      resolved_ns       = 0;
};

class SystemModeResolver {
public:
    // Resolve system mode from individual FSM states and health level.
    // health_level: 0=Excellent, 1=Good, 2=Warning, 3=Critical, 4=Halted
    // health_activity_scale: [0,1] from StrategyHealthMonitor
    static SystemModeSnapshot resolve(
            const RiskControlFSM& risk_fsm,
            const ExecControlFSM& exec_fsm,
            uint8_t health_level,
            double health_activity_scale) noexcept {

        SystemModeSnapshot snap;
        snap.risk_state = risk_fsm.state();
        snap.exec_state = exec_fsm.state();
        snap.health_level = health_level;
        snap.resolved_ns = Clock::now_ns();

        // Position scale: product of risk scale and health activity scale
        snap.position_scale = risk_fsm.position_scale() * health_activity_scale;
        snap.throttle_factor = exec_fsm.throttle_factor();

        // Capability flags: AND of risk + exec
        snap.allows_new_orders = risk_fsm.allows_new_orders() && exec_fsm.allows_new_orders();
        snap.allows_increase = risk_fsm.allows_increase() && exec_fsm.allows_new_orders();
        snap.allows_cancel = exec_fsm.allows_cancel();

        // Derive composite mode: take the most restrictive
        SystemMode risk_mode = risk_state_to_mode(snap.risk_state);
        SystemMode exec_mode = exec_state_to_mode(snap.exec_state);
        SystemMode health_mode = health_to_mode(health_level);

        // Most restrictive = highest enum value
        snap.mode = static_cast<SystemMode>(
            std::max({static_cast<uint8_t>(risk_mode),
                      static_cast<uint8_t>(exec_mode),
                      static_cast<uint8_t>(health_mode)}));

        return snap;
    }

private:
    static SystemMode risk_state_to_mode(RiskState s) noexcept {
        switch (s) {
            case RiskState::Normal:         return SystemMode::FullTrading;
            case RiskState::Cautious:       return SystemMode::ReducedActivity;
            case RiskState::Restricted:     return SystemMode::CancelOnly;
            case RiskState::CircuitBreaker: return SystemMode::CancelOnly;
            case RiskState::Halted:         return SystemMode::ViewOnly;
            default: return SystemMode::ViewOnly;
        }
    }

    static SystemMode exec_state_to_mode(ExecState s) noexcept {
        switch (s) {
            case ExecState::Active:        return SystemMode::FullTrading;
            case ExecState::Throttled:     return SystemMode::ReducedActivity;
            case ExecState::CancelOnly:    return SystemMode::CancelOnly;
            case ExecState::Suspended:     return SystemMode::ViewOnly;
            case ExecState::EmergencyFlat: return SystemMode::EmergencyShutdown;
            default: return SystemMode::ViewOnly;
        }
    }

    static SystemMode health_to_mode(uint8_t level) noexcept {
        // 0=Excellent, 1=Good → FullTrading
        // 2=Warning → ReducedActivity
        // 3=Critical → CancelOnly
        // 4=Halted → ViewOnly
        switch (level) {
            case 0: case 1: return SystemMode::FullTrading;
            case 2: return SystemMode::ReducedActivity;
            case 3: return SystemMode::CancelOnly;
            case 4: return SystemMode::ViewOnly;
            default: return SystemMode::ViewOnly;
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// CONTROL PLANE ORCHESTRATOR
// ═══════════════════════════════════════════════════════════════════════════════
//
// Convenience aggregate: owns Risk FSM, Exec FSM, Overload Detector,
// Audit Trail, and Mode Resolver. Single point of integration for Application.

struct ControlPlaneSnapshot {
    RiskControlSnapshot   risk;
    ExecControlSnapshot   exec;
    SystemModeSnapshot    system;
    OverloadSignals       last_overload;
    uint64_t              total_transitions = 0;
    uint64_t              audit_trail_depth = 0;
};

class ControlPlane {
public:
    explicit ControlPlane(const OverloadConfig& overload_cfg = {}) noexcept
        : overload_(overload_cfg) {}

    // ── Risk events ──────────────────────────────────────────────────────
    bool risk_event(RiskEvent event, uint64_t tick_id,
                    const char* reason = nullptr) noexcept {
        bool changed = risk_fsm_.apply(event, tick_id, trail_, reason);
        if (changed) {
            // Propagate risk escalation/deescalation to exec FSM
            auto rs = risk_fsm_.state();
            if (rs >= RiskState::Restricted) {
                exec_fsm_.apply(ExecEvent::RiskEscalation, tick_id, trail_,
                                "risk_state_escalated");
            } else if (rs <= RiskState::Cautious &&
                       exec_fsm_.state() != ExecState::Suspended &&
                       exec_fsm_.state() != ExecState::EmergencyFlat) {
                exec_fsm_.apply(ExecEvent::RiskDeescalation, tick_id, trail_,
                                "risk_state_deescalated");
            }
        }
        return changed;
    }

    // ── Exec events ──────────────────────────────────────────────────────
    bool exec_event(ExecEvent event, uint64_t tick_id,
                    const char* reason = nullptr) noexcept {
        return exec_fsm_.apply(event, tick_id, trail_, reason);
    }

    // ── Overload evaluation (call per tick) ──────────────────────────────
    void evaluate_overload(const OverloadSignals& signals, uint64_t tick_id) noexcept {
        auto result = overload_.evaluate(signals);
        if (result.should_escalate) {
            exec_fsm_.apply(ExecEvent::OverloadDetected, tick_id, trail_,
                            "overload_escalation");
        } else if (result.should_deescalate) {
            exec_fsm_.apply(ExecEvent::OverloadCleared, tick_id, trail_,
                            "overload_cleared");
        }
    }

    // ── Mode resolution ──────────────────────────────────────────────────
    SystemModeSnapshot resolve_mode(uint8_t health_level,
                                    double health_activity_scale) const noexcept {
        return SystemModeResolver::resolve(
            risk_fsm_, exec_fsm_, health_level, health_activity_scale);
    }

    // ── Emergency stop ───────────────────────────────────────────────────
    void emergency_stop(uint64_t tick_id) noexcept {
        risk_fsm_.apply(RiskEvent::ManualHalt, tick_id, trail_, "emergency_stop");
        exec_fsm_.apply(ExecEvent::EmergencyStop, tick_id, trail_, "emergency_stop");
    }

    // ── Manual reset ─────────────────────────────────────────────────────
    void manual_reset(uint64_t tick_id) noexcept {
        risk_fsm_.apply(RiskEvent::ManualReset, tick_id, trail_, "manual_reset");
        exec_fsm_.apply(ExecEvent::ManualResume, tick_id, trail_, "manual_reset");
    }

    // ── Accessors ────────────────────────────────────────────────────────
    const RiskControlFSM& risk_fsm() const noexcept { return risk_fsm_; }
    const ExecControlFSM& exec_fsm() const noexcept { return exec_fsm_; }
    const ControlAuditTrail& audit_trail() const noexcept { return trail_; }
    const OverloadDetector& overload_detector() const noexcept { return overload_; }

    RiskControlFSM& risk_fsm_mut() noexcept { return risk_fsm_; }
    ExecControlFSM& exec_fsm_mut() noexcept { return exec_fsm_; }

    ControlPlaneSnapshot snapshot(uint8_t health_level = 0,
                                  double health_activity_scale = 1.0) const noexcept {
        ControlPlaneSnapshot snap;
        snap.risk = risk_fsm_.snapshot();
        snap.exec = exec_fsm_.snapshot();
        snap.system = resolve_mode(health_level, health_activity_scale);
        snap.last_overload = overload_.last_signals();
        snap.total_transitions = snap.risk.transitions_total + snap.exec.transitions_total;
        snap.audit_trail_depth = trail_.count();
        return snap;
    }

private:
    RiskControlFSM   risk_fsm_;
    ExecControlFSM   exec_fsm_;
    OverloadDetector  overload_;
    ControlAuditTrail trail_;
};

} // namespace bybit
