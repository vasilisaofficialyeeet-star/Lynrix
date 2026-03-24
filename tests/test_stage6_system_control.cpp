#include <gtest/gtest.h>

// Stage 6: Control-Plane FSMs, Overload Policy, Mode Resolver, Audit Trail
#include "../src/core/system_control.h"
#include "../src/analytics/strategy_health.h"

using namespace bybit;

// ═══════════════════════════════════════════════════════════════════════════════
// Risk FSM Tests
// ═══════════════════════════════════════════════════════════════════════════════

class RiskFSMTest : public ::testing::Test {
protected:
    ControlAuditTrail trail;
    RiskControlFSM fsm;
};

TEST_F(RiskFSMTest, InitialStateIsNormal) {
    EXPECT_EQ(fsm.state(), RiskState::Normal);
    EXPECT_TRUE(fsm.allows_new_orders());
    EXPECT_TRUE(fsm.allows_increase());
    EXPECT_DOUBLE_EQ(fsm.position_scale(), 1.0);
}

TEST_F(RiskFSMTest, DrawdownWarningTransitionsToCautious) {
    bool changed = fsm.apply(RiskEvent::DrawdownWarning, 1, trail, "dd_approaching");
    EXPECT_TRUE(changed);
    EXPECT_EQ(fsm.state(), RiskState::Cautious);
    EXPECT_DOUBLE_EQ(fsm.position_scale(), 0.7);
    EXPECT_TRUE(fsm.allows_new_orders());
    EXPECT_TRUE(fsm.allows_increase());
}

TEST_F(RiskFSMTest, DrawdownBreachedTransitionsToRestricted) {
    fsm.apply(RiskEvent::DrawdownWarning, 1, trail);
    EXPECT_EQ(fsm.state(), RiskState::Cautious);

    bool changed = fsm.apply(RiskEvent::DrawdownBreached, 2, trail, "dd_exceeded");
    EXPECT_TRUE(changed);
    EXPECT_EQ(fsm.state(), RiskState::Restricted);
    EXPECT_DOUBLE_EQ(fsm.position_scale(), 0.3);
    EXPECT_TRUE(fsm.allows_new_orders());
    EXPECT_FALSE(fsm.allows_increase()); // reduce-only
}

TEST_F(RiskFSMTest, ConsecutiveLossesTripsCircuitBreaker) {
    bool changed = fsm.apply(RiskEvent::ConsecutiveLosses, 1, trail, "10_consec_losses");
    EXPECT_TRUE(changed);
    EXPECT_EQ(fsm.state(), RiskState::CircuitBreaker);
    EXPECT_DOUBLE_EQ(fsm.position_scale(), 0.0);
    EXPECT_FALSE(fsm.allows_new_orders());
    EXPECT_FALSE(fsm.allows_increase());
}

TEST_F(RiskFSMTest, LossRateTripsCircuitBreaker) {
    bool changed = fsm.apply(RiskEvent::LossRateExceeded, 1, trail, "loss_rate_50/min");
    EXPECT_TRUE(changed);
    EXPECT_EQ(fsm.state(), RiskState::CircuitBreaker);
}

TEST_F(RiskFSMTest, CooldownExpiredFromCBGoesToCautious) {
    fsm.apply(RiskEvent::ConsecutiveLosses, 1, trail);
    EXPECT_EQ(fsm.state(), RiskState::CircuitBreaker);

    bool changed = fsm.apply(RiskEvent::CooldownExpired, 2, trail, "cb_cooldown_done");
    EXPECT_TRUE(changed);
    EXPECT_EQ(fsm.state(), RiskState::Cautious);
    EXPECT_DOUBLE_EQ(fsm.position_scale(), 0.7);
}

TEST_F(RiskFSMTest, ManualResetFromAnyStateToNormal) {
    // From CircuitBreaker
    fsm.apply(RiskEvent::ConsecutiveLosses, 1, trail);
    EXPECT_EQ(fsm.state(), RiskState::CircuitBreaker);

    bool changed = fsm.apply(RiskEvent::ManualReset, 2, trail, "operator_reset");
    EXPECT_TRUE(changed);
    EXPECT_EQ(fsm.state(), RiskState::Normal);
    EXPECT_DOUBLE_EQ(fsm.position_scale(), 1.0);
}

TEST_F(RiskFSMTest, ManualResetFromHaltedToNormal) {
    fsm.apply(RiskEvent::ManualHalt, 1, trail);
    EXPECT_EQ(fsm.state(), RiskState::Halted);

    bool changed = fsm.apply(RiskEvent::ManualReset, 2, trail);
    EXPECT_TRUE(changed);
    EXPECT_EQ(fsm.state(), RiskState::Normal);
}

TEST_F(RiskFSMTest, HaltedIgnoresAllExceptManualReset) {
    fsm.apply(RiskEvent::ManualHalt, 1, trail);
    EXPECT_EQ(fsm.state(), RiskState::Halted);

    // All these should be no-ops
    EXPECT_FALSE(fsm.apply(RiskEvent::PnlNormal, 2, trail));
    EXPECT_FALSE(fsm.apply(RiskEvent::DrawdownWarning, 3, trail));
    EXPECT_FALSE(fsm.apply(RiskEvent::CooldownExpired, 4, trail));
    EXPECT_FALSE(fsm.apply(RiskEvent::HealthRecovered, 5, trail));
    EXPECT_EQ(fsm.state(), RiskState::Halted);

    // Only ManualReset works
    EXPECT_TRUE(fsm.apply(RiskEvent::ManualReset, 6, trail));
    EXPECT_EQ(fsm.state(), RiskState::Normal);
}

