#include "utils/tsc_clock.h"
#include "utils/clock.h"

#include <fmt/format.h>

#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <numeric>

using namespace bybit;

template <typename Func>
static void benchmark(const std::string& name, int iterations, Func&& fn) {
    std::vector<uint64_t> latencies;
    latencies.reserve(iterations);

    // Warmup
    for (int i = 0; i < 1000; ++i) fn();

    for (int i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        fn();
        auto end = std::chrono::high_resolution_clock::now();
        latencies.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }

    std::sort(latencies.begin(), latencies.end());
    double mean = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    uint64_t p50 = latencies[latencies.size() / 2];
    uint64_t p99 = latencies[static_cast<size_t>(latencies.size() * 0.99)];
    uint64_t mn = latencies.front();
    uint64_t mx = latencies.back();

    fmt::print("  {:40s}  mean={:8.1f}ns  min={:4d}ns  p50={:4d}ns  p99={:5d}ns  max={:6d}ns\n",
               name, mean, mn, p50, p99, mx);
}

int main() {
    fmt::print("═══════════════════════════════════════════════════════════════════════════\n");
    fmt::print("  TSC Clock vs std::chrono Benchmark\n");
    fmt::print("═══════════════════════════════════════════════════════════════════════════\n\n");

    constexpr int ITERATIONS = 1'000'000;

    // ─── Raw timing overhead ────────────────────────────────────────────────
    fmt::print("  Timing Overhead (cost of reading clock):\n");

    benchmark("TscClock::now() (mach_absolute_time)", ITERATIONS, []() {
        volatile uint64_t v = TscClock::now();
        (void)v;
    });

    benchmark("TscClock::now_ns()", ITERATIONS, []() {
        volatile uint64_t v = TscClock::now_ns();
        (void)v;
    });

    benchmark("TscClock::rdtsc() (cntvct_el0)", ITERATIONS, []() {
        volatile uint64_t v = TscClock::rdtsc();
        (void)v;
    });

    benchmark("Clock::now_ns() (delegated to TSC)", ITERATIONS, []() {
        volatile uint64_t v = Clock::now_ns();
        (void)v;
    });

    benchmark("std::chrono::steady_clock", ITERATIONS, []() {
        volatile auto v = std::chrono::steady_clock::now();
        (void)v;
    });

    benchmark("std::chrono::high_resolution_clock", ITERATIONS, []() {
        volatile auto v = std::chrono::high_resolution_clock::now();
        (void)v;
    });

    benchmark("std::chrono::system_clock", ITERATIONS, []() {
        volatile auto v = std::chrono::system_clock::now();
        (void)v;
    });

    // ─── Interval measurement ───────────────────────────────────────────────
    fmt::print("\n  Interval Measurement (start + stop + convert):\n");

    benchmark("TSC interval (now + elapsed_ns)", ITERATIONS, []() {
        uint64_t t0 = TscClock::now();
        volatile uint64_t elapsed = TscClock::elapsed_ns(t0);
        (void)elapsed;
    });

    benchmark("ScopedTscTimer", ITERATIONS, []() {
        uint64_t out = 0;
        { ScopedTscTimer timer(out); }
        volatile uint64_t v = out;
        (void)v;
    });

    benchmark("chrono interval (steady_clock)", ITERATIONS, []() {
        auto t0 = std::chrono::steady_clock::now();
        auto t1 = std::chrono::steady_clock::now();
        volatile auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        (void)ns;
    });

    // ─── Calibration info ───────────────────────────────────────────────────
    fmt::print("\n  Calibration:\n");
    fmt::print("    Ticks per ns: {:.6f}\n", TscClock::ticks_per_ns());

    uint64_t t0 = TscClock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    uint64_t elapsed = TscClock::elapsed_ns(t0);
    fmt::print("    100ms sleep measured as: {:.3f} ms\n", elapsed / 1e6);

    fmt::print("\n═══════════════════════════════════════════════════════════════════════════\n");

    return 0;
}
