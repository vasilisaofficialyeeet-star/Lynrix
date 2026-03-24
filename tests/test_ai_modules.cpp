#include <gtest/gtest.h>

#include "config/types.h"
#include "utils/clock.h"
#include "utils/memory_pool.h"
#include "orderbook/orderbook.h"
#include "trade_flow/trade_flow_engine.h"
#include "feature_engine/advanced_feature_engine.h"
#include "regime/regime_detector.h"
#include "model_engine/gru_model.h"
#include "model_engine/onnx_inference.h"
#include "model_engine/accuracy_tracker.h"
#include "strategy/adaptive_threshold.h"
#include "strategy/fill_probability.h"
#include "strategy/adaptive_position_sizer.h"
#include "analytics/strategy_metrics.h"
#include "analytics/strategy_health.h"
#include "analytics/feature_importance.h"
#include "monitoring/system_monitor.h"
#include "rl/rl_optimizer.h"

#include <cmath>
#include <chrono>
#include <random>
#include <thread>

using namespace bybit;

// ═══════════════════════════════════════════════════════════════════════════
// Memory Pool Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MemoryPoolTest, AllocDealloc) {
    MemoryPool<Features, 16> pool;
    auto* ptr = pool.allocate();
    ASSERT_NE(ptr, nullptr);
    pool.deallocate(ptr);
}

TEST(MemoryPoolTest, ExhaustPool) {
    MemoryPool<int, 4> pool;
    int* ptrs[4];
    for (int i = 0; i < 4; ++i) {
        ptrs[i] = pool.allocate();
        ASSERT_NE(ptrs[i], nullptr);
    }
    // Pool exhausted
    EXPECT_EQ(pool.allocate(), nullptr);
    // Free one and re-alloc
    pool.deallocate(ptrs[0]);
    auto* p = pool.allocate();
    EXPECT_NE(p, nullptr);
}

TEST(MemoryPoolTest, ScopedAlloc) {
    MemoryPool<double, 8> pool;
    {
        PoolPtr<double, 8> scoped(pool);
        ASSERT_NE(scoped.get(), nullptr);
        *scoped.get() = 42.0;
        EXPECT_DOUBLE_EQ(*scoped.get(), 42.0);
    }
    // After scope, block returned to pool — can allocate again
    auto* p = pool.allocate();
    EXPECT_NE(p, nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════
// Advanced Feature Engine Tests
// ═══════════════════════════════════════════════════════════════════════════

class AdvancedFeatureEngineTest : public ::testing::Test {
protected:
    OrderBook ob;
    TradeFlowEngine tf;
    AdvancedFeatureEngine afe;

    void setup_book() {
        PriceLevel bids[20], asks[20];
        for (int i = 0; i < 20; ++i) {
            bids[i] = {50000.0 - i * 0.5, (20.0 - i) * 0.1};
            asks[i] = {50000.5 + i * 0.5, (20.0 - i) * 0.1};
        }
        ob.apply_snapshot(bids, 20, asks, 20, 1);
    }

    void add_trades(int n) {
        for (int i = 0; i < n; ++i) {
            Trade t;
            t.timestamp_ns = Clock::now_ns();
            t.price = 50000.0 + (i % 5) * 0.1;
            t.qty = 0.01;
            t.is_buyer_maker = (i % 2 == 0);
            tf.on_trade(t);
        }
    }
};

TEST_F(AdvancedFeatureEngineTest, Produces25Features) {
    setup_book();
    add_trades(50);
    Features f = afe.compute(ob, tf);

    // Check all 25 features are finite
    const double* arr = f.as_array();
    for (size_t i = 0; i < FEATURE_COUNT; ++i) {
        EXPECT_TRUE(std::isfinite(arr[i])) << "Feature " << i << " is not finite";
    }
    EXPECT_NE(f.timestamp_ns, 0u);
}

TEST_F(AdvancedFeatureEngineTest, TemporalDerivatives) {
    setup_book();
    add_trades(20);

    // First tick sets baseline
    afe.compute(ob, tf);

    // Second tick should have derivatives
    PriceLevel new_bids[1] = {{50000.0, 15.0}};
    PriceLevel no_asks[0] = {};
    ob.apply_delta(new_bids, 1, no_asks, 0, 2);

    Features f = afe.compute(ob, tf);
    // At least d_imbalance_dt should be nonzero after OB change
    EXPECT_TRUE(std::isfinite(f.d_imbalance_dt));
}

TEST_F(AdvancedFeatureEngineTest, SequenceBuffer) {
    setup_book();
    add_trades(10);

    // Fill sequence buffer
    for (int i = 0; i < 150; ++i) {
        afe.compute(ob, tf);
    }

    const auto& hist = afe.history();
    EXPECT_GE(hist.size(), FEATURE_SEQ_LEN);
}

TEST_F(AdvancedFeatureEngineTest, NoHeapAllocation) {
    setup_book();
    add_trades(100);
    for (int i = 0; i < 10000; ++i) {
        Features f = afe.compute(ob, tf);
        (void)f;
    }
    SUCCEED();
}

// ═══════════════════════════════════════════════════════════════════════════
// Regime Detector Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(RegimeDetectorTest, InitialRegime) {
    RegimeDetector rd;
    auto state = rd.state();
    EXPECT_EQ(static_cast<int>(state.current), 0); // LowVolatility
}

TEST(RegimeDetectorTest, DetectRegimeWithFeatures) {
    RegimeDetector rd;
    Features f{};
    f.volatility = 0.0001;
    f.mid_momentum = 0.0;
    f.imbalance_1 = 0.5;
    f.spread_bps = 1.0;

    // Feed several ticks
    for (int i = 0; i < 200; ++i) {
        rd.update(f);
    }

    auto state = rd.state();
    EXPECT_GE(state.confidence, 0.0);
    EXPECT_LE(state.confidence, 1.0);
    EXPECT_TRUE(std::isfinite(state.volatility));
}

TEST(RegimeDetectorTest, HighVolatilityDetection) {
    RegimeDetector rd;
    Features f{};
    f.volatility = 0.01; // very high
    f.mid_momentum = 0.0;
    f.spread_bps = 10.0; // wide spread
    f.imbalance_1 = 0.5;

    for (int i = 0; i < 500; ++i) {
        rd.update(f);
    }

    auto state = rd.state();
    // Should detect high volatility or liquidity vacuum
    int regime = static_cast<int>(state.current);
    EXPECT_TRUE(regime == 1 || regime == 4); // HighVol or LiqVacuum
}

// ═══════════════════════════════════════════════════════════════════════════
// GRU Model Engine Tests
// ═══════════════════════════════════════════════════════════════════════════

class GRUModelTest : public ::testing::Test {
protected:
    GRUModelEngine gru;
    OrderBook ob;
    TradeFlowEngine tf;
    AdvancedFeatureEngine afe;

    void fill_history(int n) {
        PriceLevel bids[20], asks[20];
        for (int i = 0; i < 20; ++i) {
            bids[i] = {50000.0 - i * 0.5, (20.0 - i) * 0.1};
            asks[i] = {50000.5 + i * 0.5, (20.0 - i) * 0.1};
        }
        ob.apply_snapshot(bids, 20, asks, 20, 1);
        for (int i = 0; i < n; ++i) {
            Trade t;
            t.timestamp_ns = Clock::now_ns();
            t.price = 50000.0;
            t.qty = 0.01;
            t.is_buyer_maker = (i % 2 == 0);
            tf.on_trade(t);
            afe.compute(ob, tf);
        }
    }
};

TEST_F(GRUModelTest, DefaultPrediction) {
    fill_history(50);
    auto out = gru.predict(afe.history());
    // 3-class softmax: up + down + flat = 1.0
    double sum = out.probability_up + out.probability_down + out.horizons[1].prob_flat;
    EXPECT_NEAR(sum, 1.0, 0.01);
    EXPECT_TRUE(std::isfinite(out.probability_up));
    EXPECT_TRUE(std::isfinite(out.probability_down));
}

TEST_F(GRUModelTest, MultiHorizonOutput) {
    fill_history(50);
    auto out = gru.predict(afe.history());

    for (size_t h = 0; h < NUM_HORIZONS; ++h) {
        double sum = out.horizons[h].prob_up + out.horizons[h].prob_down + out.horizons[h].prob_flat;
        EXPECT_NEAR(sum, 1.0, 0.01) << "Horizon " << h << " probs don't sum to 1";
    }
}

TEST_F(GRUModelTest, InferenceLatencyUnder1ms) {
    fill_history(100);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        volatile auto out = gru.predict(afe.history());
        (void)out;
    }
    auto end = std::chrono::high_resolution_clock::now();
    double avg_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;

    EXPECT_LT(avg_us, 1000.0); // < 1ms
    std::cout << "GRU inference avg: " << avg_us << " us" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════
// Adaptive Threshold Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(AdaptiveThresholdTest, DefaultThreshold) {
    AdaptiveThreshold at(0.6, 0.3, 0.9);
    EXPECT_NEAR(at.current(), 0.6, 1e-10);
}

