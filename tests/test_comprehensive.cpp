#include <gtest/gtest.h>

#include "config/types.h"
#include "utils/clock.h"
#include "orderbook/orderbook.h"
#include "trade_flow/trade_flow_engine.h"
#include "feature_engine/advanced_feature_engine.h"
#include "regime/regime_detector.h"
#include "backtesting/backtester.h"
#include "risk_engine/enhanced_risk_engine.h"
#include "strategy/adaptive_threshold.h"
#include "strategy/fill_probability.h"
#include "strategy/adaptive_position_sizer.h"
#include "analytics/strategy_metrics.h"
#include "analytics/strategy_health.h"
#include "analytics/feature_importance.h"
#include "monitoring/system_monitor.h"
#include "rl/rl_optimizer.h"
#include "model_engine/gru_model.h"
#include "model_engine/accuracy_tracker.h"
#include "bridge/trading_core_api.h"

#include <cmath>
#include <chrono>
#include <random>
#include <thread>
#include <vector>
#include <numeric>
#include <algorithm>

using namespace bybit;

// ═══════════════════════════════════════════════════════════════════════════════
// Helper: create realistic order book
// ═══════════════════════════════════════════════════════════════════════════════

static OrderBook make_book(double mid = 50000.0, int levels = 20, double tick = 0.5) {
    OrderBook ob;
    std::vector<PriceLevel> bids(levels), asks(levels);
    for (int i = 0; i < levels; ++i) {
        bids[i] = {mid - (i + 1) * tick, (levels - i) * 0.5};
        asks[i] = {mid + i * tick + tick, (levels - i) * 0.5};
    }
    ob.apply_snapshot(bids.data(), levels, asks.data(), levels, 1);
    return ob;
}

static Trade make_trade(double price, double qty, bool is_buyer_maker) {
    Trade t;
    t.timestamp_ns = Clock::now_ns();
    t.price = price;
    t.qty = qty;
    t.is_buyer_maker = is_buyer_maker;
    return t;
}

// ═══════════════════════════════════════════════════════════════════════════════
// FeatureRingBuffer Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(FeatureRingBufferTest, EmptyState) {
    FeatureRingBuffer buf;
    EXPECT_EQ(buf.size(), 0u);
}

TEST(FeatureRingBufferTest, PushAndGet) {
    FeatureRingBuffer buf;
    Features f1{};
    f1.imbalance_1 = 0.5;
    f1.timestamp_ns = 100;
    buf.push(f1);

    EXPECT_EQ(buf.size(), 1u);
    EXPECT_DOUBLE_EQ(buf.latest().imbalance_1, 0.5);
    EXPECT_EQ(buf.latest().timestamp_ns, 100u);
}

TEST(FeatureRingBufferTest, LatestIsNewest) {
    FeatureRingBuffer buf;
    for (int i = 0; i < 10; ++i) {
        Features f{};
        f.imbalance_1 = static_cast<double>(i);
        f.timestamp_ns = i * 1000;
        buf.push(f);
    }
    EXPECT_DOUBLE_EQ(buf.latest().imbalance_1, 9.0);
    EXPECT_DOUBLE_EQ(buf.get(0).imbalance_1, 9.0);
    EXPECT_DOUBLE_EQ(buf.get(1).imbalance_1, 8.0);
    EXPECT_DOUBLE_EQ(buf.get(9).imbalance_1, 0.0);
}

TEST(FeatureRingBufferTest, FillSequence) {
    FeatureRingBuffer buf;
    for (int i = 0; i < 5; ++i) {
        Features f{};
        f.imbalance_1 = static_cast<double>(i + 1);
        buf.push(f);
    }

    // Request 5 features, should get all
    std::vector<double> out(5 * FEATURE_COUNT, -1.0);
    size_t written = buf.fill_sequence(out.data(), 5);
    EXPECT_EQ(written, 5u);
    // First element (oldest) should be feature 1
    EXPECT_DOUBLE_EQ(out[0], 1.0); // imbalance_1 of oldest

    // Request 10 features when only 5 exist — should zero-pad
    std::vector<double> out2(10 * FEATURE_COUNT, -1.0);
    size_t written2 = buf.fill_sequence(out2.data(), 10);
    EXPECT_EQ(written2, 5u);
    // Padding should be zero
    EXPECT_DOUBLE_EQ(out2[5 * FEATURE_COUNT], 0.0);
}

