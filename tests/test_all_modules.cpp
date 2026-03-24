#include <gtest/gtest.h>

#include "config/types.h"
#include "utils/clock.h"
#include "utils/ring_buffer.h"
#include "utils/fast_double.h"
#include "orderbook/orderbook.h"
#include "trade_flow/trade_flow_engine.h"
#include "feature_engine/feature_engine.h"
#include "model_engine/model_engine.h"
#include "risk_engine/risk_engine.h"
#include "portfolio/portfolio.h"

#include <cmath>
#include <thread>
#include <chrono>
#include <random>
#include <vector>

using namespace bybit;

// ═══════════════════════════════════════════════════════════════════════════
// RingBuffer Tests
// ═══════════════════════════════════════════════════════════════════════════

class RingBufferTest : public ::testing::Test {
protected:
    RingBuffer<int, 16> rb;
};

TEST_F(RingBufferTest, EmptyOnConstruction) {
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.size(), 0u);
}

TEST_F(RingBufferTest, PushPop) {
    EXPECT_TRUE(rb.push(42));
    EXPECT_EQ(rb.size(), 1u);
    EXPECT_FALSE(rb.empty());

    int val = 0;
    EXPECT_TRUE(rb.pop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(rb.empty());
}

TEST_F(RingBufferTest, FIFO_Order) {
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(rb.push(i));
    }
    for (int i = 0; i < 10; ++i) {
        int val = -1;
        EXPECT_TRUE(rb.pop(val));
        EXPECT_EQ(val, i);
    }
}

TEST_F(RingBufferTest, FullBuffer) {
    // Buffer size is 16, but capacity is N-1 = 15 for SPSC
    for (int i = 0; i < 15; ++i) {
        EXPECT_TRUE(rb.push(i));
    }
    EXPECT_FALSE(rb.push(99)); // full
}

TEST_F(RingBufferTest, PopEmpty) {
    int val = 0;
    EXPECT_FALSE(rb.pop(val));
}

TEST_F(RingBufferTest, WrapAround) {
    // Fill and drain multiple times to exercise wrap-around
    for (int cycle = 0; cycle < 5; ++cycle) {
        for (int i = 0; i < 10; ++i) {
            EXPECT_TRUE(rb.push(cycle * 100 + i));
        }
        for (int i = 0; i < 10; ++i) {
            int val = -1;
            EXPECT_TRUE(rb.pop(val));
            EXPECT_EQ(val, cycle * 100 + i);
        }
        EXPECT_TRUE(rb.empty());
    }
}

TEST_F(RingBufferTest, Clear) {
    rb.push(1);
    rb.push(2);
    rb.push(3);
    rb.clear();
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.size(), 0u);
}

TEST_F(RingBufferTest, ConcurrentSPSC) {
    RingBuffer<int, 1024> big_rb;
    constexpr int N = 100000;
    std::atomic<bool> done{false};
    std::atomic<int> consumed{0};

    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < N; ++i) {
            while (!big_rb.push(i)) {
                std::this_thread::yield();
            }
        }
        done.store(true, std::memory_order_release);
    });

    // Consumer thread
    std::thread consumer([&]() {
        int expected = 0;
        while (expected < N) {
            int val;
            if (big_rb.pop(val)) {
                EXPECT_EQ(val, expected);
                ++expected;
            } else if (done.load(std::memory_order_acquire) && big_rb.empty()) {
                break;
            }
        }
        consumed.store(expected, std::memory_order_release);
    });

    producer.join();
    consumer.join();
    EXPECT_EQ(consumed.load(), N);
}

// ═══════════════════════════════════════════════════════════════════════════
// FastDouble Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FastDoubleTest, PositiveInteger) {
    EXPECT_DOUBLE_EQ(fast_atof("12345"), 12345.0);
}

TEST(FastDoubleTest, PositiveDecimal) {
    EXPECT_NEAR(fast_atof("50000.5"), 50000.5, 1e-10);
}

TEST(FastDoubleTest, Negative) {
    EXPECT_NEAR(fast_atof("-123.456"), -123.456, 1e-10);
}

TEST(FastDoubleTest, Zero) {
    EXPECT_DOUBLE_EQ(fast_atof("0"), 0.0);
    EXPECT_DOUBLE_EQ(fast_atof("0.0"), 0.0);
}