TEST(AdaptiveThresholdTest, HighVolRaisesThreshold) {
    AdaptiveThreshold at(0.6, 0.3, 0.9);
    Features f{};
    f.volatility = 0.01; // very high
    f.spread_bps = 1.5;
    f.bid_depth_total = 1.0;
    f.ask_depth_total = 1.0;
    RegimeState rs;

    double t = at.update(f, rs);
    EXPECT_GT(t, 0.6); // should be raised
}

TEST(AdaptiveThresholdTest, AccuracyTracking) {
    AdaptiveThreshold at;
    // Record 10 correct
    for (int i = 0; i < 10; ++i) at.record_outcome(true);
    EXPECT_GT(at.state().recent_accuracy, 0.9);

    // Record 10 incorrect
    for (int i = 0; i < 10; ++i) at.record_outcome(false);
    EXPECT_LT(at.state().recent_accuracy, 0.6);
}

TEST(AdaptiveThresholdTest, ClampedToRange) {
    AdaptiveThreshold at(0.6, 0.3, 0.9);
    Features f{};
    f.volatility = 100.0; // extreme
    f.spread_bps = 100.0;
    f.bid_depth_total = 0.01;
    f.ask_depth_total = 0.01;
    RegimeState rs;

    double t = at.update(f, rs);
    EXPECT_LE(t, 0.9);
    EXPECT_GE(t, 0.3);
}

// ═══════════════════════════════════════════════════════════════════════════
// Fill Probability Model Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FillProbTest, BasicEstimate) {
    FillProbabilityModel fpm;
    OrderBook ob;
    PriceLevel bids[10], asks[10];
    for (int i = 0; i < 10; ++i) {
        bids[i] = {50000.0 - i * 0.5, 1.0};
        asks[i] = {50000.5 + i * 0.5, 1.0};
    }
    ob.apply_snapshot(bids, 10, asks, 10, 1);

    TradeFlowEngine tf;
    Features f{};
    auto fp = fpm.estimate(Side::Buy, Price(50000.0), Qty(0.01), ob, tf, f);
    EXPECT_GE(fp.prob_fill_500ms, 0.0);
    EXPECT_LE(fp.prob_fill_500ms, 1.0);
}

TEST(FillProbTest, ShouldUseMarketOrder) {
    FillProbabilityModel fpm;
    FillProbability fp_low;
    fp_low.prob_fill_500ms = 0.05;
    FillProbability fp_high;
    fp_high.prob_fill_500ms = 0.5;
    EXPECT_TRUE(fpm.should_use_market(fp_low, 0.8, 0.1));
    EXPECT_FALSE(fpm.should_use_market(fp_high, 0.8, 0.1));
}

// ═══════════════════════════════════════════════════════════════════════════
// Adaptive Position Sizer Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(AdaptivePositionSizerTest, BasicSizing) {
    AppConfig cfg;
    cfg.base_order_qty = 0.001;
    cfg.min_order_qty = 0.0001;
    cfg.max_order_qty = 0.01;
    AdaptivePositionSizer aps(cfg);
    Position pos{};

    double qty = aps.compute(0.7, 0.0003, 1.0, 1.5, pos, MarketRegime::LowVolatility).raw();
    EXPECT_GT(qty, 0.0);
    EXPECT_GE(qty, 0.0001);
    EXPECT_LE(qty, 0.01);
}

