// Stage 6: Control Plane FSM Benchmark
// Measures FSM transition overhead, overload evaluation, and mode resolution.

#include "../src/core/system_control.h"
#include "../src/utils/tsc_clock.h"

#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <array>
#include <numeric>

using namespace bybit;

static constexpr int WARMUP = 1000;
static constexpr int ITERATIONS = 100'000;

struct BenchResult {
    const char* name;
    double mean_ns;
    double p50_ns;
    double p99_ns;
    double min_ns;
    double max_ns;
};

template<typename Fn>
BenchResult bench(const char* name, Fn&& fn) {
    // Warmup
    for (int i = 0; i < WARMUP; ++i) fn();

    std::array<double, ITERATIONS> samples;
    for (int i = 0; i < ITERATIONS; ++i) {
        uint64_t start = TscClock::now();
        fn();
        uint64_t end = TscClock::now();
        samples[i] = TscClock::to_ns(end - start);
    }

    std::sort(samples.begin(), samples.end());
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);

    return {
        name,
        sum / ITERATIONS,
        samples[ITERATIONS / 2],
        samples[static_cast<size_t>(ITERATIONS * 0.99)],
        samples[0],
        samples[ITERATIONS - 1]
    };
}

void print_result(const BenchResult& r) {
    std::printf("  %-40s  mean=%6.1f ns  p50=%6.1f ns  p99=%6.1f ns  min=%5.1f  max=%7.1f\n",
                r.name, r.mean_ns, r.p50_ns, r.p99_ns, r.min_ns, r.max_ns);
}

int main() {
    std::printf("═══════════════════════════════════════════════════════════════════════════\n");
    std::printf("  Stage 6: Control Plane FSM Benchmark (%d iterations)\n", ITERATIONS);
    std::printf("═══════════════════════════════════════════════════════════════════════════\n\n");

    // ── Risk FSM Transition (LUT lookup) ────────────────────────────────
    {
        ControlAuditTrail trail;
        RiskControlFSM fsm;
        int toggle = 0;
        auto r = bench("RiskFSM::apply (LUT transition)", [&]() {
            if (toggle++ % 2 == 0)
                fsm.apply(RiskEvent::DrawdownWarning, toggle, trail);
            else
                fsm.apply(RiskEvent::PnlNormal, toggle, trail);
        });
        print_result(r);
    }

    // ── Risk FSM No-op (same state) ─────────────────────────────────────
    {
        ControlAuditTrail trail;
        RiskControlFSM fsm;
        auto r = bench("RiskFSM::apply (no-op, same state)", [&]() {
            fsm.apply(RiskEvent::PnlNormal, 1, trail);
        });
        print_result(r);
    }

    // ── Exec FSM Transition ─────────────────────────────────────────────
    {
        ControlAuditTrail trail;
        ExecControlFSM fsm;
        int toggle = 0;
        auto r = bench("ExecFSM::apply (LUT transition)", [&]() {
            if (toggle++ % 2 == 0)
                fsm.apply(ExecEvent::RiskEscalation, toggle, trail);
            else
                fsm.apply(ExecEvent::RiskDeescalation, toggle, trail);
        });
        print_result(r);
    }

    // ── Overload Evaluate (no overload) ─────────────────────────────────
    {
        OverloadConfig cfg;
        OverloadDetector det(cfg);
        OverloadSignals sig; // all clean
        auto r = bench("OverloadDetector::evaluate (clean)", [&]() {
            det.evaluate(sig);
        });
        print_result(r);
    }

    // ── Overload Evaluate (with signals) ────────────────────────────────
    {
        OverloadConfig cfg;
        OverloadDetector det(cfg);
        OverloadSignals sig;
        sig.tick_budget_exceeded = true;
        sig.queue_depth_high = true;
        auto r = bench("OverloadDetector::evaluate (overload)", [&]() {
            det.evaluate(sig);
        });
        print_result(r);
    }

    // ── System Mode Resolve ─────────────────────────────────────────────
    {
        ControlAuditTrail trail;
        RiskControlFSM risk;
        ExecControlFSM exec;
        risk.apply(RiskEvent::DrawdownWarning, 1, trail);
        auto r = bench("SystemModeResolver::resolve", [&]() {
            auto mode = SystemModeResolver::resolve(risk, exec, 1, 0.8);
            (void)mode;
        });
        print_result(r);
    }

    // ── Audit Trail Record ──────────────────────────────────────────────
    {
        ControlAuditTrail trail;
        ControlTransitionRecord rec;
        rec.timestamp_ns = 1000;
        rec.tick_id = 42;
        rec.domain = ControlDomain::Risk;
        rec.from_state = 0;
        rec.to_state = 1;
        auto r = bench("AuditTrail::record (64B write)", [&]() {
            trail.record(rec);
        });
        print_result(r);
    }

    // ── Full ControlPlane risk_event (FSM + propagation + audit) ────────
    {
        ControlPlane cp;
        int toggle = 0;
        auto r = bench("ControlPlane::risk_event (full)", [&]() {
            if (toggle++ % 3 == 0)
                cp.risk_event(RiskEvent::DrawdownWarning, toggle, "bench");
            else if (toggle % 3 == 1)
                cp.risk_event(RiskEvent::PnlNormal, toggle, "bench");
            else
                cp.risk_event(RiskEvent::HealthDegraded, toggle, "bench");
        });
        print_result(r);
    }

    // ── Full ControlPlane evaluate_overload ──────────────────────────────
    {
        ControlPlane cp;
        OverloadSignals sig;
        sig.tick_budget_exceeded = true;
        auto r = bench("ControlPlane::evaluate_overload", [&]() {
            cp.evaluate_overload(sig, 1);
        });
        print_result(r);
    }

    // ── ControlPlane snapshot capture ────────────────────────────────────
    {
        ControlPlane cp;
        cp.risk_event(RiskEvent::DrawdownWarning, 1);
        auto r = bench("ControlPlane::snapshot", [&]() {
            auto snap = cp.snapshot(0, 1.0);
            (void)snap;
        });
        print_result(r);
    }

    std::printf("\n═══════════════════════════════════════════════════════════════════════════\n");
    std::printf("  All benchmarks complete.\n");
    std::printf("═══════════════════════════════════════════════════════════════════════════\n");
    return 0;
}