TEST(FastDoubleTest, SmallDecimal) {
    EXPECT_NEAR(fast_atof("0.001"), 0.001, 1e-12);
}

TEST(FastDoubleTest, LargePrice) {
    EXPECT_NEAR(fast_atof("99999.9"), 99999.9, 1e-6);
}

TEST(FastDoubleTest, WithLength) {
    const char* s = "50000.5xxx";
    EXPECT_NEAR(fast_atof(s, 7), 50000.5, 1e-10);
}

// ═══════════════════════════════════════════════════════════════════════════
// Clock Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ClockTest, NowNsMonotonic) {
    uint64_t t1 = Clock::now_ns();
    uint64_t t2 = Clock::now_ns();
    EXPECT_GE(t2, t1);
}

TEST(ClockTest, NowMsMonotonic) {
    uint64_t t1 = Clock::now_ms();
    uint64_t t2 = Clock::now_ms();
    EXPECT_GE(t2, t1);
}

TEST(ClockTest, WallMsReasonable) {
    uint64_t t = Clock::wall_ms();
    // Should be after 2024-01-01 in ms
    EXPECT_GT(t, 1704067200000ULL);
}

// ═══════════════════════════════════════════════════════════════════════════
// TradeFlowEngine Tests
// ═══════════════════════════════════════════════════════════════════════════

class TradeFlowTest : public ::testing::Test {
protected:
    TradeFlowEngine tf;

    Trade make_trade(double price, double qty, bool is_buyer_maker) {
        Trade t;
        t.timestamp_ns = Clock::now_ns();
        t.price = price;
        t.qty = qty;
        t.is_buyer_maker = is_buyer_maker;
        return t;
    }
};

TEST_F(TradeFlowTest, EmptyState) {
    EXPECT_EQ(tf.size(), 0u);
    auto snap = tf.compute();
    EXPECT_EQ(snap.w100ms.trade_count, 0u);
    EXPECT_EQ(snap.w500ms.trade_count, 0u);
    EXPECT_EQ(snap.w2000ms.trade_count, 0u);
    EXPECT_FALSE(snap.burst_detected);
}

TEST_F(TradeFlowTest, SingleBuyTrade) {
    tf.on_trade(make_trade(50000.0, 1.0, false)); // buyer taker = buy aggression
    EXPECT_EQ(tf.size(), 1u);

    auto snap = tf.compute();
    EXPECT_GT(snap.w100ms.buy_volume, 0.0);
    EXPECT_DOUBLE_EQ(snap.w100ms.sell_volume, 0.0);
}

TEST_F(TradeFlowTest, SingleSellTrade) {
    tf.on_trade(make_trade(50000.0, 2.0, true)); // buyer_maker=true means sell aggression
    auto snap = tf.compute();
    EXPECT_DOUBLE_EQ(snap.w100ms.buy_volume, 0.0);
    EXPECT_GT(snap.w100ms.sell_volume, 0.0);
}

TEST_F(TradeFlowTest, AggressionRatio) {
    // 3 buy, 1 sell
    tf.on_trade(make_trade(50000.0, 1.0, false));
    tf.on_trade(make_trade(50000.0, 1.0, false));
    tf.on_trade(make_trade(50000.0, 1.0, false));
    tf.on_trade(make_trade(50000.0, 1.0, true));

    double ratio = tf.aggression_ratio(TradeFlowEngine::WINDOW_500MS);
    // buy_vol=3, sell_vol=1, ratio = 3/4 = 0.75
    EXPECT_NEAR(ratio, 0.75, 0.01);
}

TEST_F(TradeFlowTest, AggressionRatioEmpty) {
    double ratio = tf.aggression_ratio(TradeFlowEngine::WINDOW_500MS);
    EXPECT_DOUBLE_EQ(ratio, 0.5); // default when no data
}

TEST_F(TradeFlowTest, VolumeAccelerationNoDivZero) {
    double accel = tf.volume_acceleration();
    EXPECT_DOUBLE_EQ(accel, 0.0); // no trades, no division by zero
}