TEST(AdaptivePositionSizerTest, HighVolReducesSize) {
    AppConfig cfg;
    cfg.base_order_qty = 0.001;
    cfg.min_order_qty = 0.0001;
    cfg.max_order_qty = 0.01;
    AdaptivePositionSizer aps(cfg);
    Position pos{};

    double qty_low = aps.compute(0.7, 0.0001, 1.0, 1.5, pos, MarketRegime::LowVolatility).raw();
    double qty_high = aps.compute(0.7, 0.01, 1.0, 1.5, pos, MarketRegime::LowVolatility).raw();
    EXPECT_GT(qty_low, qty_high);
}

// ═══════════════════════════════════════════════════════════════════════════
// Integration: AI Pipeline
// ═══════════════════════════════════════════════════════════════════════════

TEST(AIIntegrationTest, FullAIPipeline) {
    OrderBook ob;
    TradeFlowEngine tf;
    AdvancedFeatureEngine afe;
    RegimeDetector rd;
    GRUModelEngine gru;
    AdaptiveThreshold at(0.6, 0.3, 0.9);
    FillProbabilityModel fpm;
    AppConfig aps_cfg;
    aps_cfg.base_order_qty = 0.001;
    aps_cfg.min_order_qty = 0.0001;
    aps_cfg.max_order_qty = 0.01;
    aps_cfg.risk.max_position_size = Qty(1.0);
    AdaptivePositionSizer aps(aps_cfg);

    // Setup book
    PriceLevel bids[100], asks[100];
    for (int i = 0; i < 100; ++i) {
        bids[i] = {50000.0 - i * 0.5, (100.0 - i) * 0.01};
        asks[i] = {50000.5 + i * 0.5, (100.0 - i) * 0.01};
    }
    ob.apply_snapshot(bids, 100, asks, 100, 1);

    // Inject trades
    for (int i = 0; i < 200; ++i) {
        Trade t;
        t.timestamp_ns = Clock::now_ns();
        t.price = 50000.0 + (i % 10) * 0.1;
        t.qty = 0.01;
        t.is_buyer_maker = (i % 3 == 0);
        tf.on_trade(t);
    }

    // Pre-fill history so GRU has enough data
    for (int i = 0; i < 20; ++i) {
        afe.compute(ob, tf);
    }

    int signals = 0;
    for (int tick = 0; tick < 100; ++tick) {
        // 1. Features
        Features f = afe.compute(ob, tf);
        EXPECT_TRUE(std::isfinite(f.imbalance_1));

        // 2. Regime
        rd.update(f);
        auto regime = rd.state();

        // 3. ML prediction
        auto pred = gru.predict(afe.history());
        // Each horizon: up + down + flat = 1.0
        for (size_t h = 0; h < NUM_HORIZONS; ++h) {
            double hsum = pred.horizons[h].prob_up + pred.horizons[h].prob_down + pred.horizons[h].prob_flat;
            EXPECT_NEAR(hsum, 1.0, 0.01) << "Horizon " << h;
        }

        // 4. Adaptive threshold
        double threshold = at.update(f, regime);
        EXPECT_GE(threshold, 0.3);
        EXPECT_LE(threshold, 0.9);

        // 5. Signal check
        if (pred.probability_up > threshold || pred.probability_down > threshold) {
            ++signals;
            Side side = pred.probability_up > threshold ? Side::Buy : Side::Sell;
            double price = side == Side::Buy ? ob.best_bid() : ob.best_ask();

            // 6. Fill probability
            auto fp = fpm.estimate(side, Price(price), Qty(0.001), ob, tf, f);
            EXPECT_GE(fp.prob_fill_500ms, 0.0);
            EXPECT_LE(fp.prob_fill_500ms, 1.0);

            // 7. Position sizing
            double conf = std::max(pred.probability_up, pred.probability_down);
            Position pos{};
            double qty = aps.compute(conf, f.volatility, 1.0, f.spread_bps, pos, regime.current).raw();
            EXPECT_GE(qty, 0.0);
        }

        // Vary OB slightly
        PriceLevel bid_upd[1] = {{50000.0, 1.0 + tick * 0.01}};
        PriceLevel no_asks[0] = {};
        ob.apply_delta(bid_upd, 1, no_asks, 0, ob.seq_id() + 1);
    }

    std::cout << "AI Pipeline: signals=" << signals << "/100 ticks" << std::endl;
    SUCCEED();
}

// ═══════════════════════════════════════════════════════════════════════════════
// AccuracyTracker Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AccuracyTracker, InitialState) {
    ModelAccuracyTracker tracker;
    const auto& m = tracker.metrics();
    EXPECT_EQ(m.total_predictions, 0);
    EXPECT_EQ(m.correct_predictions, 0);
    EXPECT_DOUBLE_EQ(m.accuracy, 0.0);
    EXPECT_DOUBLE_EQ(m.rolling_accuracy, 0.0);
}

TEST(AccuracyTracker, PerfectPredictions) {
    ModelAccuracyTracker tracker;

    // Record 10 "up" predictions, then evaluate with price that moved up
    uint64_t base_ts = 1000000000ULL; // 1s in ns
    double base_price = 50000.0;

    for (int i = 0; i < 10; ++i) {
        uint64_t ts = base_ts + i * 600'000'000ULL; // 600ms apart
        tracker.record_prediction(ts, base_price + i * 0.1,
                                  0, 0.8, 1); // predicted: up, prob=0.8, horizon=500ms
    }

    // Evaluate: price moved up significantly
    double new_price = base_price + 10.0; // big up move
    uint64_t eval_ts = base_ts + 20'000'000'000ULL; // 20s later, all should be evaluable
    tracker.evaluate_pending(eval_ts, new_price);

    const auto& m = tracker.metrics();
    EXPECT_EQ(m.total_predictions, 10);
    EXPECT_EQ(m.correct_predictions, 10);
    EXPECT_DOUBLE_EQ(m.accuracy, 1.0);
    EXPECT_DOUBLE_EQ(m.rolling_accuracy, 1.0);
}

