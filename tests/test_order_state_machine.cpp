#include <gtest/gtest.h>
#include "execution_engine/order_state_machine.h"
#include "risk_engine/var_engine.h"
#include "config/types.h"

#include <cmath>
#include <chrono>

using namespace bybit;

// ═══════════════════════════════════════════════════════════════════════════
// Order State Machine Tests
// ═══════════════════════════════════════════════════════════════════════════

class OrderFSMTest : public ::testing::Test {
protected:
    ManagedOrder ord;

    void SetUp() override {
        ord.reset();
    }
};

// ─── State Transition Tests ────────────────────────────────────────────────

TEST_F(OrderFSMTest, InitialStateIsIdle) {
    EXPECT_EQ(ord.state, OrdState::Idle);
    EXPECT_FALSE(ord.is_active());
    EXPECT_FALSE(ord.is_terminal());
}

TEST_F(OrderFSMTest, SubmitTransition) {
    auto r = ord.apply_event(OrdEvent::Submit);
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(r.new_state, OrdState::PendingNew);
    EXPECT_EQ(ord.state, OrdState::PendingNew);
    EXPECT_TRUE(ord.is_active());
}

TEST_F(OrderFSMTest, SubmitThenAck) {
    ord.apply_event(OrdEvent::Submit);
    auto r = ord.apply_event(OrdEvent::Ack);
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(r.new_state, OrdState::Live);
}

TEST_F(OrderFSMTest, LiveThenFill) {
    ord.apply_event(OrdEvent::Submit);
    ord.apply_event(OrdEvent::Ack);
    auto r = ord.apply_event(OrdEvent::Fill);
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(r.new_state, OrdState::Filled);
    EXPECT_TRUE(ord.is_terminal());
}

TEST_F(OrderFSMTest, LiveThenPartialFill) {
    ord.apply_event(OrdEvent::Submit);
    ord.apply_event(OrdEvent::Ack);
    auto r = ord.apply_event(OrdEvent::PartialFill);
    EXPECT_EQ(r.new_state, OrdState::PartialFill);
    EXPECT_TRUE(ord.is_active());
}

TEST_F(OrderFSMTest, PartialFillThenFill) {
    ord.apply_event(OrdEvent::Submit);
    ord.apply_event(OrdEvent::Ack);
    ord.apply_event(OrdEvent::PartialFill);
    auto r = ord.apply_event(OrdEvent::Fill);
    EXPECT_EQ(r.new_state, OrdState::Filled);
    EXPECT_TRUE(ord.is_terminal());
}

TEST_F(OrderFSMTest, LiveThenCancelReqThenCancelAck) {
    ord.apply_event(OrdEvent::Submit);
    ord.apply_event(OrdEvent::Ack);
    auto r1 = ord.apply_event(OrdEvent::CancelReq);
    EXPECT_EQ(r1.new_state, OrdState::PendingCancel);
    auto r2 = ord.apply_event(OrdEvent::CancelAck);
    EXPECT_EQ(r2.new_state, OrdState::Cancelled);
    EXPECT_TRUE(ord.is_terminal());
}

TEST_F(OrderFSMTest, LiveThenAmendReqThenAmendAck) {
    ord.apply_event(OrdEvent::Submit);
    ord.apply_event(OrdEvent::Ack);
    auto r1 = ord.apply_event(OrdEvent::AmendReq);
    EXPECT_EQ(r1.new_state, OrdState::PendingAmend);
    auto r2 = ord.apply_event(OrdEvent::AmendAck);
    EXPECT_EQ(r2.new_state, OrdState::Live);
}

TEST_F(OrderFSMTest, PendingAmendReject) {
    ord.apply_event(OrdEvent::Submit);
    ord.apply_event(OrdEvent::Ack);
    ord.apply_event(OrdEvent::AmendReq);
    auto r = ord.apply_event(OrdEvent::Reject);
    EXPECT_EQ(r.new_state, OrdState::Live); // amend rejected → back to live
}