TEST_F(RiskFSMTest, PnlNormalRecoveryPath) {
    // Normal → Cautious → Restricted → deescalate back
    fsm.apply(RiskEvent::DrawdownWarning, 1, trail);
    fsm.apply(RiskEvent::DrawdownBreached, 2, trail);
    EXPECT_EQ(fsm.state(), RiskState::Restricted);

    // PnlNormal deescalates one step
    fsm.apply(RiskEvent::PnlNormal, 3, trail);
    EXPECT_EQ(fsm.state(), RiskState::Cautious);

    fsm.apply(RiskEvent::PnlNormal, 4, trail);
    EXPECT_EQ(fsm.state(), RiskState::Normal);
}

TEST_F(RiskFSMTest, HealthDegradedEscalation) {
    // Normal → Cautious on health degraded
    fsm.apply(RiskEvent::HealthDegraded, 1, trail, "WARNING");
    EXPECT_EQ(fsm.state(), RiskState::Cautious);

    // Cautious → Restricted on further health degradation
    fsm.apply(RiskEvent::HealthDegraded, 2, trail, "CRITICAL");
    EXPECT_EQ(fsm.state(), RiskState::Restricted);
}

TEST_F(RiskFSMTest, HealthRecoveredDeescalation) {
    fsm.apply(RiskEvent::HealthDegraded, 1, trail);
    fsm.apply(RiskEvent::HealthDegraded, 2, trail);
    EXPECT_EQ(fsm.state(), RiskState::Restricted);

    fsm.apply(RiskEvent::HealthRecovered, 3, trail, "GOOD");
    EXPECT_EQ(fsm.state(), RiskState::Cautious);

    fsm.apply(RiskEvent::HealthRecovered, 4, trail, "EXCELLENT");
    EXPECT_EQ(fsm.state(), RiskState::Normal);
}

TEST_F(RiskFSMTest, NoOpTransitionReturnsFalse) {
    // PnlNormal when already Normal → no-op
    EXPECT_FALSE(fsm.apply(RiskEvent::PnlNormal, 1, trail));
    EXPECT_EQ(fsm.state(), RiskState::Normal);
    EXPECT_EQ(trail.count(), 0u); // no record for no-op
}

TEST_F(RiskFSMTest, CircuitBreakerIgnoresPnlNormal) {
    fsm.apply(RiskEvent::ConsecutiveLosses, 1, trail);
    EXPECT_EQ(fsm.state(), RiskState::CircuitBreaker);

    // PnlNormal should NOT deescalate CB — must cooldown first
    EXPECT_FALSE(fsm.apply(RiskEvent::PnlNormal, 2, trail));
    EXPECT_EQ(fsm.state(), RiskState::CircuitBreaker);
}