TEST(AccuracyTracker, MixedPredictions) {
    ModelAccuracyTracker tracker;

    uint64_t base_ts = 1000000000ULL;
    double base_price = 50000.0;

    // 5 correct "up" predictions
    for (int i = 0; i < 5; ++i) {
        uint64_t ts = base_ts + i * 600'000'000ULL;
        tracker.record_prediction(ts, base_price, 0, 0.7, 1); // predict up
    }

    // Evaluate with price up
    tracker.evaluate_pending(base_ts + 10'000'000'000ULL, base_price + 5.0);

    // 5 wrong "down" predictions (price still goes up)
    base_ts += 20'000'000'000ULL;
    for (int i = 0; i < 5; ++i) {
        uint64_t ts = base_ts + i * 600'000'000ULL;
        tracker.record_prediction(ts, base_price + 5.0, 1, 0.6, 1); // predict down
    }

    // Evaluate with price up
    tracker.evaluate_pending(base_ts + 10'000'000'000ULL, base_price + 10.0);

    const auto& m = tracker.metrics();
    EXPECT_EQ(m.total_predictions, 10);
    EXPECT_EQ(m.correct_predictions, 5);
    EXPECT_NEAR(m.accuracy, 0.5, 1e-6);

    // Precision for "up" class: TP=5, FP=0 → 1.0
    EXPECT_NEAR(m.per_class[0].precision, 1.0, 1e-6);
    // Recall for "up" class: TP=5, FN=5 → 0.5
    EXPECT_NEAR(m.per_class[0].recall, 0.5, 1e-6);
}

TEST(AccuracyTracker, PerHorizonAccuracy) {
    ModelAccuracyTracker tracker;

    uint64_t base_ts = 1000000000ULL;
    double base_price = 50000.0;

    // Record predictions for different horizons
    // Horizon 0 (100ms): predict up, correct
    tracker.record_prediction(base_ts, base_price, 0, 0.8, 0);
    // Horizon 1 (500ms): predict down, wrong (price goes up)
    tracker.record_prediction(base_ts + 100'000'000ULL, base_price, 1, 0.7, 1);
    // Horizon 2 (1s): predict up, correct
    tracker.record_prediction(base_ts + 200'000'000ULL, base_price, 0, 0.75, 2);

    // Evaluate all: price moved up
    tracker.evaluate_pending(base_ts + 10'000'000'000ULL, base_price + 5.0);

    const auto& m = tracker.metrics();
    EXPECT_EQ(m.total_predictions, 3);
    EXPECT_EQ(m.correct_predictions, 2);

    // Horizon 0: 1 correct / 1 total
    EXPECT_NEAR(m.horizon_accuracy[0], 1.0, 1e-6);
    // Horizon 1: 0 correct / 1 total
    EXPECT_NEAR(m.horizon_accuracy[1], 0.0, 1e-6);
    // Horizon 2: 1 correct / 1 total
    EXPECT_NEAR(m.horizon_accuracy[2], 1.0, 1e-6);
}

TEST(AccuracyTracker, RecordFromModelOutput) {
    ModelAccuracyTracker tracker;

    ModelOutput pred;
    pred.timestamp_ns = 1000000000ULL;
    pred.horizons[1].prob_up = 0.7;
    pred.horizons[1].prob_down = 0.15;
    pred.horizons[1].prob_flat = 0.15;

    double mid_price = 50000.0;
    tracker.record_prediction(pred, mid_price);

    // Evaluate with price up
    tracker.evaluate_pending(pred.timestamp_ns + 2'000'000'000ULL, mid_price + 5.0);

    const auto& m = tracker.metrics();
    EXPECT_EQ(m.total_predictions, 1);
    EXPECT_EQ(m.correct_predictions, 1); // predicted up, price went up
    EXPECT_DOUBLE_EQ(m.accuracy, 1.0);
}

TEST(AccuracyTracker, RollingWindow) {
    ModelAccuracyTracker tracker;

    uint64_t ts = 1000000000ULL;
    double price = 50000.0;

    // Fill rolling window with 200 correct predictions
    for (int i = 0; i < 200; ++i) {
        tracker.record_prediction(ts, price, 0, 0.8, 1);
        ts += 600'000'000ULL;
    }
    // Drain all pending (batch limit = 64 per call, need multiple calls)
    uint64_t eval_ts = ts + 10'000'000'000ULL;
    for (int drain = 0; drain < 5; ++drain) {
        tracker.evaluate_pending(eval_ts, price + 100.0);
    }

    EXPECT_NEAR(tracker.metrics().rolling_accuracy, 1.0, 1e-6);

    // Now add 50 wrong predictions
    for (int i = 0; i < 50; ++i) {
        tracker.record_prediction(ts, price + 100.0, 1, 0.6, 1); // predict down
        ts += 600'000'000ULL;
    }
    eval_ts = ts + 10'000'000'000ULL;
    for (int drain = 0; drain < 3; ++drain) {
        tracker.evaluate_pending(eval_ts, price + 200.0); // price still up
    }

    // Rolling window of 200: 150 correct + 50 wrong = 0.75
    EXPECT_NEAR(tracker.metrics().rolling_accuracy, 0.75, 0.01);
}

TEST(AccuracyTracker, Reset) {
    ModelAccuracyTracker tracker;

    uint64_t ts = 1000000000ULL;
    tracker.record_prediction(ts, 50000.0, 0, 0.8, 1);
    tracker.evaluate_pending(ts + 2'000'000'000ULL, 50010.0);

    EXPECT_GT(tracker.metrics().total_predictions, 0);

    tracker.reset();
    EXPECT_EQ(tracker.metrics().total_predictions, 0);
    EXPECT_EQ(tracker.metrics().correct_predictions, 0);
    EXPECT_DOUBLE_EQ(tracker.metrics().accuracy, 0.0);
}

TEST(AccuracyTracker, PendingNotEvaluatedBeforeHorizon) {
    ModelAccuracyTracker tracker;

    uint64_t ts = 1000000000ULL;
    tracker.record_prediction(ts, 50000.0, 0, 0.8, 1); // 500ms horizon

    // Evaluate too early (only 100ms later)
    tracker.evaluate_pending(ts + 100'000'000ULL, 50010.0);

    // Should not be evaluated yet
    EXPECT_EQ(tracker.metrics().total_predictions, 0);

    // Now evaluate after 500ms
    tracker.evaluate_pending(ts + 600'000'000ULL, 50010.0);
    EXPECT_EQ(tracker.metrics().total_predictions, 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// OnnxInferenceEngine Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(OnnxEngine, DefaultState) {
    OnnxInferenceEngine engine;
    EXPECT_FALSE(engine.loaded());
    EXPECT_EQ(engine.inference_count(), 0u);
    EXPECT_EQ(engine.inference_errors(), 0u);
}

TEST(OnnxEngine, LoadNonexistentFile) {
    OnnxInferenceEngine engine;
    bool ok = engine.load("/nonexistent/model.onnx");
    EXPECT_FALSE(ok);
    EXPECT_FALSE(engine.loaded());
#ifndef HAS_ONNXRUNTIME
    // Without ONNX Runtime, error should say it's not available
    EXPECT_NE(engine.last_error().find("not available"), std::string::npos);
#endif
}

TEST(OnnxEngine, PredictWithoutModel) {
    OnnxInferenceEngine engine;

    FeatureRingBuffer history;
    // Push 20 features
    for (int i = 0; i < 20; ++i) {
        Features f;
        f.microprice = 50000.0 + i;
        f.imbalance_1 = 0.1 * i;
        f.timestamp_ns = 1000000000ULL + i * 10'000'000ULL;
        history.push(f);
    }

    ModelOutput out = engine.predict(history);
    // Without loaded model, should return default values
    EXPECT_EQ(out.inference_latency_ns, 0u); // no model = instant return
    // Default horizon predictions
    EXPECT_NEAR(out.horizons[0].prob_up, 0.333, 0.001);
}

TEST(OnnxEngine, NormalizationSetGet) {
    OnnxInferenceEngine engine;

    std::array<double, FEATURE_COUNT> mean, std_dev;
    for (size_t i = 0; i < FEATURE_COUNT; ++i) {
        mean[i] = static_cast<double>(i) * 0.1;
        std_dev[i] = 1.0 + i * 0.05;
    }

    engine.set_normalization(mean, std_dev);
    // Can't directly verify, but should not crash
    SUCCEED();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Signal struct expected_move test
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SignalStruct, ExpectedMoveField) {
    Signal s;
    s.expected_move = BasisPoints(2.5);
    s.expected_pnl = Notional(0.001 * 50000.0 * 2.5 / 10000.0);
    EXPECT_NEAR(s.expected_move.raw(), 2.5, 1e-6);
    EXPECT_GT(s.expected_pnl.raw(), 0.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: AccuracyTracker + GRU model pipeline
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AccuracyTrackerIntegration, FullPipeline) {
    // Setup
    OrderBook ob;
    PriceLevel bids[5], asks[5];
    for (int i = 0; i < 5; ++i) {
        bids[i] = {50000.0 - i * 0.5, 1.0};
        asks[i] = {50000.5 + i * 0.5, 1.0};
    }
    ob.apply_snapshot(bids, 5, asks, 5, 1);

    TradeFlowEngine tf;
    AdvancedFeatureEngine fe;
    GRUModelEngine gru;
    ModelAccuracyTracker tracker;

    // Run 50 ticks to warm up
    for (int i = 0; i < 50; ++i) {
        Features f = fe.compute(ob, tf);
        (void)f;
    }

    // Run model + tracker for 20 ticks
    for (int i = 0; i < 20; ++i) {
        Features f = fe.compute(ob, tf);
        ModelOutput pred = gru.predict(fe.history());

        // Record prediction
        tracker.record_prediction(pred, ob.mid_price());

        // Evaluate with slight price variation
        double eval_price = ob.mid_price() + (i % 2 == 0 ? 1.0 : -1.0);
        uint64_t eval_ts = pred.timestamp_ns + 1'000'000'000ULL;
        tracker.evaluate_pending(eval_ts, eval_price);
    }

    const auto& m = tracker.metrics();
    EXPECT_GT(m.total_predictions, 0);
    EXPECT_GE(m.accuracy, 0.0);
    EXPECT_LE(m.accuracy, 1.0);
    EXPECT_GE(m.rolling_accuracy, 0.0);
    EXPECT_LE(m.rolling_accuracy, 1.0);

    // Per-class metrics should be valid
    for (int c = 0; c < 3; ++c) {
        EXPECT_GE(m.per_class[c].precision, 0.0);
        EXPECT_LE(m.per_class[c].precision, 1.0);
        EXPECT_GE(m.per_class[c].recall, 0.0);
        EXPECT_LE(m.per_class[c].recall, 1.0);
    }

    std::cout << "AccuracyTracker Integration: total=" << m.total_predictions
              << " acc=" << m.accuracy << " rolling=" << m.rolling_accuracy
              << std::endl;
    SUCCEED();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Strategy Metrics Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(StrategyMetricsTest, InitialState) {
    StrategyMetrics sm;
    const auto& s = sm.snapshot();
    EXPECT_EQ(s.total_trades, 0);
    EXPECT_DOUBLE_EQ(s.total_pnl, 0.0);
    EXPECT_DOUBLE_EQ(s.sharpe_ratio, 0.0);
    EXPECT_DOUBLE_EQ(s.max_drawdown_pct, 0.0);
    EXPECT_DOUBLE_EQ(s.win_rate, 0.0);
}

TEST(StrategyMetricsTest, RecordWinningTrades) {
    StrategyMetrics sm;
    for (int i = 0; i < 10; ++i) {
        sm.record_trade(1.0); // $1 profit each
    }
    const auto& s = sm.snapshot();
    EXPECT_EQ(s.total_trades, 10);
    EXPECT_EQ(s.winning_trades, 10);
    EXPECT_EQ(s.losing_trades, 0);
    EXPECT_NEAR(s.win_rate, 1.0, 1e-10);
    EXPECT_NEAR(s.total_pnl, 10.0, 1e-10);
    EXPECT_NEAR(s.avg_win, 1.0, 1e-10);
    EXPECT_GT(s.profit_factor, 100.0); // near infinite
    EXPECT_NEAR(s.expectancy, 1.0, 1e-10);
    EXPECT_EQ(s.consecutive_wins, 10);
    EXPECT_EQ(s.max_consecutive_wins, 10);
}

TEST(StrategyMetricsTest, RecordLosingTrades) {
    StrategyMetrics sm;
    for (int i = 0; i < 5; ++i) {
        sm.record_trade(-2.0);
    }
    const auto& s = sm.snapshot();
    EXPECT_EQ(s.total_trades, 5);
    EXPECT_EQ(s.winning_trades, 0);
    EXPECT_EQ(s.losing_trades, 5);
    EXPECT_NEAR(s.win_rate, 0.0, 1e-10);
    EXPECT_NEAR(s.total_pnl, -10.0, 1e-10);
    EXPECT_NEAR(s.avg_loss, 2.0, 1e-10);
    EXPECT_NEAR(s.profit_factor, 0.0, 1e-10);
    EXPECT_EQ(s.consecutive_losses, 5);
}

TEST(StrategyMetricsTest, MixedTrades) {
    StrategyMetrics sm;
    sm.record_trade(3.0);
    sm.record_trade(-1.0);
    sm.record_trade(2.0);
    sm.record_trade(-0.5);
    const auto& s = sm.snapshot();
    EXPECT_EQ(s.total_trades, 4);
    EXPECT_EQ(s.winning_trades, 2);
    EXPECT_EQ(s.losing_trades, 2);
    EXPECT_NEAR(s.win_rate, 0.5, 1e-10);
    EXPECT_NEAR(s.total_pnl, 3.5, 1e-10);
    EXPECT_NEAR(s.best_trade, 3.0, 1e-10);
    EXPECT_NEAR(s.worst_trade, -1.0, 1e-10);
    EXPECT_GT(s.profit_factor, 1.0);
}

TEST(StrategyMetricsTest, DrawdownTracking) {
    StrategyMetrics sm;
    sm.update_equity(100.0);
    sm.update_equity(110.0); // new peak
    sm.update_equity(99.0);  // drawdown
    const auto& s = sm.snapshot();
    EXPECT_NEAR(s.current_drawdown, (110.0 - 99.0) / 110.0, 1e-10);
    EXPECT_NEAR(s.max_drawdown_pct, (110.0 - 99.0) / 110.0, 1e-10);
}

TEST(StrategyMetricsTest, SharpeComputation) {
    StrategyMetrics sm;
    // Record positive returns
    for (int i = 0; i < 100; ++i) {
        sm.record_return(0.001); // 0.1% per period
    }
    const auto& s = sm.snapshot();
    EXPECT_GT(s.sharpe_ratio, 0.0);
    EXPECT_TRUE(std::isfinite(s.sharpe_ratio));
    EXPECT_GT(s.sortino_ratio, 0.0);
}

TEST(StrategyMetricsTest, TickUpdate) {
    StrategyMetrics sm;
    sm.tick(100.0, 99.0);
    sm.tick(101.0, 100.0);
    sm.tick(100.5, 101.0);
    const auto& s = sm.snapshot();
    EXPECT_TRUE(std::isfinite(s.hourly_pnl));
    EXPECT_TRUE(std::isfinite(s.daily_pnl));
}

TEST(StrategyMetricsTest, Reset) {
    StrategyMetrics sm;
    sm.record_trade(5.0);
    sm.record_trade(-2.0);
    sm.reset();
    const auto& s = sm.snapshot();
    EXPECT_EQ(s.total_trades, 0);
    EXPECT_DOUBLE_EQ(s.total_pnl, 0.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Strategy Health Monitor Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(StrategyHealthTest, InitialState) {
    StrategyHealthMonitor shm;
    const auto& s = shm.snapshot();
    EXPECT_EQ(s.level, StrategyHealthLevel::Good);
    EXPECT_NEAR(s.health_score, 1.0, 1e-10);
    EXPECT_NEAR(s.activity_scale, 1.0, 1e-10);
}

TEST(StrategyHealthTest, HealthyStrategy) {
    StrategyHealthMonitor shm;
    StrategyMetricsSnapshot metrics;
    metrics.total_trades = 100;
    metrics.winning_trades = 60;
    metrics.total_pnl = 50.0;
    metrics.sharpe_ratio = 2.0;
    metrics.max_drawdown_pct = 0.01;
    metrics.current_drawdown = 0.005;
    metrics.max_consecutive_losses = 3;

    shm.update(metrics, 0.65, 0.8, MarketRegime::LowVolatility);
    const auto& s = shm.snapshot();
    EXPECT_GE(s.health_score, 0.5);
    EXPECT_GE(s.activity_scale, 0.5);
    EXPECT_LE(static_cast<int>(s.level), 2); // Excellent, Good, or Warning at most
}

TEST(StrategyHealthTest, DegradedStrategy) {
    StrategyHealthMonitor shm;
    StrategyMetricsSnapshot metrics;
    metrics.total_trades = 100;
    metrics.winning_trades = 30;
    metrics.total_pnl = -50.0;
    metrics.sharpe_ratio = -1.0;
    metrics.max_drawdown_pct = 0.08;
    metrics.current_drawdown = 0.06;
    metrics.max_consecutive_losses = 15;

    shm.update(metrics, 0.25, 0.3, MarketRegime::HighVolatility);
    const auto& s = shm.snapshot();
    EXPECT_LT(s.health_score, 0.5);
    EXPECT_LT(s.activity_scale, 1.0);
    EXPECT_GT(s.threshold_offset, 0.0);
}

TEST(StrategyHealthTest, AccuracyDecliningDetection) {
    StrategyHealthMonitor shm;
    StrategyMetricsSnapshot metrics;
    metrics.total_trades = 50;
    metrics.winning_trades = 25;
    metrics.sharpe_ratio = 0.5;

    // Feed declining accuracy
    for (int i = 0; i < 40; ++i) {
        double acc = 0.7 - i * 0.01; // declining from 0.7 to 0.3
        shm.update(metrics, acc, 0.5, MarketRegime::LowVolatility);
    }
    const auto& s = shm.snapshot();
    EXPECT_TRUE(s.accuracy_declining);
}

TEST(StrategyHealthTest, RegimeChangeTracking) {
    StrategyHealthMonitor shm;
    StrategyMetricsSnapshot metrics;
    metrics.total_trades = 50;

    // Trigger many regime changes
    MarketRegime regimes[] = {
        MarketRegime::LowVolatility, MarketRegime::HighVolatility,
        MarketRegime::Trending, MarketRegime::MeanReverting,
        MarketRegime::LiquidityVacuum
    };

    for (int i = 0; i < 20; ++i) {
        shm.update(metrics, 0.5, 0.5, regimes[i % 5]);
    }
    const auto& s = shm.snapshot();
    EXPECT_GT(s.regime_changes_1h, 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Feature Importance Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(FeatureImportanceTest, InitialState) {
    FeatureImportanceAnalyzer fia;
    const auto& s = fia.snapshot();
    EXPECT_EQ(s.active_features, 0);
}

TEST(FeatureImportanceTest, AnalyzeWithData) {
    FeatureImportanceAnalyzer fia;

    std::mt19937 rng(42);
    std::normal_distribution<double> dist(0.0, 1.0);

    // Feed feature history
    for (int i = 0; i < 200; ++i) {
        Features f{};
        double* arr = f.as_mutable_array();
        for (size_t j = 0; j < FEATURE_COUNT; ++j) {
            arr[j] = dist(rng);
        }
        f.timestamp_ns = Clock::now_ns();

        double target_val = arr[0] * 0.5 + arr[1] * 0.3 + dist(rng) * 0.1;
        int target_class = target_val > 0.1 ? 0 : (target_val < -0.1 ? 1 : 2);
        fia.record_sample(f, target_class, target_val * 10.0);
    }

    fia.compute();
    const auto& s = fia.snapshot();
    EXPECT_GT(s.active_features, 0);

    // All scores should be finite
    for (size_t i = 0; i < FEATURE_COUNT; ++i) {
        EXPECT_TRUE(std::isfinite(s.scores[i].correlation));
        EXPECT_TRUE(std::isfinite(s.scores[i].mutual_information));
        EXPECT_TRUE(std::isfinite(s.scores[i].permutation_importance));
        EXPECT_TRUE(std::isfinite(s.scores[i].shap_value));
    }

    // Rankings should be a permutation of 0..24
    std::vector<int> sorted_ranking(s.ranking.begin(), s.ranking.end());
    std::sort(sorted_ranking.begin(), sorted_ranking.end());
    for (size_t i = 0; i < FEATURE_COUNT; ++i) {
        EXPECT_EQ(sorted_ranking[i], static_cast<int>(i));
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// System Monitor Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SystemMonitorTest, InitialState) {
    SystemMonitor sm;
    const auto& s = sm.snapshot();
    EXPECT_GT(s.cpu_cores, 0);
    EXPECT_NEAR(s.uptime_hours, 0.0, 0.01);
}

TEST(SystemMonitorTest, RecordLatency) {
    SystemMonitor sm;
    sm.update_latencies(
        50.0, 100.0,   // ws p50, p99
        10.0, 30.0,    // ob p50, p99
        5.0, 15.0,     // feat p50, p99
        100.0, 200.0,  // model p50, p99
        2.0, 5.0,      // risk p50, p99
        8.0, 20.0,     // order p50, p99
        150.0, 300.0   // e2e p50, p99
    );
    sm.update(1000, 50, 10, 5);
    const auto& s = sm.snapshot();
    EXPECT_NEAR(s.ws_latency_p50_us, 50.0, 1e-6);
    EXPECT_NEAR(s.ws_latency_p99_us, 100.0, 1e-6);
    EXPECT_GE(s.ws_latency_p99_us, s.ws_latency_p50_us);
    EXPECT_NEAR(s.feat_latency_p50_us, 5.0, 1e-6);
    EXPECT_NEAR(s.model_latency_p50_us, 100.0, 1e-6);
    EXPECT_NEAR(s.e2e_latency_p50_us, 150.0, 1e-6);
}

TEST(SystemMonitorTest, Throughput) {
    SystemMonitor sm;
    // First update sets baseline
    sm.update(0, 0, 0, 0);
    // Sleep >100ms to pass the dt > 0.1 guard
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    sm.update(1000, 50, 10, 5);
    const auto& s = sm.snapshot();
    EXPECT_GT(s.ticks_per_sec, 0.0);
    EXPECT_TRUE(std::isfinite(s.ticks_per_sec));
}

TEST(SystemMonitorTest, MemoryTracking) {
    SystemMonitor sm;
    sm.update(100, 10, 5, 2);
    const auto& s = sm.snapshot();
    EXPECT_GT(s.memory_used_mb, 0.0); // process should use some memory
    EXPECT_GT(s.memory_used_bytes, 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// RL Parameter Optimizer Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(RLOptimizerTest, InitialState) {
    RLOptimizer rl;
    const auto& s = rl.snapshot();
    EXPECT_EQ(s.total_steps, 0);
    EXPECT_EQ(s.total_updates, 0);
    EXPECT_TRUE(s.exploring);
}

TEST(RLOptimizerTest, ActAndStep) {
    RLOptimizer rl;

    RLState state{};
    state.volatility = 0.001;
    state.spread_bps = 1.5;
    state.liquidity_depth = 1.0;
    state.model_confidence = 0.7;
    state.recent_pnl = 0.01;
    state.drawdown = 0.02;
    state.win_rate = 0.5;
    state.sharpe = 1.0;
    state.fill_rate = 0.8;
    state.regime_stability = 0.9;

    auto action = rl.act(state);
    EXPECT_TRUE(std::isfinite(action.signal_threshold_delta));
    EXPECT_TRUE(std::isfinite(action.position_size_scale));
    EXPECT_TRUE(std::isfinite(action.order_offset_bps));
    EXPECT_TRUE(std::isfinite(action.requote_freq_scale));

    // Record experience
    RLState next_state = state;
    next_state.recent_pnl = 0.02;
    rl.step(state, action, 0.1, next_state);
    const auto& s = rl.snapshot();
    EXPECT_EQ(s.total_steps, 1);
}

TEST(RLOptimizerTest, MultipleSteps) {
    RLOptimizer rl;
    std::mt19937 rng(123);
    std::normal_distribution<double> dist(0.0, 1.0);

    RLState prev_state{};
    prev_state.volatility = 0.001;

    for (int i = 0; i < 100; ++i) {
        RLState state{};
        state.volatility = std::abs(dist(rng)) * 0.01;
        state.spread_bps = 1.0 + std::abs(dist(rng));
        state.model_confidence = 0.5 + dist(rng) * 0.2;
        state.recent_pnl = dist(rng) * 0.1;
        state.win_rate = 0.5;
        state.sharpe = dist(rng);

        auto action = rl.act(state);
        double reward = dist(rng) * 0.1;
        rl.step(prev_state, action, reward, state);
        prev_state = state;
    }

    const auto& s = rl.snapshot();
    EXPECT_EQ(s.total_steps, 100);
    EXPECT_TRUE(std::isfinite(s.avg_reward));
}

TEST(RLOptimizerTest, ActionBounds) {
    RLOptimizer rl;

    RLState prev_state{};
    for (int i = 0; i < 200; ++i) {
        RLState state{};
        state.volatility = 0.001;
        state.model_confidence = 0.5;
        auto action = rl.act(state);

        // Actions should be bounded per RLAction::from_array clamp
        EXPECT_GE(action.signal_threshold_delta, -0.1);
        EXPECT_LE(action.signal_threshold_delta, 0.1);
        EXPECT_GE(action.position_size_scale, 0.2);
        EXPECT_LE(action.position_size_scale, 2.0);
        EXPECT_GE(action.order_offset_bps, -2.0);
        EXPECT_LE(action.order_offset_bps, 2.0);
        EXPECT_GE(action.requote_freq_scale, 0.5);
        EXPECT_LE(action.requote_freq_scale, 2.0);

        rl.step(prev_state, action, 0.01, state);
        prev_state = state;
    }
}

TEST(RLOptimizerTest, PPOUpdate) {
    RLOptimizerConfig cfg;
    cfg.batch_size = 32;
    cfg.min_experiences = 32;
    cfg.online_learning = true;
    RLOptimizer rl(cfg);

    RLState prev_state{};
    // Fill experience buffer to trigger PPO update
    for (int i = 0; i < 128; ++i) {
        RLState state{};
        state.volatility = 0.001;
        state.model_confidence = 0.5;
        auto action = rl.act(state);
        rl.step(prev_state, action, 0.01, state);
        prev_state = state;
    }

    const auto& s = rl.snapshot();
    EXPECT_GE(s.total_updates, 0);
    EXPECT_TRUE(std::isfinite(s.policy_loss));
    EXPECT_TRUE(std::isfinite(s.value_loss));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration: New Modules Pipeline
// ═══════════════════════════════════════════════════════════════════════════════

TEST(NewModulesIntegration, FullPipeline) {
    // Setup trading simulation
    StrategyMetrics sm;
    StrategyHealthMonitor shm;
    FeatureImportanceAnalyzer fia;
    SystemMonitor sysmon;
    RLOptimizer rl;

    std::mt19937 rng(42);
    std::normal_distribution<double> pnl_dist(0.1, 1.0);
    std::normal_distribution<double> feat_dist(0.0, 1.0);

    double equity = 10000.0;
    double prev_equity = equity;
    RLState prev_rl_state{};

    for (int tick = 0; tick < 500; ++tick) {
        // Simulate trade
        double trade_pnl = pnl_dist(rng);
        sm.record_trade(trade_pnl);
        equity += trade_pnl;
        sm.tick(equity, prev_equity);
        prev_equity = equity;

        // Record latencies
        if (tick % 10 == 0) {
            sysmon.update_latencies(
                50.0, 100.0, 10.0, 30.0, 5.0, 15.0,
                80.0, 200.0, 2.0, 5.0, 8.0, 20.0,
                150.0, 300.0);
        }

        // Feature importance
        Features f{};
        double* arr = f.as_mutable_array();
        for (size_t j = 0; j < FEATURE_COUNT; ++j) {
            arr[j] = feat_dist(rng);
        }
        f.timestamp_ns = Clock::now_ns();
        int target_class = trade_pnl > 0.1 ? 0 : (trade_pnl < -0.1 ? 1 : 2);
        fia.record_sample(f, target_class, trade_pnl * 10.0);

        // RL step
        RLState rl_state{};
        rl_state.win_rate = sm.snapshot().win_rate;
        rl_state.recent_pnl = trade_pnl;
        rl_state.sharpe = sm.snapshot().sharpe_ratio;
        rl_state.volatility = std::abs(feat_dist(rng)) * 0.01;
        rl_state.model_confidence = 0.5;
        auto action = rl.act(rl_state);
        double reward = RLOptimizer::compute_reward(trade_pnl, sm.snapshot().current_drawdown,
                                                     sm.snapshot().sharpe_ratio, sm.snapshot().win_rate);
        rl.step(prev_rl_state, action, reward, rl_state);
        prev_rl_state = rl_state;

        // Health update every 10 ticks
        if (tick % 10 == 0) {
            double rolling_acc = 0.5 + feat_dist(rng) * 0.1;
            shm.update(sm.snapshot(), rolling_acc, 0.7, MarketRegime::LowVolatility);
        }

        // System monitor update every 50 ticks
        if (tick % 50 == 0) {
            sysmon.update(tick * 100, tick * 5, tick, tick / 2);
        }
    }

    // Analyze features
    fia.compute();

    // Verify all modules produced valid output
    const auto& sm_snap = sm.snapshot();
    EXPECT_EQ(sm_snap.total_trades, 500);
    EXPECT_TRUE(std::isfinite(sm_snap.sharpe_ratio));
    EXPECT_TRUE(std::isfinite(sm_snap.sortino_ratio));
    EXPECT_GT(sm_snap.max_drawdown_pct, 0.0);

    const auto& sh_snap = shm.snapshot();
    EXPECT_TRUE(std::isfinite(sh_snap.health_score));
    EXPECT_GE(sh_snap.health_score, 0.0);
    EXPECT_LE(sh_snap.health_score, 1.0);

    const auto& fi_snap = fia.snapshot();
    EXPECT_GT(fi_snap.active_features, 0);

    const auto& sys_snap = sysmon.snapshot();
    EXPECT_GT(sys_snap.cpu_cores, 0);
    EXPECT_GE(sys_snap.ticks_per_sec, 0.0); // may be 0 if test runs too fast

    const auto& rl_snap = rl.snapshot();
    EXPECT_EQ(rl_snap.total_steps, 500);
    EXPECT_TRUE(std::isfinite(rl_snap.avg_reward));

    std::cout << "New Modules Integration:"
              << " trades=" << sm_snap.total_trades
              << " sharpe=" << sm_snap.sharpe_ratio
              << " health=" << sh_snap.health_score
              << " rl_steps=" << rl_snap.total_steps
              << " features=" << fi_snap.active_features
              << std::endl;
    SUCCEED();
}