TEST_F(OrderFSMTest, PendingNewReject) {
    ord.apply_event(OrdEvent::Submit);
    auto r = ord.apply_event(OrdEvent::Reject);
    EXPECT_EQ(r.new_state, OrdState::Cancelled);
    EXPECT_TRUE(ord.is_terminal());
}

TEST_F(OrderFSMTest, InstantFillFromPendingNew) {
    ord.apply_event(OrdEvent::Submit);
    auto r = ord.apply_event(OrdEvent::Fill);
    EXPECT_EQ(r.new_state, OrdState::Filled);
    EXPECT_TRUE(ord.is_terminal());
}

TEST_F(OrderFSMTest, TimeoutFromLive) {
    ord.apply_event(OrdEvent::Submit);
    ord.apply_event(OrdEvent::Ack);
    auto r = ord.apply_event(OrdEvent::Timeout);
    EXPECT_EQ(r.new_state, OrdState::PendingCancel);
}

TEST_F(OrderFSMTest, TerminalStatesAreNoOps) {
    ord.apply_event(OrdEvent::Submit);
    ord.apply_event(OrdEvent::Ack);
    ord.apply_event(OrdEvent::Fill);

    // All events on terminal state should be no-ops
    auto r1 = ord.apply_event(OrdEvent::Submit);
    EXPECT_FALSE(r1.changed);
    EXPECT_EQ(r1.new_state, OrdState::Filled);

    auto r2 = ord.apply_event(OrdEvent::CancelReq);
    EXPECT_FALSE(r2.changed);
}

TEST_F(OrderFSMTest, DuplicateAckIsNoOp) {
    ord.apply_event(OrdEvent::Submit);
    ord.apply_event(OrdEvent::Ack);
    auto r = ord.apply_event(OrdEvent::Ack);
    EXPECT_FALSE(r.changed);
    EXPECT_EQ(r.new_state, OrdState::Live);
}

TEST_F(OrderFSMTest, FillDuringCancel) {
    ord.apply_event(OrdEvent::Submit);
    ord.apply_event(OrdEvent::Ack);
    ord.apply_event(OrdEvent::CancelReq);
    auto r = ord.apply_event(OrdEvent::Fill);
    EXPECT_EQ(r.new_state, OrdState::Filled);
}

// ─── FSM Latency Test ──────────────────────────────────────────────────────

TEST_F(OrderFSMTest, TransitionLatency) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; ++i) {
        ManagedOrder o;
        o.apply_event(OrdEvent::Submit);
        o.apply_event(OrdEvent::Ack);
        o.apply_event(OrdEvent::Fill);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double avg = static_cast<double>(ns) / 1000000.0 / 3.0;
    std::cout << "Average FSM transition latency: " << avg << " ns" << std::endl;
    EXPECT_LT(avg, 100.0); // <100 ns per transition
}

// ─── State/Event Name Lookup ───────────────────────────────────────────────

TEST_F(OrderFSMTest, StateNames) {
    EXPECT_STREQ(ord_state_name(OrdState::Idle), "Idle");
    EXPECT_STREQ(ord_state_name(OrdState::Live), "Live");
    EXPECT_STREQ(ord_state_name(OrdState::Filled), "Filled");
    EXPECT_STREQ(ord_state_name(OrdState::Cancelled), "Cancelled");
}

TEST_F(OrderFSMTest, EventNames) {
    EXPECT_STREQ(ord_event_name(OrdEvent::Submit), "Submit");
    EXPECT_STREQ(ord_event_name(OrdEvent::Fill), "Fill");
    EXPECT_STREQ(ord_event_name(OrdEvent::CancelAck), "CancelAck");
}

// ═══════════════════════════════════════════════════════════════════════════
// Fill Probability Tracker Tests
// ═══════════════════════════════════════════════════════════════════════════

class FillProbTrackerTest : public ::testing::Test {
protected:
    FillProbTracker tracker;
    void SetUp() override { tracker.reset(); }
};

TEST_F(FillProbTrackerTest, InitialPrior) {
    double p = tracker.fill_probability(100.0, 100.0, 0.1);
    EXPECT_NEAR(p, 0.5, 0.01); // 50% prior
}