TEST_F(RiskFSMTest, TransitionCountTracked) {
    fsm.apply(RiskEvent::DrawdownWarning, 1, trail);
    fsm.apply(RiskEvent::DrawdownBreached, 2, trail);
    fsm.apply(RiskEvent::PnlNormal, 3, trail);
    EXPECT_EQ(fsm.snapshot().transitions_total, 3u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Execution FSM Tests
// ═══════════════════════════════════════════════════════════════════════════════

class ExecFSMTest : public ::testing::Test {
protected:
    ControlAuditTrail trail;
    ExecControlFSM fsm;
};

TEST_F(ExecFSMTest, InitialStateIsActive) {
    EXPECT_EQ(fsm.state(), ExecState::Active);
    EXPECT_TRUE(fsm.allows_new_orders());
    EXPECT_TRUE(fsm.allows_amend());
    EXPECT_TRUE(fsm.allows_cancel());
    EXPECT_DOUBLE_EQ(fsm.throttle_factor(), 1.0);
}

TEST_F(ExecFSMTest, RiskEscalationThrottles) {
    bool changed = fsm.apply(ExecEvent::RiskEscalation, 1, trail);
    EXPECT_TRUE(changed);
    EXPECT_EQ(fsm.state(), ExecState::Throttled);
    EXPECT_DOUBLE_EQ(fsm.throttle_factor(), 0.5);
    EXPECT_TRUE(fsm.allows_new_orders());
}

TEST_F(ExecFSMTest, DoubleEscalationToCancelOnly) {
    fsm.apply(ExecEvent::RiskEscalation, 1, trail);
    EXPECT_EQ(fsm.state(), ExecState::Throttled);

    fsm.apply(ExecEvent::RiskEscalation, 2, trail);
    EXPECT_EQ(fsm.state(), ExecState::CancelOnly);
    EXPECT_FALSE(fsm.allows_new_orders());
    EXPECT_FALSE(fsm.allows_amend());
    EXPECT_TRUE(fsm.allows_cancel());
    EXPECT_DOUBLE_EQ(fsm.throttle_factor(), 0.0);
}

TEST_F(ExecFSMTest, DeescalationPath) {
    fsm.apply(ExecEvent::RiskEscalation, 1, trail);
    fsm.apply(ExecEvent::RiskEscalation, 2, trail);
    EXPECT_EQ(fsm.state(), ExecState::CancelOnly);

    fsm.apply(ExecEvent::RiskDeescalation, 3, trail);
    EXPECT_EQ(fsm.state(), ExecState::Throttled);

    fsm.apply(ExecEvent::RiskDeescalation, 4, trail);
    EXPECT_EQ(fsm.state(), ExecState::Active);
}

TEST_F(ExecFSMTest, OverloadEscalation) {
    fsm.apply(ExecEvent::OverloadDetected, 1, trail);
    EXPECT_EQ(fsm.state(), ExecState::Throttled);

    fsm.apply(ExecEvent::OverloadDetected, 2, trail);
    EXPECT_EQ(fsm.state(), ExecState::CancelOnly);
}

TEST_F(ExecFSMTest, OverloadCleared) {
    fsm.apply(ExecEvent::OverloadDetected, 1, trail);
    fsm.apply(ExecEvent::OverloadDetected, 2, trail);
    EXPECT_EQ(fsm.state(), ExecState::CancelOnly);

    fsm.apply(ExecEvent::OverloadCleared, 3, trail);
    EXPECT_EQ(fsm.state(), ExecState::Throttled);

    fsm.apply(ExecEvent::OverloadCleared, 4, trail);
    EXPECT_EQ(fsm.state(), ExecState::Active);
}

TEST_F(ExecFSMTest, ManualSuspend) {
    fsm.apply(ExecEvent::ManualSuspend, 1, trail);
    EXPECT_EQ(fsm.state(), ExecState::Suspended);
    EXPECT_FALSE(fsm.allows_new_orders());
    EXPECT_FALSE(fsm.allows_amend());
    EXPECT_FALSE(fsm.allows_cancel());
}

TEST_F(ExecFSMTest, SuspendedIgnoresEscalation) {
    fsm.apply(ExecEvent::ManualSuspend, 1, trail);
    EXPECT_FALSE(fsm.apply(ExecEvent::RiskEscalation, 2, trail));
    EXPECT_FALSE(fsm.apply(ExecEvent::OverloadDetected, 3, trail));
    EXPECT_FALSE(fsm.apply(ExecEvent::RiskDeescalation, 4, trail));
    EXPECT_EQ(fsm.state(), ExecState::Suspended);
}

TEST_F(ExecFSMTest, SuspendedResumesToActive) {
    fsm.apply(ExecEvent::ManualSuspend, 1, trail);
    fsm.apply(ExecEvent::ManualResume, 2, trail);
    EXPECT_EQ(fsm.state(), ExecState::Active);
}

TEST_F(ExecFSMTest, EmergencyStopFromAnyState) {
    // From Active
    fsm.apply(ExecEvent::EmergencyStop, 1, trail);
    EXPECT_EQ(fsm.state(), ExecState::EmergencyFlat);
    EXPECT_FALSE(fsm.allows_new_orders());
    EXPECT_TRUE(fsm.allows_cancel()); // must cancel to flatten
}

TEST_F(ExecFSMTest, EmergencyFlatIgnoresResume) {
    fsm.apply(ExecEvent::EmergencyStop, 1, trail);
    EXPECT_FALSE(fsm.apply(ExecEvent::ManualResume, 2, trail));
    EXPECT_EQ(fsm.state(), ExecState::EmergencyFlat);
}

TEST_F(ExecFSMTest, FlatCompleteGoesToSuspended) {
    fsm.apply(ExecEvent::EmergencyStop, 1, trail);
    fsm.apply(ExecEvent::FlatComplete, 2, trail);
    EXPECT_EQ(fsm.state(), ExecState::Suspended);
    // Then manual resume to active
    fsm.apply(ExecEvent::ManualResume, 3, trail);
    EXPECT_EQ(fsm.state(), ExecState::Active);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Audit Trail Tests
// ═══════════════════════════════════════════════════════════════════════════════

class AuditTrailTest : public ::testing::Test {
protected:
    ControlAuditTrail trail;
};

TEST_F(AuditTrailTest, EmptyTrail) {
    EXPECT_EQ(trail.count(), 0u);
}

TEST_F(AuditTrailTest, RecordAndRetrieve) {
    ControlTransitionRecord rec;
    rec.timestamp_ns = 1000;
    rec.tick_id = 42;
    rec.domain = ControlDomain::Risk;
    rec.from_state = 0;
    rec.to_state = 1;
    rec.event = 1;
    rec.severity = 1;
    rec.set_reason("drawdown_warning");

    trail.record(rec);
    EXPECT_EQ(trail.count(), 1u);

    auto& last = trail.last();
    EXPECT_EQ(last.tick_id, 42u);
    EXPECT_EQ(last.domain, ControlDomain::Risk);
    EXPECT_EQ(last.from_state, 0);
    EXPECT_EQ(last.to_state, 1);
    EXPECT_STREQ(last.reason, "drawdown_warning");
}

TEST_F(AuditTrailTest, RingBufferWraps) {
    for (size_t i = 0; i < ControlAuditTrail::CAPACITY + 10; ++i) {
        ControlTransitionRecord rec;
        rec.tick_id = i;
        rec.timestamp_ns = i * 1000;
        trail.record(rec);
    }
    EXPECT_EQ(trail.count(), ControlAuditTrail::CAPACITY);
    // Oldest should be entry 10 (first 10 overwritten)
    EXPECT_EQ(trail.at(0).tick_id, 10u);
    // Last should be the most recent
    EXPECT_EQ(trail.last().tick_id, ControlAuditTrail::CAPACITY + 9);
}

TEST_F(AuditTrailTest, CountRecentByDomain) {
    uint64_t now = 10'000'000'000ULL; // 10 seconds
    for (int i = 0; i < 5; ++i) {
        ControlTransitionRecord rec;
        rec.timestamp_ns = now - 500'000'000ULL + i * 100'000'000ULL; // within 1 sec window
        rec.domain = ControlDomain::Risk;
        trail.record(rec);
    }
    for (int i = 0; i < 3; ++i) {
        ControlTransitionRecord rec;
        rec.timestamp_ns = now - 500'000'000ULL + i * 100'000'000ULL;
        rec.domain = ControlDomain::Execution;
        trail.record(rec);
    }
    EXPECT_EQ(trail.count_recent(ControlDomain::Risk, 1'000'000'000ULL, now), 5u);
    EXPECT_EQ(trail.count_recent(ControlDomain::Execution, 1'000'000'000ULL, now), 3u);
}

TEST_F(AuditTrailTest, ReasonTruncation) {
    ControlTransitionRecord rec;
    rec.set_reason("this_is_a_very_long_reason_string_that_exceeds_39_characters_total");
    EXPECT_EQ(std::strlen(rec.reason), 39u); // truncated to 39 chars
}

TEST_F(AuditTrailTest, RecordSize64Bytes) {
    EXPECT_EQ(sizeof(ControlTransitionRecord), 64u);
}

TEST_F(AuditTrailTest, FSMTransitionsRecordedInTrail) {
    RiskControlFSM risk;
    ExecControlFSM exec;

    risk.apply(RiskEvent::DrawdownWarning, 1, trail);
    exec.apply(ExecEvent::RiskEscalation, 1, trail);
    risk.apply(RiskEvent::PnlNormal, 2, trail);

    EXPECT_EQ(trail.count(), 3u);
    EXPECT_EQ(trail.at(0).domain, ControlDomain::Risk);
    EXPECT_EQ(trail.at(1).domain, ControlDomain::Execution);
    EXPECT_EQ(trail.at(2).domain, ControlDomain::Risk);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Overload Detector Tests
// ═══════════════════════════════════════════════════════════════════════════════

class OverloadDetectorTest : public ::testing::Test {
protected:
    OverloadConfig cfg;
    void SetUp() override {
        cfg.escalation_threshold = 3;
        cfg.consecutive_overload_limit = 5;
        cfg.recovery_ticks = 10;
    }
};

TEST_F(OverloadDetectorTest, NoOverloadNoAction) {
    OverloadDetector det(cfg);
    OverloadSignals sig;
    auto result = det.evaluate(sig);
    EXPECT_FALSE(result.should_escalate);
    EXPECT_FALSE(result.should_deescalate);
    EXPECT_EQ(result.severity, 0);
}

TEST_F(OverloadDetectorTest, LatencySpikeEscalates) {
    OverloadDetector det(cfg);
    OverloadSignals sig;
    sig.latency_spike = true; // severity 3 >= threshold 3
    auto result = det.evaluate(sig);
    EXPECT_TRUE(result.should_escalate);
    EXPECT_EQ(result.severity, 3);
}

TEST_F(OverloadDetectorTest, BudgetExceededAloneNoEscalate) {
    OverloadDetector det(cfg);
    OverloadSignals sig;
    sig.tick_budget_exceeded = true; // severity 2 < threshold 3
    auto result = det.evaluate(sig);
    EXPECT_FALSE(result.should_escalate);
    EXPECT_EQ(result.severity, 2);
}

TEST_F(OverloadDetectorTest, ConsecutiveOverloadEscalates) {
    OverloadDetector det(cfg);
    OverloadSignals sig;
    sig.tick_budget_exceeded = true; // severity 2, below threshold

    for (int i = 0; i < cfg.consecutive_overload_limit; ++i) {
        auto result = det.evaluate(sig);
        if (i < cfg.consecutive_overload_limit - 1) {
            EXPECT_FALSE(result.should_escalate);
        } else {
            EXPECT_TRUE(result.should_escalate);
        }
    }
}

TEST_F(OverloadDetectorTest, RecoveryAfterCleanTicks) {
    OverloadDetector det(cfg);

    // First trigger overload
    OverloadSignals overload_sig;
    overload_sig.latency_spike = true;
    det.evaluate(overload_sig);

    // Then clean ticks
    OverloadSignals clean_sig;
    for (int i = 0; i < cfg.recovery_ticks; ++i) {
        auto result = det.evaluate(clean_sig);
        if (i < cfg.recovery_ticks - 1) {
            EXPECT_FALSE(result.should_deescalate);
        } else {
            EXPECT_TRUE(result.should_deescalate);
        }
    }
}

TEST_F(OverloadDetectorTest, OverloadSeverityComputation) {
    OverloadSignals sig;
    EXPECT_EQ(sig.severity(), 0);

    sig.tick_budget_exceeded = true;
    EXPECT_EQ(sig.severity(), 2);

    sig.latency_spike = true;
    EXPECT_EQ(sig.severity(), 5);

    sig.order_rate_high = true;
    sig.queue_depth_high = true;
    sig.memory_pressure = true;
    EXPECT_EQ(sig.severity(), 9);
}

// ═══════════════════════════════════════════════════════════════════════════════
// System Mode Resolver Tests
// ═══════════════════════════════════════════════════════════════════════════════

class SystemModeTest : public ::testing::Test {
protected:
    ControlAuditTrail trail;
    RiskControlFSM risk;
    ExecControlFSM exec;
};

TEST_F(SystemModeTest, AllNominalFullTrading) {
    auto mode = SystemModeResolver::resolve(risk, exec, 0, 1.0);
    EXPECT_EQ(mode.mode, SystemMode::FullTrading);
    EXPECT_DOUBLE_EQ(mode.position_scale, 1.0);
    EXPECT_DOUBLE_EQ(mode.throttle_factor, 1.0);
    EXPECT_TRUE(mode.allows_new_orders);
    EXPECT_TRUE(mode.allows_increase);
}

TEST_F(SystemModeTest, RiskCautiousReducedActivity) {
    risk.apply(RiskEvent::DrawdownWarning, 1, trail);
    auto mode = SystemModeResolver::resolve(risk, exec, 0, 1.0);
    EXPECT_EQ(mode.mode, SystemMode::ReducedActivity);
    EXPECT_DOUBLE_EQ(mode.position_scale, 0.7);
}

TEST_F(SystemModeTest, ExecThrottledReducedActivity) {
    exec.apply(ExecEvent::OverloadDetected, 1, trail);
    auto mode = SystemModeResolver::resolve(risk, exec, 0, 1.0);
    EXPECT_EQ(mode.mode, SystemMode::ReducedActivity);
    EXPECT_DOUBLE_EQ(mode.throttle_factor, 0.5);
}

TEST_F(SystemModeTest, HealthWarningReducedActivity) {
    auto mode = SystemModeResolver::resolve(risk, exec, 2, 0.5); // Warning
    EXPECT_EQ(mode.mode, SystemMode::ReducedActivity);
    EXPECT_DOUBLE_EQ(mode.position_scale, 0.5); // 1.0 * 0.5
}

TEST_F(SystemModeTest, HealthCriticalCancelOnly) {
    auto mode = SystemModeResolver::resolve(risk, exec, 3, 0.2); // Critical
    EXPECT_EQ(mode.mode, SystemMode::CancelOnly);
}

TEST_F(SystemModeTest, HealthHaltedViewOnly) {
    auto mode = SystemModeResolver::resolve(risk, exec, 4, 0.0); // Halted
    EXPECT_EQ(mode.mode, SystemMode::ViewOnly);
}

TEST_F(SystemModeTest, MostRestrictiveWins) {
    // Risk Normal but Exec CancelOnly → CancelOnly
    exec.apply(ExecEvent::RiskEscalation, 1, trail);
    exec.apply(ExecEvent::RiskEscalation, 2, trail);
    EXPECT_EQ(exec.state(), ExecState::CancelOnly);

    auto mode = SystemModeResolver::resolve(risk, exec, 0, 1.0);
    EXPECT_EQ(mode.mode, SystemMode::CancelOnly);
}

TEST_F(SystemModeTest, EmergencyShutdownOverridesAll) {
    exec.apply(ExecEvent::EmergencyStop, 1, trail);
    auto mode = SystemModeResolver::resolve(risk, exec, 0, 1.0);
    EXPECT_EQ(mode.mode, SystemMode::EmergencyShutdown);
}

TEST_F(SystemModeTest, CombinedPositionScale) {
    risk.apply(RiskEvent::DrawdownWarning, 1, trail); // scale 0.7
    auto mode = SystemModeResolver::resolve(risk, exec, 2, 0.5); // health 0.5
    EXPECT_NEAR(mode.position_scale, 0.35, 1e-12); // 0.7 * 0.5
}

TEST_F(SystemModeTest, AllowsNewOrdersFalseWhenEitherBlocks) {
    risk.apply(RiskEvent::ConsecutiveLosses, 1, trail); // CB, blocks new orders
    auto mode = SystemModeResolver::resolve(risk, exec, 0, 1.0);
    EXPECT_FALSE(mode.allows_new_orders);
    EXPECT_FALSE(mode.allows_increase);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Control Plane Orchestrator Tests
// ═══════════════════════════════════════════════════════════════════════════════

class ControlPlaneTest : public ::testing::Test {
protected:
    ControlPlane cp;
};

TEST_F(ControlPlaneTest, InitialState) {
    EXPECT_EQ(cp.risk_fsm().state(), RiskState::Normal);
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::Active);
    EXPECT_EQ(cp.audit_trail().count(), 0u);
}

TEST_F(ControlPlaneTest, RiskEventPropagation) {
    // Risk escalation to Restricted should propagate to Exec
    cp.risk_event(RiskEvent::DrawdownBreached, 1, "dd_exceeded");
    EXPECT_EQ(cp.risk_fsm().state(), RiskState::Restricted);
    // Should have propagated to exec FSM
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::Throttled);
}

TEST_F(ControlPlaneTest, RiskCBPropagatesExecEscalation) {
    cp.risk_event(RiskEvent::ConsecutiveLosses, 1, "10_losses");
    EXPECT_EQ(cp.risk_fsm().state(), RiskState::CircuitBreaker);
    // Exec should be escalated too
    EXPECT_NE(cp.exec_fsm().state(), ExecState::Active);
}

TEST_F(ControlPlaneTest, RiskDeescalationPropagates) {
    cp.risk_event(RiskEvent::DrawdownBreached, 1);
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::Throttled);

    cp.risk_event(RiskEvent::PnlNormal, 2);
    EXPECT_EQ(cp.risk_fsm().state(), RiskState::Cautious);
    // Exec should deescalate since risk is now Cautious (<=)
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::Active);
}

TEST_F(ControlPlaneTest, EmergencyStopHaltsBoth) {
    cp.emergency_stop(1);
    EXPECT_EQ(cp.risk_fsm().state(), RiskState::Halted);
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::EmergencyFlat);
}

TEST_F(ControlPlaneTest, ManualResetResetsBoth) {
    cp.emergency_stop(1);
    cp.exec_event(ExecEvent::FlatComplete, 2); // complete flatten first

    cp.manual_reset(3);
    EXPECT_EQ(cp.risk_fsm().state(), RiskState::Normal);
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::Active);
}

TEST_F(ControlPlaneTest, OverloadEvaluationEscalates) {
    OverloadSignals sig;
    sig.latency_spike = true; // severity 3

    cp.evaluate_overload(sig, 1);
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::Throttled);
}

TEST_F(ControlPlaneTest, OverloadRecovery) {
    OverloadSignals overload;
    overload.latency_spike = true;
    cp.evaluate_overload(overload, 1);
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::Throttled);

    // Clean ticks to recover
    OverloadSignals clean;
    for (int i = 0; i < 20; ++i) {
        cp.evaluate_overload(clean, 2 + i);
    }
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::Active);
}

