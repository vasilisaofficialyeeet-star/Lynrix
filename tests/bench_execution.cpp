#include "execution_engine/order_state_machine.h"
#include "risk_engine/var_engine.h"
#include "config/types.h"
#include "utils/tsc_clock.h"

#include <fmt/format.h>

#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>
#include <cmath>

using namespace bybit;

template <typename Func>
static void benchmark(const std::string& name, int iterations, Func&& fn) {
    std::vector<uint64_t> latencies;
    latencies.reserve(iterations);

    // Warmup
    for (int i = 0; i < 1000; ++i) fn();

    for (int i = 0; i < iterations; ++i) {
        uint64_t t0 = TscClock::now();
        fn();
        uint64_t elapsed = TscClock::elapsed_ns(t0);
        latencies.push_back(elapsed);
    }

    std::sort(latencies.begin(), latencies.end());
    double mean = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    uint64_t p50 = latencies[latencies.size() / 2];
    uint64_t p99 = latencies[static_cast<size_t>(latencies.size() * 0.99)];
    uint64_t p999 = latencies[static_cast<size_t>(latencies.size() * 0.999)];
    uint64_t mn = latencies.front();
    uint64_t mx = latencies.back();

    fmt::print("  {:40s}  mean={:8.1f}ns  min={:5d}ns  p50={:5d}ns  p99={:5d}ns  p99.9={:6d}ns  max={:6d}ns\n",
               name, mean, mn, p50, p99, p999, mx);
}