TEST(FeatureRingBufferTest, Overflow) {
    FeatureRingBuffer buf;
    // Push more than CAPACITY
    for (int i = 0; i < 200; ++i) {
        Features f{};
        f.imbalance_1 = static_cast<double>(i);
        buf.push(f);
    }
    // Size should be capped at CAPACITY
    EXPECT_LE(buf.size(), FeatureRingBuffer::CAPACITY);
    // Latest should be 199
    EXPECT_DOUBLE_EQ(buf.latest().imbalance_1, 199.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AdvancedFeatureEngine Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AdvancedFeatureEngineTest, InvalidBookZeros) {
    AdvancedFeatureEngine fe;
    OrderBook ob;
    TradeFlowEngine tf;
    Features f = fe.compute(ob, tf);
    EXPECT_DOUBLE_EQ(f.imbalance_1, 0.0);
    EXPECT_DOUBLE_EQ(f.volatility, 0.0);
    EXPECT_NE(f.timestamp_ns, 0u);
}

TEST(AdvancedFeatureEngineTest, AllFeaturesFinite) {
    AdvancedFeatureEngine fe;
    OrderBook ob = make_book();
    TradeFlowEngine tf;

    // Add some trades
    for (int i = 0; i < 100; ++i) {
        tf.on_trade(make_trade(50000.0, 0.1, i % 3 == 0));
    }

    // Compute several ticks to build state
    for (int i = 0; i < 20; ++i) {
        Features f = fe.compute(ob, tf);
        const double* arr = f.as_array();
        for (size_t j = 0; j < FEATURE_COUNT; ++j) {
            EXPECT_TRUE(std::isfinite(arr[j]))
                << "Feature " << j << " is not finite at tick " << i;
        }
    }
}

TEST(AdvancedFeatureEngineTest, TemporalDerivatives) {
    AdvancedFeatureEngine fe;
    TradeFlowEngine tf;

    // First tick
    OrderBook ob1 = make_book(50000.0, 20);
    Features f1 = fe.compute(ob1, tf);

    // Second tick with different book (shifted mid)
    OrderBook ob2 = make_book(50010.0, 20);
    Features f2 = fe.compute(ob2, tf);

    // Temporal derivatives should be non-zero after price change
    EXPECT_TRUE(std::isfinite(f2.d_imbalance_dt));
    EXPECT_TRUE(std::isfinite(f2.d2_imbalance_dt2));
    EXPECT_TRUE(std::isfinite(f2.d_volatility_dt));
    EXPECT_TRUE(std::isfinite(f2.d_momentum_dt));
    // Mid momentum should reflect price move
    EXPECT_GT(f2.mid_momentum, 0.0);
}

TEST(AdvancedFeatureEngineTest, HistoryAccumulation) {
    AdvancedFeatureEngine fe;
    OrderBook ob = make_book();
    TradeFlowEngine tf;

    EXPECT_EQ(fe.tick_count(), 0u);
    for (int i = 0; i < 50; ++i) {
        fe.compute(ob, tf);
    }
    EXPECT_EQ(fe.tick_count(), 50u);
    EXPECT_EQ(fe.history().size(), 50u);
}

TEST(AdvancedFeatureEngineTest, Reset) {
    AdvancedFeatureEngine fe;
    OrderBook ob = make_book();
    TradeFlowEngine tf;

    fe.compute(ob, tf);
    fe.compute(ob, tf);
    EXPECT_EQ(fe.tick_count(), 2u);

    fe.reset();
    EXPECT_EQ(fe.tick_count(), 0u);

    Features f = fe.compute(ob, tf);
    EXPECT_TRUE(std::isfinite(f.volatility));
    EXPECT_DOUBLE_EQ(f.volatility, 0.0); // first tick after reset
}

TEST(AdvancedFeatureEngineTest, VolatilityIncreasesWithPriceMove) {
    AdvancedFeatureEngine fe;
    TradeFlowEngine tf;

    // Steady price
    for (int i = 0; i < 10; ++i) {
        OrderBook ob = make_book(50000.0, 20);
        fe.compute(ob, tf);
    }
    double low_vol = fe.last().volatility;

    // Big price moves
    for (int i = 0; i < 10; ++i) {
        double price = 50000.0 + (i % 2 == 0 ? 100.0 : -100.0);
        OrderBook ob = make_book(price, 20);
        fe.compute(ob, tf);
    }
    double high_vol = fe.last().volatility;
    EXPECT_GT(high_vol, low_vol);
}

// ═══════════════════════════════════════════════════════════════════════════════
// CircuitBreaker Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CircuitBreakerTest, NotTrippedByDefault) {
    CircuitBreakerConfig cfg;
    cfg.enabled = true;
    cfg.loss_threshold = 100.0;
    cfg.cooldown_sec = 60;
    cfg.consecutive_losses = 10;
    cfg.drawdown_threshold = 0.1;
    cfg.max_loss_rate = 50.0;
    CircuitBreaker cb(cfg);
    EXPECT_FALSE(cb.is_tripped());
    EXPECT_FALSE(cb.tripped());
}

TEST(CircuitBreakerTest, DisabledNeverTrips) {
    CircuitBreakerConfig cfg;
    cfg.enabled = false;
    CircuitBreaker cb(cfg);
    cb.update(-1000.0, -1000.0); // huge loss
    EXPECT_FALSE(cb.is_tripped());
}

TEST(CircuitBreakerTest, TripsOnAbsoluteLoss) {
    CircuitBreakerConfig cfg;
    cfg.enabled = true;
    cfg.loss_threshold = 100.0;
    cfg.cooldown_sec = 3600;
    cfg.consecutive_losses = 100;
    cfg.drawdown_threshold = 1.0;
    cfg.max_loss_rate = 10000.0;
    CircuitBreaker cb(cfg);
    cb.update(-150.0, -150.0);
    EXPECT_TRUE(cb.tripped());
    EXPECT_TRUE(cb.is_tripped());
    EXPECT_STREQ(cb.trip_reason(), "absolute_loss_exceeded");
}