TEST_F(ControlPlaneTest, SnapshotCapture) {
    cp.risk_event(RiskEvent::DrawdownWarning, 1);
    cp.risk_event(RiskEvent::DrawdownBreached, 2);

    auto snap = cp.snapshot(0, 1.0);
    EXPECT_EQ(snap.risk.state, RiskState::Restricted);
    EXPECT_GT(snap.total_transitions, 0u);
    EXPECT_GT(snap.audit_trail_depth, 0u);
    EXPECT_EQ(snap.system.mode, SystemMode::CancelOnly);
}

TEST_F(ControlPlaneTest, ModeResolution) {
    auto mode = cp.resolve_mode(0, 1.0); // Excellent health
    EXPECT_EQ(mode.mode, SystemMode::FullTrading);

    cp.risk_event(RiskEvent::DrawdownWarning, 1);
    mode = cp.resolve_mode(2, 0.5); // Warning health
    EXPECT_EQ(mode.mode, SystemMode::ReducedActivity);
}

TEST_F(ControlPlaneTest, AuditTrailIntegrity) {
    cp.risk_event(RiskEvent::DrawdownWarning, 10, "test_dd");
    cp.risk_event(RiskEvent::DrawdownBreached, 20, "test_breach");
    cp.emergency_stop(30);

    auto& trail = cp.audit_trail();
    EXPECT_GE(trail.count(), 3u);

    // First risk transition
    auto& first = trail.at(0);
    EXPECT_EQ(first.domain, ControlDomain::Risk);
    EXPECT_EQ(first.tick_id, 10u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Transition Table Exhaustiveness Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TransitionTableTest, RiskTransitionTableComplete) {
    // Verify every [state][event] cell is defined (no uninitialized memory)
    for (size_t s = 0; s < static_cast<size_t>(RiskState::COUNT); ++s) {
        for (size_t e = 0; e < static_cast<size_t>(RiskEvent::COUNT); ++e) {
            auto result = RISK_TRANSITION[s][e];
            EXPECT_LT(static_cast<size_t>(result), static_cast<size_t>(RiskState::COUNT))
                << "Invalid state at [" << s << "][" << e << "]";
        }
    }
}

TEST(TransitionTableTest, ExecTransitionTableComplete) {
    for (size_t s = 0; s < static_cast<size_t>(ExecState::COUNT); ++s) {
        for (size_t e = 0; e < static_cast<size_t>(ExecEvent::COUNT); ++e) {
            auto result = EXEC_TRANSITION[s][e];
            EXPECT_LT(static_cast<size_t>(result), static_cast<size_t>(ExecState::COUNT))
                << "Invalid state at [" << s << "][" << e << "]";
        }
    }
}

TEST(TransitionTableTest, ManualResetAlwaysReachesNormal) {
    ControlAuditTrail trail;
    for (size_t s = 0; s < static_cast<size_t>(RiskState::COUNT); ++s) {
        RiskControlFSM fsm;
        // Force to state s via events
        auto state = static_cast<RiskState>(s);
        // ManualReset should always reach Normal (or be no-op if already Normal)
        auto result = RISK_TRANSITION[s][static_cast<size_t>(RiskEvent::ManualReset)];
        EXPECT_EQ(result, RiskState::Normal)
            << "ManualReset from " << risk_state_name(state) << " did not reach Normal";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Name Functions Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(NameFunctionsTest, RiskStateNames) {
    EXPECT_STREQ(risk_state_name(RiskState::Normal), "Normal");
    EXPECT_STREQ(risk_state_name(RiskState::Cautious), "Cautious");
    EXPECT_STREQ(risk_state_name(RiskState::Restricted), "Restricted");
    EXPECT_STREQ(risk_state_name(RiskState::CircuitBreaker), "CircuitBreaker");
    EXPECT_STREQ(risk_state_name(RiskState::Halted), "Halted");
}

TEST(NameFunctionsTest, ExecStateNames) {
    EXPECT_STREQ(exec_state_name(ExecState::Active), "Active");
    EXPECT_STREQ(exec_state_name(ExecState::Throttled), "Throttled");
    EXPECT_STREQ(exec_state_name(ExecState::CancelOnly), "CancelOnly");
    EXPECT_STREQ(exec_state_name(ExecState::Suspended), "Suspended");
    EXPECT_STREQ(exec_state_name(ExecState::EmergencyFlat), "EmergencyFlat");
}

TEST(NameFunctionsTest, SystemModeNames) {
    EXPECT_STREQ(system_mode_name(SystemMode::FullTrading), "FullTrading");
    EXPECT_STREQ(system_mode_name(SystemMode::ReducedActivity), "ReducedActivity");
    EXPECT_STREQ(system_mode_name(SystemMode::CancelOnly), "CancelOnly");
    EXPECT_STREQ(system_mode_name(SystemMode::ViewOnly), "ViewOnly");
    EXPECT_STREQ(system_mode_name(SystemMode::EmergencyShutdown), "EmergencyShutdown");
}

TEST(NameFunctionsTest, DomainNames) {
    EXPECT_STREQ(domain_name(ControlDomain::Risk), "Risk");
    EXPECT_STREQ(domain_name(ControlDomain::Execution), "Execution");
    EXPECT_STREQ(domain_name(ControlDomain::System), "System");
    EXPECT_STREQ(domain_name(ControlDomain::Overload), "Overload");
}

TEST(NameFunctionsTest, RiskEventNames) {
    EXPECT_STREQ(risk_event_name(RiskEvent::PnlNormal), "PnlNormal");
    EXPECT_STREQ(risk_event_name(RiskEvent::ManualHalt), "ManualHalt");
}

TEST(NameFunctionsTest, ExecEventNames) {
    EXPECT_STREQ(exec_event_name(ExecEvent::EmergencyStop), "EmergencyStop");
    EXPECT_STREQ(exec_event_name(ExecEvent::FlatComplete), "FlatComplete");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Layout & Trivially-Copyable Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(LayoutTest, ControlTransitionRecordIs64Bytes) {
    EXPECT_EQ(sizeof(ControlTransitionRecord), 64u);
}

TEST(LayoutTest, ControlTransitionRecordTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<ControlTransitionRecord>);
}

TEST(LayoutTest, OverloadSignalsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<OverloadSignals>);
}

TEST(LayoutTest, RiskControlSnapshotTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<RiskControlSnapshot>);
}

TEST(LayoutTest, ExecControlSnapshotTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<ExecControlSnapshot>);
}