int main() {
    fmt::print("═══════════════════════════════════════════════════════════════════════════\n");
    fmt::print("  Execution Engine & Risk Benchmark (Stage 2)\n");
    fmt::print("═══════════════════════════════════════════════════════════════════════════\n\n");

    constexpr int ITERATIONS = 500000;

    // ═══════════════════════════════════════════════════════════════════════
    // Order State Machine
    // ═══════════════════════════════════════════════════════════════════════
    fmt::print("  Order State Machine:\n");

    benchmark("FSM transition (Submit)", ITERATIONS, []() {
        ManagedOrder o;
        auto r = o.apply_event(OrdEvent::Submit);
        (void)r;
    });

    benchmark("FSM full lifecycle (Submit→Ack→Fill)", ITERATIONS, []() {
        ManagedOrder o;
        o.apply_event(OrdEvent::Submit);
        o.apply_event(OrdEvent::Ack);
        auto r = o.apply_event(OrdEvent::Fill);
        (void)r;
    });

    benchmark("FSM with amend (Submit→Ack→Amend→Ack→Fill)", ITERATIONS, []() {
        ManagedOrder o;
        o.apply_event(OrdEvent::Submit);
        o.apply_event(OrdEvent::Ack);
        o.apply_event(OrdEvent::AmendReq);
        o.apply_event(OrdEvent::AmendAck);
        auto r = o.apply_event(OrdEvent::Fill);
        (void)r;
    });

    benchmark("apply_transition (pure LUT)", ITERATIONS, []() {
        volatile auto r = apply_transition(OrdState::Live, OrdEvent::Fill);
        (void)r;
    });

    // ═══════════════════════════════════════════════════════════════════════
    // Fill Probability Tracker
    // ═══════════════════════════════════════════════════════════════════════
    fmt::print("\n  Fill Probability Tracker:\n");

    FillProbTracker tracker;
    int fill_idx = 0;

    benchmark("record_fill (EMA update)", ITERATIONS, [&]() {
        tracker.record_fill(100.0 + (fill_idx % 10) * 0.1, 100.0, 0.1);
        ++fill_idx;
    });

    benchmark("record_miss (EMA update)", ITERATIONS, [&]() {
        tracker.record_miss(100.0 + (fill_idx % 10) * 0.1, 100.0, 0.1);
        ++fill_idx;
    });

    benchmark("fill_probability (query)", ITERATIONS, [&]() {
        volatile double p = tracker.fill_probability(100.0, 100.0, 0.1);
        (void)p;
    });

    // ═══════════════════════════════════════════════════════════════════════
    // Market Impact Model
    // ═══════════════════════════════════════════════════════════════════════
    fmt::print("\n  Market Impact Model:\n");

    benchmark("temporary_impact", ITERATIONS, []() {
        volatile double v = MarketImpactModel::temporary_impact(0.01, 1000.0, 0.02, 0.5);
        (void)v;
    });

    benchmark("permanent_impact", ITERATIONS, []() {
        volatile double v = MarketImpactModel::permanent_impact(0.15, 0.5);
        (void)v;
    });

    benchmark("expected_slippage_bps", ITERATIONS, []() {
        volatile double v = MarketImpactModel::expected_slippage_bps(
            Side::Buy, 0.01, 1000.0, 0.02, 0.5, 0.1);
        (void)v;
    });

    benchmark("optimal_slices", ITERATIONS, []() {
        volatile uint32_t v = MarketImpactModel::optimal_slices(0.1, 1000.0, 1.0);
        (void)v;
    });

    // ═══════════════════════════════════════════════════════════════════════
    // Adaptive Cancel/Replace
    // ═══════════════════════════════════════════════════════════════════════
    fmt::print("\n  Adaptive Cancel/Replace:\n");

    AdaptiveCancelState cancel_state;
    AdaptiveCancelConfig cancel_cfg;
    int cancel_idx = 0;

    benchmark("decide (keep)", ITERATIONS, [&]() {
        volatile int d = cancel_state.decide(100.0, 100.1, 0.5, 0.3,
            static_cast<uint64_t>(cancel_idx) * 1'000'000'000ULL, cancel_cfg);
        (void)d;
        ++cancel_idx;
    });

    benchmark("decide (amend)", ITERATIONS, [&]() {
        AdaptiveCancelState s;
        volatile int d = s.decide(100.0, 101.0, 0.5, 0.3,
            1'000'000'000ULL, cancel_cfg);
        (void)d;
    });

    // ═══════════════════════════════════════════════════════════════════════
    // Order Manager
    // ═══════════════════════════════════════════════════════════════════════
    fmt::print("\n  Order Manager:\n");

    benchmark("alloc + reset", ITERATIONS, []() {
        OrderManager mgr;
        auto* o = mgr.alloc();
        (void)o;
    });

    {
        OrderManager mgr;
        for (int i = 0; i < 32; ++i) {
            auto* o = mgr.alloc();
            char id[16];
            snprintf(id, sizeof(id), "ord_%d", i);
            o->order_id.set(id);
        }

        benchmark("find by ID (32 orders)", ITERATIONS, [&]() {
            volatile size_t idx = mgr.find("ord_15");
            (void)idx;
        });
    }

    // ═══════════════════════════════════════════════════════════════════════
    // VaR Engine
    // ═══════════════════════════════════════════════════════════════════════
    fmt::print("\n  VaR Engine:\n");

    VaREngine var_engine;
    std::mt19937 rng(42);
    std::normal_distribution<double> price_noise(0.0, 50.0);

    // Feed price data
    double price = 50000.0;
    for (int i = 0; i < 2000; ++i) {
        price += price_noise(rng) * 0.01;
        var_engine.on_price(price);
    }

    benchmark("on_price (feed tick)", ITERATIONS, [&]() {
        price += 0.001;
        var_engine.on_price(price);
    });

    // VaR compute is heavier — fewer iterations
    benchmark("compute (10k MC scenarios)", 1000, [&]() {
        volatile auto r = var_engine.compute(0.01, 500.0, 10.0);
        (void)r;
    });

    // Also test with 1k scenarios for comparison
    {
        VaRConfig fast_cfg;
        fast_cfg.num_scenarios = 1000;
        VaREngine fast_var(fast_cfg);
        for (int i = 0; i < 500; ++i) fast_var.on_price(50000.0 + i * 0.1);

        benchmark("compute (1k MC scenarios)", 10000, [&]() {
            volatile auto r = fast_var.compute(0.01, 500.0, 10.0);
            (void)r;
        });
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Iceberg + TWAP
    // ═══════════════════════════════════════════════════════════════════════
    fmt::print("\n  Iceberg / TWAP:\n");

    benchmark("IcebergConfig::init + 10 slices", ITERATIONS, []() {
        IcebergConfig ice;
        ice.init(1.0, 0.1);
        for (int i = 0; i < 10; ++i) {
            double q = ice.next_slice_qty();
            ice.on_slice_fill(q);
        }
    });

    benchmark("SliceSchedule::init_twap", ITERATIONS, []() {
        SliceSchedule s;
        s.init_twap(Side::Buy, 1.0, 10, 1'000'000'000ULL);
    });

    benchmark("SliceSchedule::should_send_slice", ITERATIONS, []() {
        SliceSchedule s;
        s.init_twap(Side::Buy, 1.0, 10, 1'000'000'000ULL);
        volatile bool b = s.should_send_slice();
        (void)b;
    });

    // ═══════════════════════════════════════════════════════════════════════
    // Summary
    // ═══════════════════════════════════════════════════════════════════════
    fmt::print("\n═══════════════════════════════════════════════════════════════════════════\n");

    auto var_result = var_engine.result();
    fmt::print("\n  VaR Results (last compute):\n");
    fmt::print("    Parametric VaR 95: ${:.2f}\n", var_result.parametric_var_95);
    fmt::print("    Parametric VaR 99: ${:.2f}\n", var_result.parametric_var_99);
    fmt::print("    MC VaR 95:         ${:.2f}\n", var_result.mc_var_95);
    fmt::print("    MC VaR 99:         ${:.2f}\n", var_result.mc_var_99);
    fmt::print("    CVaR 99:           ${:.2f}\n", var_result.cvar_99);
    fmt::print("    Worst stress:      ${:.2f}\n", var_result.worst_stress_loss);
    fmt::print("    Compute latency:   {} ns\n", var_result.compute_latency_ns);
    fmt::print("    Scenarios used:    {}\n", var_result.scenarios_used);
    fmt::print("    Fill tracker: {} submissions, {} fills, rate={:.1f}%\n",
               tracker.total_submissions(), tracker.total_fills(),
               tracker.aggregate_fill_rate() * 100.0);

    return 0;
}