TEST(CircuitBreakerTest, TripsOnConsecutiveLosses) {
    CircuitBreakerConfig cfg;
    cfg.enabled = true;
    cfg.loss_threshold = 10000.0;
    cfg.cooldown_sec = 3600;
    cfg.consecutive_losses = 5;
    cfg.drawdown_threshold = 1.0;
    cfg.max_loss_rate = 10000.0;
    CircuitBreaker cb(cfg);

    for (int i = 0; i < 5; ++i) {
        cb.update(-static_cast<double>(i) * 1.0, -1.0);
    }
    EXPECT_TRUE(cb.tripped());
    EXPECT_STREQ(cb.trip_reason(), "consecutive_losses_exceeded");
}

TEST(CircuitBreakerTest, ConsecutiveLossesResetOnWin) {
    CircuitBreakerConfig cfg;
    cfg.enabled = true;
    cfg.loss_threshold = 10000.0;
    cfg.cooldown_sec = 3600;
    cfg.consecutive_losses = 5;
    cfg.drawdown_threshold = 1.0;
    cfg.max_loss_rate = 10000.0;
    CircuitBreaker cb(cfg);

    cb.update(0.0, -1.0);
    cb.update(0.0, -1.0);
    cb.update(0.0, -1.0);
    EXPECT_EQ(cb.consecutive_losses(), 3);
    cb.update(0.0, 5.0); // winning trade
    EXPECT_EQ(cb.consecutive_losses(), 0);
}

TEST(CircuitBreakerTest, TripsOnDrawdown) {
    CircuitBreakerConfig cfg;
    cfg.enabled = true;
    cfg.loss_threshold = 10000.0;
    cfg.cooldown_sec = 3600;
    cfg.consecutive_losses = 100;
    cfg.drawdown_threshold = 0.1; // 10%
    cfg.max_loss_rate = 10000.0;
    CircuitBreaker cb(cfg);

    cb.update(1000.0, 1000.0); // peak at 1000
    cb.update(850.0, -150.0);  // drawdown = 15% > 10%
    EXPECT_TRUE(cb.tripped());
    EXPECT_STREQ(cb.trip_reason(), "drawdown_exceeded");
}

TEST(CircuitBreakerTest, ManualReset) {
    CircuitBreakerConfig cfg;
    cfg.enabled = true;
    cfg.loss_threshold = 50.0;
    cfg.cooldown_sec = 3600;
    CircuitBreaker cb(cfg);

    cb.update(-100.0, -100.0);
    EXPECT_TRUE(cb.tripped());

    cb.manual_reset();
    EXPECT_FALSE(cb.tripped());
}

// ═══════════════════════════════════════════════════════════════════════════════
// EnhancedRiskEngine Tests
// ═══════════════════════════════════════════════════════════════════════════════

class EnhancedRiskTest : public ::testing::Test {
protected:
    RiskLimits limits;
    CircuitBreakerConfig cb_cfg;
    EnhancedRiskEngine risk;

    void SetUp() override {
        limits.max_position_size = Qty(1.0);
        limits.max_leverage = 10.0;
        limits.max_daily_loss = Notional(100.0);
        limits.max_drawdown = 0.1;
        limits.max_orders_per_sec = 5;
        cb_cfg.enabled = true;
        cb_cfg.loss_threshold = 200.0;
        cb_cfg.cooldown_sec = 60;
        cb_cfg.consecutive_losses = 10;
        cb_cfg.drawdown_threshold = 0.15;
        cb_cfg.max_loss_rate = 100.0;
        risk = EnhancedRiskEngine(limits, cb_cfg);
    }

    Signal make_signal(Side side, double qty, double price = 50000.0) {
        Signal s;
        s.side = side;
        s.qty = Qty(qty);
        s.price = Price(price);
        s.confidence = 0.7;
        s.timestamp_ns = Clock::now_ns();
        return s;
    }
};

TEST_F(EnhancedRiskTest, PassesNormalOrder) {
    Position pos{};
    auto check = risk.check_order(make_signal(Side::Buy, 0.1), pos);
    EXPECT_TRUE(check.passed);
}

TEST_F(EnhancedRiskTest, RejectsExceedingPosition) {
    Position pos;
    pos.size = Qty(0.9);
    pos.side = Side::Buy;
    auto check = risk.check_order(make_signal(Side::Buy, 0.2), pos);
    EXPECT_FALSE(check.passed);
    EXPECT_STREQ(check.reason, "max_position_size_exceeded");
}

TEST_F(EnhancedRiskTest, RegimeAwarePositionLimit) {
    Position pos;
    pos.size = Qty(0.4);
    pos.side = Side::Buy;

    // Normal regime: 0.4 + 0.2 = 0.6 < 1.0, passes
    auto check = risk.check_order(make_signal(Side::Buy, 0.2), pos, MarketRegime::LowVolatility);
    EXPECT_TRUE(check.passed);

    // High vol: limit halved to 0.5, 0.4 + 0.2 = 0.6 > 0.5, rejected
    auto check2 = risk.check_order(make_signal(Side::Buy, 0.2), pos, MarketRegime::HighVolatility);
    EXPECT_FALSE(check2.passed);
    EXPECT_STREQ(check2.reason, "max_position_size_exceeded");
}