TEST_F(FillProbTrackerTest, FillsIncreaseProb) {
    double ref = 100.0, tick = 0.1;
    for (int i = 0; i < 100; ++i) {
        tracker.record_submission(100.0, ref, tick);
        tracker.record_fill(100.0, ref, tick);
    }
    double p = tracker.fill_probability(100.0, ref, tick);
    EXPECT_GT(p, 0.9);
}

TEST_F(FillProbTrackerTest, MissesDecreaseProb) {
    double ref = 100.0, tick = 0.1;
    for (int i = 0; i < 100; ++i) {
        tracker.record_submission(100.0, ref, tick);
        tracker.record_miss(100.0, ref, tick);
    }
    double p = tracker.fill_probability(100.0, ref, tick);
    EXPECT_LT(p, 0.1);
}

TEST_F(FillProbTrackerTest, DifferentBandsIndependent) {
    double ref = 100.0, tick = 0.1;
    // Fill at 100.0
    for (int i = 0; i < 50; ++i) {
        tracker.record_submission(100.0, ref, tick);
        tracker.record_fill(100.0, ref, tick);
    }
    // Miss at 100.5
    for (int i = 0; i < 50; ++i) {
        tracker.record_submission(100.5, ref, tick);
        tracker.record_miss(100.5, ref, tick);
    }

    double p_near = tracker.fill_probability(100.0, ref, tick);
    double p_far  = tracker.fill_probability(100.5, ref, tick);
    EXPECT_GT(p_near, p_far);
}

TEST_F(FillProbTrackerTest, AggregateFillRate) {
    double ref = 100.0, tick = 0.1;
    for (int i = 0; i < 100; ++i) {
        tracker.record_submission(100.0, ref, tick);
    }
    for (int i = 0; i < 40; ++i) {
        tracker.record_fill(100.0, ref, tick);
    }
    EXPECT_NEAR(tracker.aggregate_fill_rate(), 0.4, 0.01);
}