TEST(LayoutTest, SystemModeSnapshotTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<SystemModeSnapshot>);
}

TEST(LayoutTest, ControlPlaneSnapshotTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<ControlPlaneSnapshot>);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Determinism / Replay Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(DeterminismTest, SameEventSequenceSameTrajectory) {
    // Two independent control planes, same event sequence → identical state
    ControlPlane cp1, cp2;

    cp1.risk_event(RiskEvent::DrawdownWarning, 1, "dd");
    cp2.risk_event(RiskEvent::DrawdownWarning, 1, "dd");

    cp1.risk_event(RiskEvent::DrawdownBreached, 2, "breach");
    cp2.risk_event(RiskEvent::DrawdownBreached, 2, "breach");

    OverloadSignals sig;
    sig.latency_spike = true;
    cp1.evaluate_overload(sig, 3);
    cp2.evaluate_overload(sig, 3);

    EXPECT_EQ(cp1.risk_fsm().state(), cp2.risk_fsm().state());
    EXPECT_EQ(cp1.exec_fsm().state(), cp2.exec_fsm().state());

    auto m1 = cp1.resolve_mode(0, 1.0);
    auto m2 = cp2.resolve_mode(0, 1.0);
    EXPECT_EQ(m1.mode, m2.mode);
    EXPECT_DOUBLE_EQ(m1.position_scale, m2.position_scale);
}