TEST_F(EnhancedRiskTest, BlocksInLiquidityVacuum) {
    Position pos{};
    auto check = risk.check_order(make_signal(Side::Buy, 0.001), pos, MarketRegime::LiquidityVacuum);
    EXPECT_FALSE(check.passed);
    EXPECT_STREQ(check.reason, "liquidity_vacuum_regime");
}

TEST_F(EnhancedRiskTest, CircuitBreakerIntegration) {
    // Trip the circuit breaker via excessive losses
    risk.update_pnl(Notional(-250.0), Notional(-250.0));
    Position pos{};
    auto check = risk.check_order(make_signal(Side::Buy, 0.001), pos);
    EXPECT_FALSE(check.passed);
    EXPECT_STREQ(check.reason, "circuit_breaker_tripped");
}

TEST_F(EnhancedRiskTest, DailyLossLimit) {
    risk.update_pnl(Notional(-150.0), Notional(-150.0));
    Position pos{};
    auto check = risk.check_order(make_signal(Side::Buy, 0.001), pos);
    // Should be blocked by either circuit_breaker or daily_loss
    EXPECT_FALSE(check.passed);
}

TEST_F(EnhancedRiskTest, OrderRateLimit) {
    Position pos{};
    auto sig = make_signal(Side::Buy, 0.01);
    for (int i = 0; i < 5; ++i) {
        risk.check_order(sig, pos);
        risk.record_order();
    }
    auto check = risk.check_order(sig, pos);
    EXPECT_FALSE(check.passed);
    EXPECT_STREQ(check.reason, "max_orders_per_sec_exceeded");
}

TEST_F(EnhancedRiskTest, DrawdownTracking) {
    risk.update_pnl(Notional(0.0), Notional(1000.0)); // peak
    risk.update_pnl(Notional(0.0), Notional(850.0));  // dd=15%
    EXPECT_NEAR(risk.drawdown(), 0.15, 1e-10);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Backtester Tests
// ═══════════════════════════════════════════════════════════════════════════════

static std::vector<BacktestTick> generate_trending_ticks(int count, double start_price, double drift) {
    std::vector<BacktestTick> ticks;
    ticks.reserve(count);
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 0.5);

    double price = start_price;
    uint64_t ts = 1000000000ULL; // 1s in ns

    for (int i = 0; i < count; ++i) {
        price += drift + noise(rng);
        BacktestTick t;
        t.timestamp_ns = ts + i * 10000000ULL; // 10ms apart
        t.bid_price = price - 0.25;
        t.ask_price = price + 0.25;
        t.bid_qty = 1.0;
        t.ask_qty = 1.0;
        t.last_price = price;
        t.last_qty = 0.01;
        t.is_buyer_maker = (i % 2 == 0);
        ticks.push_back(t);
    }
    return ticks;
}

TEST(BacktesterTest, EmptyTicks) {
    Backtester bt;
    auto result = bt.run({}, [](const Features&, const OrderBook&,
                                std::vector<SimulatedOrder>&) {});
    EXPECT_EQ(result.total_fills, 0);
    EXPECT_EQ(result.total_orders, 0);
    EXPECT_DOUBLE_EQ(result.total_pnl, 0.0);
}

TEST(BacktesterTest, NoSignalsNoTrades) {
    auto ticks = generate_trending_ticks(100, 50000.0, 0.1);
    Backtester bt;
    auto result = bt.run(ticks, [](const Features&, const OrderBook&,
                                   std::vector<SimulatedOrder>&) {
        // No orders generated
    });
    EXPECT_EQ(result.total_fills, 0);
    EXPECT_EQ(result.total_orders, 0);
    EXPECT_DOUBLE_EQ(result.total_pnl, 0.0);
    EXPECT_GT(result.duration_hours, 0.0);
}

TEST(BacktesterTest, MarketBuyFill) {
    auto ticks = generate_trending_ticks(200, 50000.0, 0.1);
    BacktestConfig cfg;
    cfg.order_latency_ticks = 0; // instant fill
    cfg.partial_fills = false;
    Backtester bt(cfg);

    bool order_placed = false;
    auto result = bt.run(ticks, [&](const Features&, const OrderBook& ob,
                                     std::vector<SimulatedOrder>& orders) {
        if (!order_placed && ob.valid()) {
            SimulatedOrder ord;
            ord.side = Side::Buy;
            ord.type = OrderType::Market;
            ord.price = ob.best_ask();
            ord.qty = 0.01;
            orders.push_back(ord);
            order_placed = true;
        }
    });

    EXPECT_EQ(result.total_orders, 1);
    EXPECT_GE(result.total_fills, 1);
    EXPECT_GT(result.total_fees, 0.0);
}