TEST_F(FillProbTrackerTest, EMAUpdateLatency) {
    double ref = 100.0, tick = 0.1;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; ++i) {
        tracker.record_fill(100.0 + (i % 10) * tick, ref, tick);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double avg = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1000000.0;
    std::cout << "Average EMA fill prob update: " << avg << " ns" << std::endl;
    EXPECT_LT(avg, 100.0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Iceberg Order Tests
// ═══════════════════════════════════════════════════════════════════════════

class IcebergTest : public ::testing::Test {
protected:
    IcebergConfig ice;
};

TEST_F(IcebergTest, Init) {
    ice.init(1.0, 0.1);
    EXPECT_TRUE(ice.active);
    EXPECT_EQ(ice.max_slices, 10u);
    EXPECT_DOUBLE_EQ(ice.remaining_qty, 1.0);
}

TEST_F(IcebergTest, SliceProgression) {
    ice.init(1.0, 0.1);
    EXPECT_NEAR(ice.next_slice_qty(), 0.1, 1e-12);

    for (int i = 0; i < 10; ++i) {
        double sq = ice.next_slice_qty();
        EXPECT_GT(sq, 0.0);
        ice.on_slice_fill(sq);
    }
    EXPECT_TRUE(ice.is_complete());
    EXPECT_NEAR(ice.completion_pct(), 1.0, 1e-10);
}

TEST_F(IcebergTest, PartialLastSlice) {
    ice.init(0.95, 0.1); // 9 full + 1 partial
    for (int i = 0; i < 9; ++i) {
        ice.on_slice_fill(0.1);
    }
    EXPECT_NEAR(ice.next_slice_qty(), 0.05, 1e-10);
    ice.on_slice_fill(0.05);
    EXPECT_TRUE(ice.is_complete());
}

// ═══════════════════════════════════════════════════════════════════════════
// TWAP/VWAP Slice Schedule Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(SliceScheduleTest, TWAPInit) {
    SliceSchedule s;
    s.init_twap(Side::Buy, 1.0, 10, 1'000'000'000ULL); // 1s duration
    EXPECT_TRUE(s.active);
    EXPECT_EQ(s.num_slices, 10u);
    EXPECT_EQ(s.interval_ns, 100'000'000ULL); // 100ms per slice
}

TEST(SliceScheduleTest, TWAPSliceQty) {
    SliceSchedule s;
    s.init_twap(Side::Buy, 1.0, 5, 5'000'000'000ULL);
    // Equal slices: 0.2 each
    EXPECT_NEAR(s.next_slice_qty(), 0.2, 1e-10);
}

TEST(SliceScheduleTest, VWAPInit) {
    SliceSchedule s;
    s.init_vwap(Side::Sell, 2.0, 20, 10'000'000'000ULL);
    EXPECT_EQ(s.algo, SliceAlgo::VWAP);
    EXPECT_TRUE(s.active);
}

TEST(SliceScheduleTest, SliceCompletion) {
    SliceSchedule s;
    s.init_twap(Side::Buy, 1.0, 4, 4'000'000'000ULL);
    for (uint32_t i = 0; i < 4; ++i) {
        double q = s.next_slice_qty();
        s.on_slice_sent();
        s.on_slice_fill(q);
    }
    EXPECT_TRUE(s.is_complete());
    EXPECT_NEAR(s.completion_pct(), 1.0, 1e-10);
}

// ═══════════════════════════════════════════════════════════════════════════
// Market Impact Model Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MarketImpactTest, TemporaryImpact) {
    double impact = MarketImpactModel::temporary_impact(
        0.01, 1000.0, 0.02, 0.5);
    EXPECT_GT(impact, 0.0);
    EXPECT_LT(impact, 10000.0); // reasonable bps range
}

TEST(MarketImpactTest, LargerOrderMoreImpact) {
    double small = MarketImpactModel::temporary_impact(0.001, 1000.0, 0.02, 0.5);
    double large = MarketImpactModel::temporary_impact(0.1,   1000.0, 0.02, 0.5);
    EXPECT_GT(large, small);
}

TEST(MarketImpactTest, SlippageBps) {
    double slip = MarketImpactModel::expected_slippage_bps(
        Side::Buy, 0.01, 1000.0, 0.02, 0.5, 0.1);
    EXPECT_GT(slip, 0.0);
}

TEST(MarketImpactTest, FavorableImbalanceReducesSlippage) {
    // Buy with ask-heavy book (negative imbalance) should have less slippage
    double slip_favorable = MarketImpactModel::expected_slippage_bps(
        Side::Buy, 0.01, 1000.0, 0.02, 0.5, -0.5); // ask heavy, good for buyer
    double slip_adverse = MarketImpactModel::expected_slippage_bps(
        Side::Buy, 0.01, 1000.0, 0.02, 0.5, +0.5); // bid heavy, bad for buyer
    EXPECT_LT(slip_favorable, slip_adverse);
}

TEST(MarketImpactTest, OptimalSlices) {
    uint32_t n = MarketImpactModel::optimal_slices(0.1, 1000.0, 1.0);
    EXPECT_GE(n, 1u);
    EXPECT_LE(n, 100u);
}

TEST(MarketImpactTest, ImpactLatency) {
    auto start = std::chrono::high_resolution_clock::now();
    volatile double v = 0.0;
    for (int i = 0; i < 1000000; ++i) {
        v = MarketImpactModel::expected_slippage_bps(
            Side::Buy, 0.01, 1000.0, 0.02, 0.5, 0.1);
    }
    (void)v;
    auto end = std::chrono::high_resolution_clock::now();
    double avg = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1000000.0;
    std::cout << "Average market impact computation: " << avg << " ns" << std::endl;
    EXPECT_LT(avg, 200.0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Adaptive Cancel/Replace Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(AdaptiveCancelTest, KeepWhenClose) {
    AdaptiveCancelState state;
    AdaptiveCancelConfig cfg;
    int decision = state.decide(100.0, 100.1, 0.5, 0.3, 1'000'000'000ULL, cfg);
    EXPECT_EQ(decision, 0); // keep
}

TEST(AdaptiveCancelTest, AmendWhenDrift) {
    AdaptiveCancelState state;
    AdaptiveCancelConfig cfg;
    // Price drifted 1.0 from mid, spread=0.5, drift > 1.5*spread
    int decision = state.decide(100.0, 101.0, 0.5, 0.3, 1'000'000'000ULL, cfg);
    EXPECT_EQ(decision, 1); // amend
}

TEST(AdaptiveCancelTest, CancelWhenLargeDrift) {
    AdaptiveCancelState state;
    AdaptiveCancelConfig cfg;
    // Price drifted 2.0 from mid, spread=0.5, drift > 3*spread
    int decision = state.decide(100.0, 102.0, 0.5, 0.3, 1'000'000'000ULL, cfg);
    EXPECT_EQ(decision, 2); // cancel
}

TEST(AdaptiveCancelTest, CancelWhenLowFillProb) {
    AdaptiveCancelState state;
    AdaptiveCancelConfig cfg;
    // Close to mid but fill prob < 5%
    int decision = state.decide(100.0, 100.1, 0.5, 0.01, 1'000'000'000ULL, cfg);
    EXPECT_EQ(decision, 2); // cancel
}

TEST(AdaptiveCancelTest, CancelAfterMaxAmends) {
    AdaptiveCancelState state;
    AdaptiveCancelConfig cfg;
    state.amend_count = 5; // max
    int decision = state.decide(100.0, 100.5, 0.5, 0.3, 1'000'000'000ULL, cfg);
    EXPECT_EQ(decision, 2); // cancel due to max amends
}

TEST(AdaptiveCancelTest, AmendCooldown) {
    AdaptiveCancelState state;
    AdaptiveCancelConfig cfg;
    state.last_amend_ns = 999'000'000ULL; // 999ms ago (but we check with 1B ns)
    // Drift enough to amend, but cooldown not elapsed
    int decision = state.decide(100.0, 101.0, 0.5, 0.3, 1'000'000'000ULL, cfg);
    // 1B - 999M = 1M ns = 1ms, cooldown is 50ms, so not OK
    EXPECT_EQ(decision, 0); // keep (cooldown)
}

// ═══════════════════════════════════════════════════════════════════════════
// Order Manager Tests
// ═══════════════════════════════════════════════════════════════════════════

class OrderManagerTest : public ::testing::Test {
protected:
    OrderManager mgr;
    void SetUp() override { mgr.reset(); }
};

TEST_F(OrderManagerTest, AllocAndCount) {
    auto* o = mgr.alloc();
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(mgr.count(), 1u);
    o->state = OrdState::Live;
    EXPECT_TRUE(o->is_active());
}

TEST_F(OrderManagerTest, RemoveSwapsLast) {
    auto* o1 = mgr.alloc();
    o1->order_id.set("order1");
    auto* o2 = mgr.alloc();
    o2->order_id.set("order2");
    auto* o3 = mgr.alloc();
    o3->order_id.set("order3");

    EXPECT_EQ(mgr.count(), 3u);

    // Remove order1 (idx 0), order3 should be swapped in
    mgr.remove(0);
    EXPECT_EQ(mgr.count(), 2u);
    EXPECT_STREQ(mgr[0].order_id.c_str(), "order3");
}

TEST_F(OrderManagerTest, FindById) {
    auto* o = mgr.alloc();
    o->order_id.set("test_123");

    size_t idx = mgr.find("test_123");
    EXPECT_EQ(idx, 0u);

    size_t bad = mgr.find("nonexistent");
    EXPECT_EQ(bad, SIZE_MAX);
}

TEST_F(OrderManagerTest, FullPool) {
    for (size_t i = 0; i < MAX_OPEN_ORDERS; ++i) {
        EXPECT_NE(mgr.alloc(), nullptr);
    }
    EXPECT_TRUE(mgr.full());
    EXPECT_EQ(mgr.alloc(), nullptr);
}

TEST_F(OrderManagerTest, IterateActive) {
    for (int i = 0; i < 5; ++i) {
        auto* o = mgr.alloc();
        o->price = Price(100.0 + i);
    }

    int count = 0;
    for (auto& o : mgr) {
        EXPECT_GT(o.price.raw(), 0.0);
        ++count;
    }
    EXPECT_EQ(count, 5);
}

// ═══════════════════════════════════════════════════════════════════════════
// VaR Engine Tests
// ═══════════════════════════════════════════════════════════════════════════

class VaREngineTest : public ::testing::Test {
protected:
    VaREngine var;
    void SetUp() override { var.reset(); }
};

TEST_F(VaREngineTest, EmptyReturnsNoCompute) {
    auto r = var.compute(0.01, 500.0, 0.0);
    EXPECT_DOUBLE_EQ(r.mc_var_95, 0.0);
}

TEST_F(VaREngineTest, FeedPricesAndCompute) {
    // Feed 200 price ticks with some volatility
    for (int i = 0; i < 200; ++i) {
        double price = 50000.0 + std::sin(i * 0.1) * 50.0;
        var.on_price(price);
    }

    auto r = var.compute(0.01, 500.0, 10.0);
    EXPECT_GT(r.parametric_var_95, 0.0);
    EXPECT_GT(r.parametric_var_99, 0.0);
    EXPECT_GT(r.mc_var_95, 0.0);
    EXPECT_GT(r.mc_var_99, 0.0);
    EXPECT_GE(r.mc_var_99, r.mc_var_95);
    EXPECT_GT(r.cvar_99, 0.0);
    EXPECT_GT(r.compute_latency_ns, 0u);
    EXPECT_GT(r.scenarios_used, 0u);
}

TEST_F(VaREngineTest, HigherVolHigherVaR) {
    VaREngine var_calm, var_wild;

    for (int i = 0; i < 200; ++i) {
        var_calm.on_price(50000.0 + i * 0.01);  // very calm
        var_wild.on_price(50000.0 + std::sin(i * 0.5) * 500.0); // wild
    }

    auto r_calm = var_calm.compute(0.01, 500.0, 0.0);
    auto r_wild = var_wild.compute(0.01, 500.0, 0.0);

    EXPECT_GT(r_wild.parametric_var_95, r_calm.parametric_var_95);
}

TEST_F(VaREngineTest, StressTests) {
    for (int i = 0; i < 100; ++i) var.on_price(50000.0 + i * 0.1);
    auto r = var.compute(0.01, 500.0, 0.0);

    EXPECT_GT(r.worst_stress_loss, 0.0);
    EXPECT_GT(r.avg_stress_loss, 0.0);
    EXPECT_GE(r.worst_stress_loss, r.avg_stress_loss);
}

TEST_F(VaREngineTest, PositionSizeLimit) {
    for (int i = 0; i < 100; ++i) var.on_price(50000.0 + i * 0.1);
    var.compute(0.01, 500.0, 0.0);

    double max_pos = var.max_position_for_var(100.0, 50000.0, var.current_volatility());
    EXPECT_GT(max_pos, 0.0);
    EXPECT_LT(max_pos, 1e10);
}

TEST_F(VaREngineTest, ComputeLatency) {
    for (int i = 0; i < 1000; ++i) {
        var.on_price(50000.0 + std::sin(i * 0.01) * 100.0);
    }

    // Warmup
    var.compute(0.01, 500.0, 0.0);

    // Measure
    uint64_t total_ns = 0;
    constexpr int RUNS = 100;
    for (int i = 0; i < RUNS; ++i) {
        uint64_t t0 = TscClock::now();
        var.compute(0.01, 500.0, 0.0);
        total_ns += TscClock::elapsed_ns(t0);
    }

    double avg_us = static_cast<double>(total_ns) / RUNS / 1000.0;
    std::cout << "Average VaR compute (10k MC): " << avg_us << " µs" << std::endl;
    EXPECT_LT(avg_us, 2000.0); // <2 ms (conservative; sorting 10k doubles ~1ms)
}

TEST_F(VaREngineTest, WithinVarLimit) {
    for (int i = 0; i < 100; ++i) var.on_price(50000.0 + i * 0.01);
    var.compute(0.01, 500.0, 0.0);

    EXPECT_TRUE(var.within_var_limit(10000.0));  // generous limit
}
