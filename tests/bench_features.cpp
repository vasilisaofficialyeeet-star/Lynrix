#include "orderbook/orderbook.h"
#include "trade_flow/trade_flow_engine.h"
#include "feature_engine/feature_engine.h"
#include "model_engine/model_engine.h"
#include "config/types.h"
#include "utils/clock.h"

#include <fmt/format.h>

#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>
#include <cmath>

using namespace bybit;

static void setup_orderbook(OrderBook& ob) {
    PriceLevel bids[200];
    PriceLevel asks[200];
    for (int i = 0; i < 200; ++i) {
        bids[i] = {50000.0 - i * 0.5, (200.0 - i) * 0.01};
        asks[i] = {50000.5 + i * 0.5, (200.0 - i) * 0.01};
    }
    ob.apply_snapshot(bids, 200, asks, 200, 1);
}

static void fill_trades(TradeFlowEngine& tf) {
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_dist(49900.0, 50100.0);
    std::uniform_real_distribution<double> qty_dist(0.001, 1.0);
    std::bernoulli_distribution side_dist(0.5);

    uint64_t now = Clock::now_ns();
    for (int i = 0; i < 2000; ++i) {
        Trade t;
        t.timestamp_ns = now - (2000 - i) * 1'000'000ULL;
        t.price = price_dist(rng);
        t.qty = qty_dist(rng);
        t.is_buyer_maker = side_dist(rng);
        tf.on_trade(t);
    }
}

template <typename Func>
static std::vector<uint64_t> benchmark(const std::string& name, int iterations, Func&& fn) {
    std::vector<uint64_t> latencies;
    latencies.reserve(iterations);

    // Warmup
    for (int i = 0; i < 100; ++i) fn();

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
    uint64_t p999 = latencies[static_cast<size_t>(latencies.size() * 0.999)];
    uint64_t max = latencies.back();

    fmt::print("  {:30s}  mean={:8.0f}ns  p50={:6d}ns  p99={:6d}ns  p99.9={:6d}ns  max={:6d}ns\n",
               name, mean, p50, p99, p999, max);

    return latencies;
}

int main() {
    fmt::print("═══════════════════════════════════════════════════════════════════\n");
    fmt::print("  Feature Engine & Model Benchmark\n");
    fmt::print("═══════════════════════════════════════════════════════════════════\n\n");

    constexpr int ITERATIONS = 100000;

    OrderBook ob;
    TradeFlowEngine tf;
    FeatureEngine fe;
    ModelEngine model;

    setup_orderbook(ob);
    fill_trades(tf);

    // Load dummy model weights
    std::array<double, Features::COUNT> weights;
    for (size_t i = 0; i < Features::COUNT; ++i) {
        weights[i] = 0.1 * (i + 1);
    }
    model.load_weights(weights, -0.5);

    fmt::print("  Orderbook: {} bids, {} asks\n", ob.bid_count(), ob.ask_count());
    fmt::print("  Trades: {} in buffer\n", tf.size());
    fmt::print("  Iterations: {}\n\n", ITERATIONS);

    // ─── Individual Benchmarks ──────────────────────────────────────────────

    fmt::print("  Individual Operations:\n");

    benchmark("OB imbalance(5)", ITERATIONS, [&]() {
        volatile double v = ob.imbalance(5);
        (void)v;
    });

    benchmark("OB imbalance(20)", ITERATIONS, [&]() {
        volatile double v = ob.imbalance(20);
        (void)v;
    });

    benchmark("OB microprice", ITERATIONS, [&]() {
        volatile double v = ob.microprice();
        (void)v;
    });

    benchmark("OB liquidity_slope(20)", ITERATIONS, [&]() {
        volatile double v = ob.liquidity_slope(20);
        (void)v;
    });

    benchmark("TradeFlow compute", ITERATIONS, [&]() {
        volatile auto v = tf.compute();
        (void)v;
    });

    benchmark("TradeFlow aggression_ratio", ITERATIONS, [&]() {
        volatile double v = tf.aggression_ratio(TradeFlowEngine::WINDOW_500MS);
        (void)v;
    });

    // ─── Full Feature Computation ───────────────────────────────────────────

    fmt::print("\n  Full Pipeline:\n");

    Features last_f;
    benchmark("FeatureEngine::compute", ITERATIONS, [&]() {
        last_f = fe.compute(ob, tf);
    });

    benchmark("ModelEngine::predict", ITERATIONS, [&]() {
        volatile auto v = model.predict(last_f);
        (void)v;
    });

    // ─── End-to-End ─────────────────────────────────────────────────────────

    fmt::print("\n  End-to-End (features + model):\n");

    benchmark("Features + Inference", ITERATIONS, [&]() {
        Features f = fe.compute(ob, tf);
        volatile auto v = model.predict(f);
        (void)v;
    });

    // ─── OB Delta Update ────────────────────────────────────────────────────

    fmt::print("\n  OB Updates:\n");

    std::mt19937 rng(123);
    std::uniform_real_distribution<double> qty_dist(0.01, 5.0);
    int update_idx = 0;

    benchmark("OB delta update (1 bid)", ITERATIONS, [&]() {
        double price = 50000.0 - (update_idx % 200) * 0.5;
        PriceLevel bid[1] = {{price, qty_dist(rng)}};
        PriceLevel no_asks[0] = {};
        ob.apply_delta(bid, 1, no_asks, 0, ob.seq_id() + 1);
        ++update_idx;
    });

    fmt::print("\n═══════════════════════════════════════════════════════════════════\n");

    // Final verification
    auto prediction = model.predict(last_f);
    fmt::print("\n  Sample prediction: P(up)={:.4f}  P(down)={:.4f}\n",
               prediction.probability_up, prediction.probability_down);
    fmt::print("  Sample features: imb5={:.6f} imb20={:.6f} mp_dev={:.9f}\n",
               last_f.imbalance_5, last_f.imbalance_20, last_f.microprice_dev);

    return 0;
}
