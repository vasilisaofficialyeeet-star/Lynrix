#include "orderbook/orderbook_v3.h"
#include "config/types.h"
#include "utils/tsc_clock.h"

#include <fmt/format.h>

#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>

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

    fmt::print("  {:36s}  mean={:8.1f}ns  min={:5d}ns  p50={:5d}ns  p99={:5d}ns  p99.9={:6d}ns  max={:6d}ns\n",
               name, mean, mn, p50, p99, p999, mx);
}

int main() {
    fmt::print("═══════════════════════════════════════════════════════════════════════════\n");
    fmt::print("  OrderBook v3 Benchmark (O(1) hashmap + intrusive linked list)\n");
    fmt::print("═══════════════════════════════════════════════════════════════════════════\n\n");

    constexpr int ITERATIONS = 500000;

    OrderBook ob;

    // ─── Setup: 200-level book ──────────────────────────────────────────────
    PriceLevel bids[200];
    PriceLevel asks[200];
    for (int i = 0; i < 200; ++i) {
        bids[i] = {50000.0 - i * 0.5, (200.0 - i) * 0.01};
        asks[i] = {50000.5 + i * 0.5, (200.0 - i) * 0.01};
    }
    ob.apply_snapshot(bids, 200, asks, 200, 1);

    fmt::print("  Book: {} bids, {} asks\n", ob.bid_count(), ob.ask_count());
    fmt::print("  Iterations: {}\n\n", ITERATIONS);

    // ─── O(1) Modify existing level ─────────────────────────────────────────
    fmt::print("  O(1) Operations (existing price):\n");

    int modify_idx = 0;
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> qty_dist(0.01, 5.0);

    benchmark("Modify existing bid (O(1))", ITERATIONS, [&]() {
        double price = 50000.0 - (modify_idx % 200) * 0.5;
        PriceLevel bid[1] = {{price, qty_dist(rng)}};
        PriceLevel no_asks[0] = {};
        ob.apply_delta(bid, 1, no_asks, 0, ob.seq_id() + 1);
        ++modify_idx;
    });

    modify_idx = 0;
    benchmark("Modify existing ask (O(1))", ITERATIONS, [&]() {
        double price = 50000.5 + (modify_idx % 200) * 0.5;
        PriceLevel no_bids[0] = {};
        PriceLevel ask[1] = {{price, qty_dist(rng)}};
        ob.apply_delta(no_bids, 0, ask, 1, ob.seq_id() + 1);
        ++modify_idx;
    });

    // ─── BBO access ─────────────────────────────────────────────────────────
    fmt::print("\n  BBO Access:\n");

    benchmark("best_bid()", ITERATIONS, [&]() {
        volatile double v = ob.best_bid();
        (void)v;
    });

    benchmark("best_ask()", ITERATIONS, [&]() {
        volatile double v = ob.best_ask();
        (void)v;
    });

    benchmark("microprice()", ITERATIONS, [&]() {
        volatile double v = ob.microprice();
        (void)v;
    });

    benchmark("spread()", ITERATIONS, [&]() {
        volatile double v = ob.spread();
        (void)v;
    });

    // ─── Depth queries ──────────────────────────────────────────────────────
    fmt::print("\n  Depth Queries:\n");

    benchmark("imbalance(1)", ITERATIONS, [&]() {
        volatile double v = ob.imbalance(1);
        (void)v;
    });

    benchmark("imbalance(5)", ITERATIONS, [&]() {
        volatile double v = ob.imbalance(5);
        (void)v;
    });

    benchmark("imbalance(20)", ITERATIONS, [&]() {
        volatile double v = ob.imbalance(20);
        (void)v;
    });

    benchmark("vwap(20)", ITERATIONS, [&]() {
        volatile double v = ob.vwap(20);
        (void)v;
    });

    benchmark("liquidity_slope(20)", ITERATIONS, [&]() {
        volatile double v = ob.liquidity_slope(20);
        (void)v;
    });

    benchmark("total_bid_qty(200)", ITERATIONS, [&]() {
        volatile double v = ob.total_bid_qty(200);
        (void)v;
    });

    // ─── Insert + Remove (worst case) ───────────────────────────────────────
    fmt::print("\n  Insert/Remove (worst-case new price):\n");

    // Reset book for clean insert test
    ob.apply_snapshot(bids, 200, asks, 200, 1);

    int insert_idx = 0;
    benchmark("Insert new best bid + remove", 100000, [&]() {
        // Insert new best bid
        double new_price = 50001.0 + insert_idx * 0.1;
        PriceLevel nbid[1] = {{new_price, 1.0}};
        PriceLevel no_asks[0] = {};
        ob.apply_delta(nbid, 1, no_asks, 0, ob.seq_id() + 1);
        // Remove it
        PriceLevel rm[1] = {{new_price, 0.0}};
        ob.apply_delta(rm, 1, no_asks, 0, ob.seq_id() + 1);
        ++insert_idx;
    });

    fmt::print("\n═══════════════════════════════════════════════════════════════════════════\n");

    // Final state
    fmt::print("\n  Final book state: {} bids, {} asks\n", ob.bid_count(), ob.ask_count());
    fmt::print("  Best bid: {:.1f} @ {:.4f}\n", ob.best_bid(), ob.best_bid_qty());
    fmt::print("  Best ask: {:.1f} @ {:.4f}\n", ob.best_ask(), ob.best_ask_qty());
    fmt::print("  Spread: {:.2f}\n", ob.spread());

    return 0;
}