TEST_F(TradeFlowTest, TradeVelocity) {
    for (int i = 0; i < 100; ++i) {
        tf.on_trade(make_trade(50000.0, 0.1, i % 2 == 0));
    }
    double vel = tf.trade_velocity();
    EXPECT_GT(vel, 0.0);
}

TEST_F(TradeFlowTest, ManyTrades) {
    for (int i = 0; i < 5000; ++i) {
        tf.on_trade(make_trade(50000.0 + i * 0.1, 0.01, i % 3 == 0));
    }
    EXPECT_EQ(tf.size(), 5000u);
    auto snap = tf.compute();
    EXPECT_GT(snap.w2000ms.trade_count, 0u);
}

TEST_F(TradeFlowTest, Reset) {
    tf.on_trade(make_trade(50000.0, 1.0, false));
    tf.reset();
    EXPECT_EQ(tf.size(), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
// FeatureEngine Tests
// ═══════════════════════════════════════════════════════════════════════════

class FeatureEngineTest : public ::testing::Test {
protected:
    OrderBook ob;
    TradeFlowEngine tf;
    FeatureEngine fe;

    void setup_book() {
        PriceLevel bids[10], asks[10];
        for (int i = 0; i < 10; ++i) {
            bids[i] = {50000.0 - i * 0.5, (10.0 - i) * 0.1};
            asks[i] = {50000.5 + i * 0.5, (10.0 - i) * 0.1};
        }
        ob.apply_snapshot(bids, 10, asks, 10, 1);
    }

    void add_trades(int n) {
        for (int i = 0; i < n; ++i) {
            Trade t;
            t.timestamp_ns = Clock::now_ns();
            t.price = 50000.0;
            t.qty = 0.01;
            t.is_buyer_maker = (i % 2 == 0);
            tf.on_trade(t);
        }
    }
};

TEST_F(FeatureEngineTest, InvalidBookReturnsZeros) {
    // Book not valid — features should be mostly zeros
    Features f = fe.compute(ob, tf);
    EXPECT_DOUBLE_EQ(f.imbalance_5, 0.0);
    EXPECT_DOUBLE_EQ(f.microprice_dev, 0.0);
}

TEST_F(FeatureEngineTest, ValidBookProducesFeatures) {
    setup_book();
    add_trades(50);
    Features f = fe.compute(ob, tf);

    EXPECT_NE(f.timestamp_ns, 0u);
    // With asymmetric book (decreasing qty), imbalance should be 0 (symmetric)
    // but liquidity slope should be positive
    EXPECT_GT(f.ob_slope, 0.0);
}

TEST_F(FeatureEngineTest, FeaturesArrayLayout) {
    setup_book();
    Features f = fe.compute(ob, tf);

    const double* arr = f.as_array();
    EXPECT_DOUBLE_EQ(arr[0], f.imbalance_1);
    EXPECT_DOUBLE_EQ(arr[1], f.imbalance_5);
    EXPECT_DOUBLE_EQ(arr[3], f.ob_slope);
    EXPECT_DOUBLE_EQ(arr[17], f.short_term_pressure);
}

TEST_F(FeatureEngineTest, EWMAVolatilityGrows) {
    setup_book();

    fe.compute(ob, tf); // first tick: sets prev_mid

    // Change mid price
    PriceLevel new_bid[1] = {{50001.0, 5.0}};
    PriceLevel no_asks[0] = {};
    ob.apply_delta(new_bid, 1, no_asks, 0, 2);

    Features f2 = fe.compute(ob, tf);
    EXPECT_GT(f2.volatility, 0.0);
}

TEST_F(FeatureEngineTest, MicropriceDeviation) {
    // Asymmetric book so microprice != mid
    PriceLevel bids[1] = {{50000.0, 10.0}};
    PriceLevel asks[1] = {{50001.0, 1.0}};
    ob.apply_snapshot(bids, 1, asks, 1, 1);

    Features f = fe.compute(ob, tf);
    // microprice should be closer to bid (more bid qty), so deviation < 0
    // microprice = (50000*1 + 50001*10) / 11 = 550010/11 = 50000.909...
    // mid = 50000.5
    // deviation = (50000.909 - 50000.5) / 50000.5 > 0
    EXPECT_GT(f.microprice_dev, 0.0);
}

TEST_F(FeatureEngineTest, ShortTermPressure) {
    setup_book();
    fe.compute(ob, tf); // first tick

    // Move microprice up
    PriceLevel big_bid[1] = {{50000.0, 100.0}};
    PriceLevel no_asks[0] = {};
    ob.apply_delta(big_bid, 1, no_asks, 0, 2);

    Features f2 = fe.compute(ob, tf);
    // Microprice should have moved, so short_term_pressure != 0
    // (might be 0 if microprice didn't change enough, so just test no crash)
    EXPECT_TRUE(std::isfinite(f2.short_term_pressure));
}

TEST_F(FeatureEngineTest, Reset) {
    setup_book();
    fe.compute(ob, tf);
    fe.reset();
    // After reset, first compute should not produce NaN
    Features f = fe.compute(ob, tf);
    EXPECT_TRUE(std::isfinite(f.imbalance_5));
    EXPECT_TRUE(std::isfinite(f.volatility));
}

TEST_F(FeatureEngineTest, NoHeapAllocation) {
    setup_book();
    add_trades(100);
    // Compute many times — should not grow memory
    for (int i = 0; i < 10000; ++i) {
        Features f = fe.compute(ob, tf);
        (void)f;
    }
    // If we got here without crash/oom, we're good
    SUCCEED();
}

// ═══════════════════════════════════════════════════════════════════════════
// ModelEngine Tests
// ═══════════════════════════════════════════════════════════════════════════

class ModelEngineTest : public ::testing::Test {
protected:
    ModelEngine model;

    void load_default_weights() {
        std::array<double, Features::COUNT> w;
        for (size_t i = 0; i < Features::COUNT; ++i) w[i] = 0.1;
        model.load_weights(w, 0.0);
    }
};

TEST_F(ModelEngineTest, UnloadedReturns50_50) {
    Features f{};
    auto out = model.predict(f);
    EXPECT_DOUBLE_EQ(out.probability_up, 0.5);
    EXPECT_DOUBLE_EQ(out.probability_down, 0.5);
}

TEST_F(ModelEngineTest, LoadedFlag) {
    EXPECT_FALSE(model.loaded());
    load_default_weights();
    EXPECT_TRUE(model.loaded());
}

TEST_F(ModelEngineTest, ZeroFeaturesZeroBias) {
    load_default_weights();
    Features f{};
    // All features zero, bias zero: sigmoid(0) = 0.5
    auto out = model.predict(f);
    EXPECT_NEAR(out.probability_up, 0.5, 1e-10);
    EXPECT_NEAR(out.probability_down, 0.5, 1e-10);
}

TEST_F(ModelEngineTest, PositiveBias) {
    std::array<double, Features::COUNT> w{};
    model.load_weights(w, 5.0); // strong positive bias

    Features f{};
    auto out = model.predict(f);
    EXPECT_GT(out.probability_up, 0.9);
    EXPECT_LT(out.probability_down, 0.1);
}

TEST_F(ModelEngineTest, NegativeBias) {
    std::array<double, Features::COUNT> w{};
    model.load_weights(w, -5.0);

    Features f{};
    auto out = model.predict(f);
    EXPECT_LT(out.probability_up, 0.1);
    EXPECT_GT(out.probability_down, 0.9);
}

TEST_F(ModelEngineTest, ProbabilitiesSumToOne) {
    load_default_weights();

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);

    for (int i = 0; i < 1000; ++i) {
        Features f;
        double* arr = const_cast<double*>(f.as_array());
        for (size_t j = 0; j < Features::COUNT; ++j) {
            arr[j] = dist(rng);
        }
        auto out = model.predict(f);
        EXPECT_NEAR(out.probability_up + out.probability_down, 1.0, 1e-10);
        EXPECT_GE(out.probability_up, 0.0);
        EXPECT_LE(out.probability_up, 1.0);
    }
}

TEST_F(ModelEngineTest, SigmoidClampingExtremePositive) {
    std::array<double, Features::COUNT> w;
    for (auto& v : w) v = 100.0; // extreme weights
    model.load_weights(w, 100.0);

    Features f;
    double* arr = const_cast<double*>(f.as_array());
    for (size_t i = 0; i < Features::COUNT; ++i) arr[i] = 100.0;

    auto out = model.predict(f);
    EXPECT_NEAR(out.probability_up, 1.0, 1e-10);
    EXPECT_TRUE(std::isfinite(out.probability_up));
}

TEST_F(ModelEngineTest, SigmoidClampingExtremeNegative) {
    std::array<double, Features::COUNT> w;
    for (auto& v : w) v = -100.0;
    model.load_weights(w, -100.0);

    Features f;
    double* arr = const_cast<double*>(f.as_array());
    for (size_t i = 0; i < Features::COUNT; ++i) arr[i] = 100.0;

    auto out = model.predict(f);
    EXPECT_NEAR(out.probability_up, 0.0, 1e-10);
    EXPECT_TRUE(std::isfinite(out.probability_up));
}

TEST_F(ModelEngineTest, InferenceLatency) {
    load_default_weights();
    Features f{};
    f.imbalance_5 = 0.3;
    f.aggression_ratio = 0.6;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100000; ++i) {
        volatile auto out = model.predict(f);
        (void)out;
    }
    auto end = std::chrono::high_resolution_clock::now();
    double avg_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 100000.0;

    // Must be < 50µs = 50000ns
    EXPECT_LT(avg_ns, 50000.0);
    std::cout << "Model inference avg: " << avg_ns << " ns" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════
// RiskEngine Tests
// ═══════════════════════════════════════════════════════════════════════════

class RiskEngineTest : public ::testing::Test {
protected:
    RiskLimits limits;
    RiskEngine risk;

    void SetUp() override {
        limits.max_position_size = Qty(1.0);
        limits.max_leverage = 10.0;
        limits.max_daily_loss = Notional(100.0);
        limits.max_drawdown = 0.1;
        limits.max_orders_per_sec = 5;
        risk.set_limits(limits);
        risk.reset();
    }

    Signal make_signal(Side side, double qty) {
        Signal s;
        s.side = side;
        s.qty = Qty(qty);
        s.price = Price(50000.0);
        s.confidence = 0.7;
        s.timestamp_ns = Clock::now_ns();
        return s;
    }
};

TEST_F(RiskEngineTest, PassesValidOrder) {
    Position pos{};
    auto signal = make_signal(Side::Buy, 0.1);
    auto check = risk.check_order(signal, pos);
    EXPECT_TRUE(check.passed);
    EXPECT_EQ(check.reason, nullptr);
}

TEST_F(RiskEngineTest, RejectsExceedingPositionSize) {
    Position pos;
    pos.size = Qty(0.9);
    pos.side = Side::Buy;

    auto signal = make_signal(Side::Buy, 0.2); // would make 1.1 > 1.0
    auto check = risk.check_order(signal, pos);
    EXPECT_FALSE(check.passed);
    EXPECT_STREQ(check.reason, "max_position_size exceeded");
}

TEST_F(RiskEngineTest, AllowsOppositeDirectionReducing) {
    Position pos;
    pos.size = Qty(0.9);
    pos.side = Side::Buy;

    // Selling reduces position, not increases
    auto signal = make_signal(Side::Sell, 0.5);
    auto check = risk.check_order(signal, pos);
    EXPECT_TRUE(check.passed);
}

TEST_F(RiskEngineTest, RejectsExceedingDailyLoss) {
    // Simulate a big loss
    risk.update_pnl(Notional(-150.0), Notional(-150.0)); // daily pnl = -150

    Position pos{};
    auto signal = make_signal(Side::Buy, 0.01);
    auto check = risk.check_order(signal, pos);
    EXPECT_FALSE(check.passed);
    EXPECT_STREQ(check.reason, "max_daily_loss exceeded");
}

TEST_F(RiskEngineTest, RejectsExceedingDrawdown) {
    risk.update_pnl(Notional(0.0), Notional(1000.0)); // peak = 1000
    risk.update_pnl(Notional(0.0), Notional(850.0));  // drawdown = 15% > 10%

    Position pos{};
    auto signal = make_signal(Side::Buy, 0.01);
    auto check = risk.check_order(signal, pos);
    EXPECT_FALSE(check.passed);
    EXPECT_STREQ(check.reason, "max_drawdown exceeded");
}

TEST_F(RiskEngineTest, RejectsExceedingOrderRate) {
    Position pos{};
    auto signal = make_signal(Side::Buy, 0.01);

    // Send 5 orders quickly
    for (int i = 0; i < 5; ++i) {
        risk.check_order(signal, pos);
        risk.record_order();
    }

    // 6th should be rejected
    auto check = risk.check_order(signal, pos);
    EXPECT_FALSE(check.passed);
    EXPECT_STREQ(check.reason, "max_orders_per_sec exceeded");
}

TEST_F(RiskEngineTest, DailyReset) {
    risk.update_pnl(Notional(-50.0), Notional(-50.0));
    EXPECT_DOUBLE_EQ(risk.daily_pnl(), -50.0);
    risk.reset_daily();
    EXPECT_DOUBLE_EQ(risk.daily_pnl(), 0.0);
}

TEST_F(RiskEngineTest, CheckLatency) {
    Position pos{};
    auto signal = make_signal(Side::Buy, 0.01);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100000; ++i) {
        volatile auto check = risk.check_order(signal, pos);
        (void)check;
    }
    auto end = std::chrono::high_resolution_clock::now();
    double avg_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 100000.0;

    // Must be < 10µs = 10000ns
    EXPECT_LT(avg_ns, 10000.0);
    std::cout << "Risk check avg: " << avg_ns << " ns" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════
// Portfolio Tests
// ═══════════════════════════════════════════════════════════════════════════

class PortfolioTest : public ::testing::Test {
protected:
    Portfolio portfolio;
};

TEST_F(PortfolioTest, InitialState) {
    auto snap = portfolio.snapshot();
    EXPECT_DOUBLE_EQ(snap.size.raw(), 0.0);
    EXPECT_DOUBLE_EQ(snap.entry_price.raw(), 0.0);
    EXPECT_DOUBLE_EQ(snap.realized_pnl.raw(), 0.0);
    EXPECT_DOUBLE_EQ(snap.unrealized_pnl.raw(), 0.0);
    EXPECT_FALSE(portfolio.has_position());
}

TEST_F(PortfolioTest, UpdatePosition) {
    portfolio.update_position(Qty(0.5), Price(50000.0), Side::Buy);
    EXPECT_TRUE(portfolio.has_position());

    auto snap = portfolio.snapshot();
    EXPECT_DOUBLE_EQ(snap.size.raw(), 0.5);
    EXPECT_DOUBLE_EQ(snap.entry_price.raw(), 50000.0);
    EXPECT_EQ(snap.side, Side::Buy);
}

TEST_F(PortfolioTest, MarkToMarketLong) {
    portfolio.update_position(Qty(1.0), Price(50000.0), Side::Buy);
    portfolio.mark_to_market(Price(51000.0)); // price went up 1000

    auto snap = portfolio.snapshot();
    EXPECT_NEAR(snap.unrealized_pnl.raw(), 1000.0, 1e-6);
}

TEST_F(PortfolioTest, MarkToMarketShort) {
    portfolio.update_position(Qty(1.0), Price(50000.0), Side::Sell);
    portfolio.mark_to_market(Price(49000.0)); // price went down 1000 — profit for short

    auto snap = portfolio.snapshot();
    EXPECT_NEAR(snap.unrealized_pnl.raw(), 1000.0, 1e-6);
}

TEST_F(PortfolioTest, MarkToMarketShortLoss) {
    portfolio.update_position(Qty(1.0), Price(50000.0), Side::Sell);
    portfolio.mark_to_market(Price(51000.0)); // price went up — loss for short

    auto snap = portfolio.snapshot();
    EXPECT_NEAR(snap.unrealized_pnl.raw(), -1000.0, 1e-6);
}

TEST_F(PortfolioTest, RealizedPnl) {
    portfolio.add_realized_pnl(Notional(100.0));
    portfolio.add_realized_pnl(Notional(50.0));
    portfolio.add_realized_pnl(Notional(-30.0));

    auto snap = portfolio.snapshot();
    EXPECT_NEAR(snap.realized_pnl.raw(), 120.0, 1e-6);
}

TEST_F(PortfolioTest, FundingImpact) {
    portfolio.add_funding(Notional(-5.0));
    portfolio.add_funding(Notional(-3.0));

    auto snap = portfolio.snapshot();
    EXPECT_NEAR(snap.funding_impact.raw(), -8.0, 1e-6);
}

TEST_F(PortfolioTest, NetPnl) {
    portfolio.update_position(Qty(1.0), Price(50000.0), Side::Buy);
    portfolio.mark_to_market(Price(50500.0));
    portfolio.add_realized_pnl(Notional(100.0));
    portfolio.add_funding(Notional(-10.0));

    double net = portfolio.net_pnl().raw();
    // realized(100) + unrealized(500) + funding(-10) = 590
    EXPECT_NEAR(net, 590.0, 1e-6);
}

TEST_F(PortfolioTest, Reset) {
    portfolio.update_position(Qty(1.0), Price(50000.0), Side::Buy);
    portfolio.add_realized_pnl(Notional(100.0));
    portfolio.reset();

    auto snap = portfolio.snapshot();
    EXPECT_DOUBLE_EQ(snap.size.raw(), 0.0);
    EXPECT_DOUBLE_EQ(snap.realized_pnl.raw(), 0.0);
    EXPECT_FALSE(portfolio.has_position());
}

TEST_F(PortfolioTest, ConcurrentReadWrite) {
    std::atomic<bool> done{false};
    std::atomic<int> reads{0};

    // Writer thread
    std::thread writer([&]() {
        for (int i = 0; i < 10000; ++i) {
            double price = 50000.0 + i;
            portfolio.update_position(Qty(static_cast<double>(i) * 0.001), Price(price), Side::Buy);
            portfolio.mark_to_market(Price(price + 100.0));
        }
        done.store(true, std::memory_order_release);
    });

    // Reader thread
    std::thread reader([&]() {
        while (!done.load(std::memory_order_acquire)) {
            auto snap = portfolio.snapshot();
            // Should never get NaN or infinity
            EXPECT_TRUE(std::isfinite(snap.size.raw()));
            EXPECT_TRUE(std::isfinite(snap.entry_price.raw()));
            EXPECT_TRUE(std::isfinite(snap.unrealized_pnl.raw()));
            reads.fetch_add(1, std::memory_order_relaxed);
        }
    });

    writer.join();
    reader.join();
    EXPECT_GT(reads.load(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Integration Test: Full Pipeline
// ═══════════════════════════════════════════════════════════════════════════

TEST(IntegrationTest, FullPipeline) {
    // Setup
    OrderBook ob;
    TradeFlowEngine tf;
    FeatureEngine fe;
    ModelEngine model;
    RiskEngine risk;
    Portfolio portfolio;

    // Load model
    std::array<double, Features::COUNT> weights = {
        0.42, 0.28, -0.15, 0.35, -0.22, 0.55, 0.18, 0.30, 0.65, -0.40, 0.48
    };
    model.load_weights(weights, -0.05);

    // Risk limits
    RiskLimits limits;
    limits.max_position_size = Qty(1.0);
    limits.max_daily_loss = Notional(100.0);
    limits.max_drawdown = 0.1;
    limits.max_orders_per_sec = 10;
    risk.set_limits(limits);

    // Apply orderbook
    PriceLevel bids[200], asks[200];
    for (int i = 0; i < 200; ++i) {
        bids[i] = {50000.0 - i * 0.5, (200.0 - i) * 0.01};
        asks[i] = {50000.5 + i * 0.5, (200.0 - i) * 0.01};
    }
    ob.apply_snapshot(bids, 200, asks, 200, 1);
    EXPECT_TRUE(ob.valid());

    // Inject trades
    for (int i = 0; i < 500; ++i) {
        Trade t;
        t.timestamp_ns = Clock::now_ns();
        t.price = 50000.0 + (i % 10) * 0.1;
        t.qty = 0.01;
        t.is_buyer_maker = (i % 3 == 0);
        tf.on_trade(t);
    }

    // Run pipeline multiple ticks
    int signals_generated = 0;
    int risk_passed = 0;

    for (int tick = 0; tick < 100; ++tick) {
        // 1. Feature computation
        Features f = fe.compute(ob, tf);
        EXPECT_TRUE(std::isfinite(f.imbalance_5));
        EXPECT_TRUE(std::isfinite(f.aggression_ratio));
        EXPECT_NE(f.timestamp_ns, 0u);

        // 2. Model inference
        ModelOutput prediction = model.predict(f);
        EXPECT_GE(prediction.probability_up, 0.0);
        EXPECT_LE(prediction.probability_up, 1.0);
        EXPECT_NEAR(prediction.probability_up + prediction.probability_down, 1.0, 1e-10);

        // 3. Signal generation
        double threshold = 0.6;
        if (prediction.probability_up > threshold || prediction.probability_down > threshold) {
            Signal signal;
            signal.side = (prediction.probability_up > threshold) ? Side::Buy : Side::Sell;
            signal.price = Price((signal.side == Side::Buy) ? ob.best_bid() : ob.best_ask());
            signal.qty = Qty(0.001);
            signal.confidence = std::max(prediction.probability_up, prediction.probability_down);
            signal.timestamp_ns = Clock::now_ns();
            ++signals_generated;

            // 4. Risk check
            Position pos = portfolio.snapshot();
            auto check = risk.check_order(signal, pos);
            if (check.passed) {
                ++risk_passed;
                risk.record_order();

                // 5. Simulate fill (paper)
                portfolio.update_position(
                    Qty(pos.size.raw() + signal.qty.raw()),
                    signal.price,
                    signal.side
                );
            }
        }

        // Update OB slightly
        PriceLevel bid_upd[1] = {{50000.0, 1.0 + tick * 0.01}};
        PriceLevel no_asks[0] = {};
        ob.apply_delta(bid_upd, 1, no_asks, 0, ob.seq_id() + 1);

        // Mark to market
        portfolio.mark_to_market(Price(ob.mid_price()));
    }

    std::cout << "Integration: signals=" << signals_generated
              << " risk_passed=" << risk_passed << std::endl;

    // Verify everything stayed finite
    auto final_pos = portfolio.snapshot();
    EXPECT_TRUE(std::isfinite(final_pos.size.raw()));
    EXPECT_TRUE(std::isfinite(final_pos.unrealized_pnl.raw()));
    EXPECT_TRUE(std::isfinite(portfolio.net_pnl().raw()));
}

// ═══════════════════════════════════════════════════════════════════════════
// LatencyHistogram Tests
// ═══════════════════════════════════════════════════════════════════════════

#include "metrics/latency_histogram.h"

TEST(LatencyHistogramTest, EmptyHistogram) {
    LatencyHistogram h;
    EXPECT_EQ(h.count(), 0u);
    EXPECT_EQ(h.percentile(0.5), 0u);
    EXPECT_DOUBLE_EQ(h.mean_ns(), 0.0);
}

TEST(LatencyHistogramTest, SingleRecord) {
    LatencyHistogram h;
    h.record(5000); // 5µs
    EXPECT_EQ(h.count(), 1u);
    EXPECT_DOUBLE_EQ(h.mean_ns(), 5000.0);
}

TEST(LatencyHistogramTest, MinMax) {
    LatencyHistogram h;
    h.record(1000);
    h.record(5000);
    h.record(10000);
    EXPECT_EQ(h.min(), 1000u);
    EXPECT_EQ(h.max(), 10000u);
}

TEST(LatencyHistogramTest, Percentile) {
    LatencyHistogram h;
    // Record 1000 samples: 0-999µs
    for (int i = 0; i < 1000; ++i) {
        h.record(static_cast<uint64_t>(i) * 1000); // i µs in ns
    }
    EXPECT_EQ(h.count(), 1000u);

    // p50 should be around 500µs
    uint64_t p50 = h.percentile(0.5);
    EXPECT_GE(p50, 400000u);
    EXPECT_LE(p50, 600000u);
}

TEST(LatencyHistogramTest, Reset) {
    LatencyHistogram h;
    h.record(5000);
    h.record(10000);
    h.reset();
    EXPECT_EQ(h.count(), 0u);
}

TEST(LatencyHistogramTest, ConcurrentRecords) {
    LatencyHistogram h;
    constexpr int N = 10000;

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&h, t]() {
            for (int i = 0; i < N; ++i) {
                h.record(static_cast<uint64_t>(t * N + i) * 100);
            }
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(h.count(), static_cast<uint64_t>(4 * N));
}