TEST(DeterminismTest, DifferentEventSequenceDifferentState) {
    ControlPlane cp1, cp2;

    cp1.risk_event(RiskEvent::DrawdownWarning, 1);
    cp2.risk_event(RiskEvent::ConsecutiveLosses, 1);

    EXPECT_NE(cp1.risk_fsm().state(), cp2.risk_fsm().state());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Full Scenario Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ScenarioTest, GradualDegradationAndRecovery) {
    ControlPlane cp;

    // Phase 1: All good
    auto mode = cp.resolve_mode(0, 1.0);
    EXPECT_EQ(mode.mode, SystemMode::FullTrading);

    // Phase 2: Health degrades
    cp.risk_event(RiskEvent::HealthDegraded, 100, "WARNING");
    mode = cp.resolve_mode(2, 0.5);
    EXPECT_EQ(mode.mode, SystemMode::ReducedActivity);
    EXPECT_NEAR(mode.position_scale, 0.35, 1e-12); // 0.7 * 0.5

    // Phase 3: Drawdown breaches
    cp.risk_event(RiskEvent::DrawdownBreached, 200, "dd_exceeded");
    mode = cp.resolve_mode(3, 0.2);
    EXPECT_EQ(mode.mode, SystemMode::CancelOnly);

    // Phase 4: Circuit breaker trips
    cp.risk_event(RiskEvent::ConsecutiveLosses, 300, "10_losses");
    mode = cp.resolve_mode(4, 0.0);
    EXPECT_EQ(mode.mode, SystemMode::ViewOnly);
    EXPECT_DOUBLE_EQ(mode.position_scale, 0.0);

    // Phase 5: Cooldown + health recovery
    cp.risk_event(RiskEvent::CooldownExpired, 400, "cooldown_done");
    cp.risk_event(RiskEvent::HealthRecovered, 500, "GOOD");
    mode = cp.resolve_mode(1, 0.9);
    // Should be ReducedActivity or better
    EXPECT_LE(static_cast<uint8_t>(mode.mode),
              static_cast<uint8_t>(SystemMode::ReducedActivity));

    // Phase 6: Full recovery
    cp.risk_event(RiskEvent::PnlNormal, 600, "all_clear");
    mode = cp.resolve_mode(0, 1.0);
    EXPECT_EQ(mode.mode, SystemMode::FullTrading);
    EXPECT_DOUBLE_EQ(mode.position_scale, 1.0);
}