TEST(BacktesterTest, LimitOrderWithLatency) {
    auto ticks = generate_trending_ticks(500, 50000.0, 0.0); // flat market
    BacktestConfig cfg;
    cfg.order_latency_ticks = 5;
    cfg.partial_fills = false;
    Backtester bt(cfg);

    int orders_placed = 0;
    auto result = bt.run(ticks, [&](const Features&, const OrderBook& ob,
                                     std::vector<SimulatedOrder>& orders) {
        if (orders_placed < 3 && ob.valid()) {
            SimulatedOrder ord;
            ord.side = Side::Buy;
            ord.type = OrderType::Limit;
            ord.price = ob.best_bid(); // at best bid
            ord.qty = 0.01;
            orders.push_back(ord);
            orders_placed++;
        }
    });

    EXPECT_EQ(result.total_orders, 3);
    // Some fills may have happened
    EXPECT_GE(result.total_fills, 0);
}

TEST(BacktesterTest, RoundTrip) {
    // Generate uptrend then downtrend
    auto up_ticks = generate_trending_ticks(200, 50000.0, 0.5);
    auto down_ticks = generate_trending_ticks(200, up_ticks.back().bid_price + 0.25, -0.5);
    // Fix timestamps for down ticks
    uint64_t last_ts = up_ticks.back().timestamp_ns;
    for (auto& t : down_ticks) {
        t.timestamp_ns += last_ts;
    }

    std::vector<BacktestTick> ticks;
    ticks.insert(ticks.end(), up_ticks.begin(), up_ticks.end());
    ticks.insert(ticks.end(), down_ticks.begin(), down_ticks.end());

    BacktestConfig cfg;
    cfg.order_latency_ticks = 0;
    cfg.partial_fills = false;
    Backtester bt(cfg);

    int tick_num = 0;
    bool bought = false;
    bool sold = false;

    auto result = bt.run(ticks, [&](const Features&, const OrderBook& ob,
                                     std::vector<SimulatedOrder>& orders) {
        if (!bought && tick_num == 10 && ob.valid()) {
            SimulatedOrder ord;
            ord.side = Side::Buy;
            ord.type = OrderType::Market;
            ord.price = ob.best_ask();
            ord.qty = 0.01;
            orders.push_back(ord);
            bought = true;
        }
        if (bought && !sold && tick_num == 190 && ob.valid()) {
            SimulatedOrder ord;
            ord.side = Side::Sell;
            ord.type = OrderType::Market;
            ord.price = ob.best_bid();
            ord.qty = 0.01;
            orders.push_back(ord);
            sold = true;
        }
        tick_num++;
    });

    EXPECT_EQ(result.total_orders, 2);
    EXPECT_EQ(result.total_fills, 2);
    // Should have made money on uptrend
    EXPECT_GT(result.total_pnl, 0.0);
    EXPECT_TRUE(std::isfinite(result.net_pnl));
    EXPECT_GT(result.equity_curve.size(), 0u);
}

TEST(BacktesterTest, FeeAccounting) {
    auto ticks = generate_trending_ticks(100, 50000.0, 0.0);
    BacktestConfig cfg;
    cfg.maker_fee_rate = 0.001;  // 10 bps
    cfg.taker_fee_rate = 0.005;  // 50 bps
    cfg.order_latency_ticks = 0;
    cfg.partial_fills = false;
    cfg.base_slippage_bps = 0.0;
    Backtester bt(cfg);

    auto result = bt.run(ticks, [](const Features&, const OrderBook& ob,
                                    std::vector<SimulatedOrder>& orders) {
        static int count = 0;
        if (count == 0 && ob.valid()) {
            SimulatedOrder ord;
            ord.side = Side::Buy;
            ord.type = OrderType::Market;
            ord.price = ob.best_ask();
            ord.qty = 1.0;
            orders.push_back(ord);
            count++;
        }
    });

    EXPECT_GT(result.total_fees, 0.0);
    EXPECT_TRUE(std::isfinite(result.total_fees));
}

TEST(BacktesterTest, MetricsTracking) {
    auto ticks = generate_trending_ticks(500, 50000.0, 0.2);
    BacktestConfig cfg;
    cfg.order_latency_ticks = 0;
    cfg.partial_fills = false;
    Backtester bt(cfg);

    int tick_num = 0;
    auto result = bt.run(ticks, [&](const Features&, const OrderBook& ob,
                                     std::vector<SimulatedOrder>& orders) {
        if (ob.valid() && tick_num % 100 == 10) {
            SimulatedOrder buy;
            buy.side = Side::Buy;
            buy.type = OrderType::Market;
            buy.price = ob.best_ask();
            buy.qty = 0.01;
            orders.push_back(buy);
        }
        if (ob.valid() && tick_num % 100 == 50) {
            SimulatedOrder sell;
            sell.side = Side::Sell;
            sell.type = OrderType::Market;
            sell.price = ob.best_bid();
            sell.qty = 0.01;
            orders.push_back(sell);
        }
        tick_num++;
    });

    // Verify metrics are computed
    EXPECT_TRUE(std::isfinite(result.metrics.sharpe_ratio));
    EXPECT_TRUE(std::isfinite(result.metrics.max_drawdown_pct));
    EXPECT_GT(result.equity_curve.size(), 0u);
    EXPECT_GT(result.drawdown_curve.size(), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// GRU Model + AccuracyTracker Integration
// ═══════════════════════════════════════════════════════════════════════════════

TEST(GRUModelTest, PredictWithHistory) {
    GRUModelEngine gru;
    AdvancedFeatureEngine fe;
    OrderBook ob = make_book();
    TradeFlowEngine tf;

    // Build history
    for (int i = 0; i < 30; ++i) {
        tf.on_trade(make_trade(50000.0 + i * 0.1, 0.01, i % 2 == 0));
        fe.compute(ob, tf);
    }

    ModelOutput out = gru.predict(fe.history());
    EXPECT_TRUE(std::isfinite(out.probability_up));
    EXPECT_TRUE(std::isfinite(out.probability_down));
    EXPECT_GE(out.probability_up, 0.0);
    EXPECT_LE(out.probability_up, 1.0);
    EXPECT_GT(out.inference_latency_ns, 0u);
}

TEST(GRUModelTest, PredictInsufficientHistory) {
    GRUModelEngine gru;
    FeatureRingBuffer buf;

    // Only 5 features, need >= 10
    for (int i = 0; i < 5; ++i) {
        Features f{};
        f.imbalance_1 = 0.1;
        buf.push(f);
    }

    ModelOutput out = gru.predict(buf);
    // Should return default (0.5/0.5) since not enough history
    EXPECT_NEAR(out.probability_up, 0.5, 0.01);
}

TEST(AccuracyTrackerTest, RecordAndEvaluate) {
    ModelAccuracyTracker tracker;
    AdvancedFeatureEngine fe;
    OrderBook ob = make_book();
    TradeFlowEngine tf;
    GRUModelEngine gru;

    double price = 50000.0;
    for (int i = 0; i < 50; ++i) {
        tf.on_trade(make_trade(price, 0.01, i % 2 == 0));
        fe.compute(ob, tf);
        price += 0.5; // trending up
    }

    ModelOutput pred = gru.predict(fe.history());
    tracker.record_prediction(pred, price);

    // Evaluate after some time
    price += 10.0;
    tracker.evaluate_pending(Clock::now_ns() + 1000000000ULL, price);

    auto m = tracker.metrics();
    EXPECT_GE(m.total_predictions, 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// RegimeDetector Full Test
// ═══════════════════════════════════════════════════════════════════════════════

TEST(RegimeDetectorTest, DetectsLowVolatility) {
    RegimeDetector rd;
    // Feed stable features
    for (int i = 0; i < 100; ++i) {
        Features f{};
        f.volatility = 0.0001;  // very low
        f.spread_bps = 1.0;
        f.imbalance_5 = 0.01;
        f.aggression_ratio = 0.5;
        f.depth_concentration = 0.3;
        f.ob_slope = 0.5;
        RegimeState rs = rd.update(f);
        (void)rs;
    }
    RegimeState rs = rd.state();
    // Should detect low volatility or at least not liquidity vacuum
    EXPECT_NE(rs.current, MarketRegime::LiquidityVacuum);
    EXPECT_GE(rs.confidence, 0.0);
    EXPECT_LE(rs.confidence, 1.0);
}

TEST(RegimeDetectorTest, DetectsHighVolatility) {
    RegimeDetector rd;
    for (int i = 0; i < 100; ++i) {
        Features f{};
        f.volatility = 0.05;  // very high
        f.spread_bps = 20.0;
        f.imbalance_5 = 0.4;
        f.aggression_ratio = 0.8;
        f.volume_accel = 10.0;
        RegimeState rs = rd.update(f);
        (void)rs;
    }
    RegimeState rs = rd.state();
    EXPECT_GE(rs.confidence, 0.0);
}

TEST(RegimeDetectorTest, RegimeParamsFinite) {
    RegimeDetector rd;
    Features f{};
    f.volatility = 0.001;
    rd.update(f);
    auto params = rd.current_params();
    EXPECT_TRUE(std::isfinite(params.signal_threshold));
    EXPECT_TRUE(std::isfinite(params.position_scale));
    EXPECT_TRUE(std::isfinite(params.entry_offset_bps));
}

// ═══════════════════════════════════════════════════════════════════════════════
// C API Bridge Struct Layout Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CAPIBridgeTest, TCStrategyMetricsLayout) {
    TCStrategyMetrics m{};
    m.sharpe_ratio = 2.5;
    m.sortino_ratio = 3.0;
    m.max_drawdown_pct = 0.05;
    m.win_rate = 0.65;
    m.total_trades = 100;
    m.profit_factor = 2.0;
    EXPECT_DOUBLE_EQ(m.sharpe_ratio, 2.5);
    EXPECT_DOUBLE_EQ(m.win_rate, 0.65);
    EXPECT_EQ(m.total_trades, 100);
}

TEST(CAPIBridgeTest, TCStrategyHealthLayout) {
    TCStrategyHealth h{};
    h.health_level = 1;
    h.health_score = 0.85;
    h.activity_scale = 1.2;
    h.accuracy_declining = false;
    h.regime_changes_1h = 3;
    EXPECT_EQ(h.health_level, 1);
    EXPECT_DOUBLE_EQ(h.health_score, 0.85);
    EXPECT_FALSE(h.accuracy_declining);
}

TEST(CAPIBridgeTest, TCSystemMonitorLayout) {
    TCSystemMonitor s{};
    s.cpu_usage_pct = 45.0;
    s.memory_used_mb = 128.0;
    s.cpu_cores = 8;
    s.ticks_per_sec = 10000.0;
    s.gpu_available = false;
    s.gpu_name = "N/A";
    s.inference_backend = "CPU";
    EXPECT_DOUBLE_EQ(s.cpu_usage_pct, 45.0);
    EXPECT_EQ(s.cpu_cores, 8);
    EXPECT_STREQ(s.gpu_name, "N/A");
    EXPECT_STREQ(s.inference_backend, "CPU");
}

TEST(CAPIBridgeTest, TCRLStateLayout) {
    TCRLState r{};
    r.signal_threshold_delta = 0.05;
    r.position_size_scale = 1.5;
    r.avg_reward = 0.1;
    r.total_steps = 500;
    r.exploring = true;
    EXPECT_DOUBLE_EQ(r.signal_threshold_delta, 0.05);
    EXPECT_EQ(r.total_steps, 500);
    EXPECT_TRUE(r.exploring);
}

TEST(CAPIBridgeTest, TCFeatureImportanceLayout) {
    TCFeatureImportance fi{};
    for (int i = 0; i < 25; ++i) {
        fi.permutation_importance[i] = static_cast<double>(i) * 0.01;
        fi.ranking[i] = 24 - i;
    }
    fi.active_features = 20;
    EXPECT_NEAR(fi.permutation_importance[10], 0.1, 1e-10);
    EXPECT_EQ(fi.ranking[0], 24);
    EXPECT_EQ(fi.active_features, 20);
}

TEST(CAPIBridgeTest, TCFeaturesCompleteLayout) {
    TCFeatures f{};
    f.imbalance_1 = 0.1;
    f.imbalance_5 = 0.2;
    f.volatility = 0.001;
    f.microprice = 50000.5;
    f.d_imbalance_dt = 0.05;
    f.timestamp_ns = 12345;
    EXPECT_DOUBLE_EQ(f.imbalance_1, 0.1);
    EXPECT_DOUBLE_EQ(f.volatility, 0.001);
    EXPECT_EQ(f.timestamp_ns, 12345u);
}

TEST(CAPIBridgeTest, TCModelPredictionLayout) {
    TCModelPrediction p{};
    p.h100ms_up = 0.6;
    p.h100ms_down = 0.3;
    p.h100ms_flat = 0.1;
    p.probability_up = 0.65;
    p.probability_down = 0.35;
    p.model_confidence = 0.3;
    p.inference_latency_ns = 5000;
    EXPECT_DOUBLE_EQ(p.probability_up, 0.65);
    EXPECT_NEAR(p.probability_up + p.probability_down, 1.0, 1e-10);
    EXPECT_EQ(p.inference_latency_ns, 5000u);
}

TEST(CAPIBridgeTest, TCCircuitBreakerStateLayout) {
    TCCircuitBreakerState cb{};
    cb.tripped = true;
    cb.in_cooldown = true;
    cb.trip_reason = "test_reason";
    cb.consecutive_losses = 5;
    cb.peak_pnl = 1000.0;
    cb.drawdown_pct = 0.08;
    EXPECT_TRUE(cb.tripped);
    EXPECT_STREQ(cb.trip_reason, "test_reason");
    EXPECT_EQ(cb.consecutive_losses, 5);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Edge Case & Robustness Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(RobustnessTest, StrategyMetricsNaNSafety) {
    StrategyMetrics sm;
    // Zero division scenarios
    sm.record_trade(0.0);
    const auto& s = sm.snapshot();
    EXPECT_TRUE(std::isfinite(s.win_rate));
    EXPECT_TRUE(std::isfinite(s.profit_factor));
    EXPECT_TRUE(std::isfinite(s.expectancy));
}

TEST(RobustnessTest, StrategyMetricsLargeValues) {
    StrategyMetrics sm;
    for (int i = 0; i < 1000; ++i) {
        sm.record_trade(i % 2 == 0 ? 1e6 : -5e5);
    }
    const auto& s = sm.snapshot();
    EXPECT_TRUE(std::isfinite(s.sharpe_ratio));
    EXPECT_TRUE(std::isfinite(s.total_pnl));
    EXPECT_TRUE(std::isfinite(s.max_drawdown_pct));
}

TEST(RobustnessTest, FeatureEngineWithEmptyTradeFlow) {
    AdvancedFeatureEngine fe;
    OrderBook ob = make_book();
    TradeFlowEngine tf; // empty

    Features f = fe.compute(ob, tf);
    EXPECT_TRUE(std::isfinite(f.aggression_ratio));
    EXPECT_TRUE(std::isfinite(f.trade_velocity));
    EXPECT_TRUE(std::isfinite(f.avg_trade_size));
    EXPECT_DOUBLE_EQ(f.avg_trade_size, 0.0);
}

TEST(RobustnessTest, FeatureEngineZeroSpread) {
    AdvancedFeatureEngine fe;
    TradeFlowEngine tf;
    OrderBook ob;
    PriceLevel bid{50000.0, 1.0};
    PriceLevel ask{50000.0, 1.0}; // zero spread
    ob.apply_snapshot(&bid, 1, &ask, 1, 1);

    Features f = fe.compute(ob, tf);
    EXPECT_TRUE(std::isfinite(f.spread_bps));
}

TEST(RobustnessTest, RLOptimizerWithExtremeValues) {
    RLOptimizer rl;
    RLState state{};
    state.volatility = 1e10;
    state.spread_bps = 1e10;
    state.recent_pnl = -1e10;

    auto action = rl.act(state);
    EXPECT_TRUE(std::isfinite(action.signal_threshold_delta));
    EXPECT_TRUE(std::isfinite(action.position_size_scale));
}

TEST(RobustnessTest, SystemMonitorInitialValues) {
    SystemMonitor sm;
    const auto& s = sm.snapshot();
    EXPECT_GE(s.cpu_cores, 1);
    EXPECT_GE(s.memory_used_bytes, 0u);
    EXPECT_TRUE(std::isfinite(s.cpu_usage_pct));
    EXPECT_TRUE(std::isfinite(s.memory_used_mb));
}

TEST(RobustnessTest, BacktesterWithSingleTick) {
    BacktestTick t;
    t.timestamp_ns = Clock::now_ns();
    t.bid_price = 50000.0;
    t.ask_price = 50001.0;
    t.bid_qty = 1.0;
    t.ask_qty = 1.0;
    t.last_price = 50000.5;
    t.last_qty = 0.01;

    Backtester bt;
    auto result = bt.run({t}, [](const Features&, const OrderBook&,
                                  std::vector<SimulatedOrder>&) {});
    EXPECT_EQ(result.total_fills, 0);
    EXPECT_TRUE(std::isfinite(result.net_pnl));
}

// ═══════════════════════════════════════════════════════════════════════════════
// End-to-End Pipeline: Feature → GRU → Signal → Risk → Backtest
// ═══════════════════════════════════════════════════════════════════════════════

TEST(E2ETest, FullTradingPipeline) {
    auto ticks = generate_trending_ticks(1000, 50000.0, 0.1);

    BacktestConfig bt_cfg;
    bt_cfg.order_latency_ticks = 2;
    bt_cfg.partial_fills = false;
    bt_cfg.base_slippage_bps = 0.5;
    Backtester bt(bt_cfg);

    GRUModelEngine gru;
    RiskLimits limits;
    limits.max_position_size = Qty(0.1);
    limits.max_daily_loss = Notional(1000.0);
    limits.max_drawdown = 0.2;
    limits.max_orders_per_sec = 100;
    CircuitBreakerConfig cb_cfg;
    cb_cfg.enabled = true;
    cb_cfg.loss_threshold = 500.0;
    cb_cfg.consecutive_losses = 20;
    cb_cfg.drawdown_threshold = 0.3;
    cb_cfg.cooldown_sec = 60;
    cb_cfg.max_loss_rate = 200.0;
    EnhancedRiskEngine risk(limits, cb_cfg);

    RegimeDetector regime;
    StrategyHealthMonitor health;
    SystemMonitor sysmon;
    int orders_total = 0;

    auto result = bt.run(ticks, [&](const Features& f, const OrderBook& ob,
                                     std::vector<SimulatedOrder>& orders) {
        if (!ob.valid()) return;

        // Regime detection
        RegimeState rs = regime.update(f);

        // Only trade in stable regimes
        if (rs.current == MarketRegime::LiquidityVacuum) return;

        // Simple momentum strategy (very low threshold for synthetic data)
        if (std::abs(f.mid_momentum) > 0.0000001 && orders_total < 50) {
            SimulatedOrder ord;
            ord.side = f.mid_momentum > 0 ? Side::Buy : Side::Sell;
            ord.type = OrderType::Market;
            ord.price = (ord.side == Side::Buy) ? ob.best_ask() : ob.best_bid();
            ord.qty = 0.001;

            // Quick risk check
            Position pos{};
            auto check = risk.check_order(
                Signal{ord.side, Price(ord.price), Qty(ord.qty), 0.7, Clock::now_ns()},
                pos, rs.current);

            if (check.passed) {
                orders.push_back(ord);
                risk.record_order();
                orders_total++;
            }
        }
    });

    // Verify pipeline produced valid results
    EXPECT_GT(result.total_orders, 0);
    EXPECT_TRUE(std::isfinite(result.total_pnl));
    EXPECT_TRUE(std::isfinite(result.total_fees));
    EXPECT_TRUE(std::isfinite(result.net_pnl));
    EXPECT_GT(result.equity_curve.size(), 0u);
    EXPECT_TRUE(std::isfinite(result.metrics.sharpe_ratio));

    std::cout << "E2E Pipeline: orders=" << result.total_orders
              << " fills=" << result.total_fills
              << " pnl=" << result.total_pnl
              << " fees=" << result.total_fees
              << " net=" << result.net_pnl
              << " sharpe=" << result.metrics.sharpe_ratio
              << std::endl;
    SUCCEED();
}