TEST(ScenarioTest, OverloadFlashCrash) {
    ControlPlane cp;

    // Rapid overload signals
    OverloadSignals flash;
    flash.latency_spike = true;
    flash.tick_budget_exceeded = true;
    flash.queue_depth_high = true;

    for (int i = 0; i < 10; ++i) {
        cp.evaluate_overload(flash, i);
    }
    // Exec should be heavily throttled or cancel-only
    EXPECT_GE(static_cast<uint8_t>(cp.exec_fsm().state()),
              static_cast<uint8_t>(ExecState::Throttled));

    // Clear overload
    OverloadSignals clean;
    for (int i = 0; i < 25; ++i) {
        cp.evaluate_overload(clean, 100 + i);
    }
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::Active);
}

TEST(ScenarioTest, EmergencyStopAndRecovery) {
    ControlPlane cp;

    // Normal operation
    EXPECT_EQ(cp.risk_fsm().state(), RiskState::Normal);
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::Active);

    // Emergency
    cp.emergency_stop(1);
    EXPECT_EQ(cp.risk_fsm().state(), RiskState::Halted);
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::EmergencyFlat);

    auto mode = cp.resolve_mode(0, 1.0);
    EXPECT_EQ(mode.mode, SystemMode::EmergencyShutdown);
    EXPECT_FALSE(mode.allows_new_orders);

    // Flatten complete
    cp.exec_event(ExecEvent::FlatComplete, 2);
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::Suspended);

    // Manual reset
    cp.manual_reset(3);
    EXPECT_EQ(cp.risk_fsm().state(), RiskState::Normal);
    EXPECT_EQ(cp.exec_fsm().state(), ExecState::Active);

    mode = cp.resolve_mode(0, 1.0);
    EXPECT_EQ(mode.mode, SystemMode::FullTrading);
}

TEST(ScenarioTest, AuditTrailReplayFidelity) {
    ControlPlane cp;

    // Generate a sequence of events
    cp.risk_event(RiskEvent::DrawdownWarning, 1, "dd_warn");
    cp.risk_event(RiskEvent::DrawdownBreached, 2, "dd_breach");
    cp.risk_event(RiskEvent::ConsecutiveLosses, 3, "consec_loss");
    cp.risk_event(RiskEvent::CooldownExpired, 4, "cooldown");
    cp.risk_event(RiskEvent::ManualReset, 5, "reset");

    auto& trail = cp.audit_trail();
    EXPECT_GE(trail.count(), 5u); // at least 5 risk transitions (plus propagated exec)

    // Verify each risk transition is recorded with correct tick_id
    bool found_tick_1 = false, found_tick_5 = false;
    for (size_t i = 0; i < trail.count(); ++i) {
        auto& rec = trail.at(i);
        if (rec.tick_id == 1 && rec.domain == ControlDomain::Risk) found_tick_1 = true;
        if (rec.tick_id == 5 && rec.domain == ControlDomain::Risk) found_tick_5 = true;
    }
    EXPECT_TRUE(found_tick_1);
    EXPECT_TRUE(found_tick_5);
}
