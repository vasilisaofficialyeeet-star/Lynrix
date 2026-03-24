#include <gtest/gtest.h>
#include "orderbook/orderbook.h"
#include "config/types.h"

#include <cmath>
#include <random>
#include <chrono>

using namespace bybit;

class OrderBookTest : public ::testing::Test {
protected:
    OrderBook ob;

    void SetUp() override {
        ob.reset();
    }

    void apply_basic_snapshot() {
        PriceLevel bids[5] = {
            {100.0, 1.0}, {99.5, 2.0}, {99.0, 3.0}, {98.5, 1.5}, {98.0, 0.5}
        };
        PriceLevel asks[5] = {
            {100.5, 1.0}, {101.0, 2.0}, {101.5, 3.0}, {102.0, 1.5}, {102.5, 0.5}
        };
        ob.apply_snapshot(bids, 5, asks, 5, 1);
    }
};

// ─── Snapshot Tests ─────────────────────────────────────────────────────────

TEST_F(OrderBookTest, EmptyBookDefaults) {
    EXPECT_DOUBLE_EQ(ob.best_bid(), 0.0);
    EXPECT_DOUBLE_EQ(ob.best_ask(), 0.0);
    EXPECT_DOUBLE_EQ(ob.spread(), 0.0);
    EXPECT_DOUBLE_EQ(ob.mid_price(), 0.0);
    EXPECT_FALSE(ob.valid());
}

TEST_F(OrderBookTest, SnapshotApply) {
    apply_basic_snapshot();

    EXPECT_TRUE(ob.valid());
    EXPECT_EQ(ob.bid_count(), 5u);
    EXPECT_EQ(ob.ask_count(), 5u);
    EXPECT_DOUBLE_EQ(ob.best_bid(), 100.0);
    EXPECT_DOUBLE_EQ(ob.best_ask(), 100.5);
    EXPECT_EQ(ob.seq_id(), 1u);
}

TEST_F(OrderBookTest, SnapshotSortsBids) {
    // Provide bids in unsorted order
    PriceLevel bids[3] = {{98.0, 1.0}, {100.0, 2.0}, {99.0, 3.0}};
    PriceLevel asks[1] = {{101.0, 1.0}};
    ob.apply_snapshot(bids, 3, asks, 1, 1);

    EXPECT_DOUBLE_EQ(ob.best_bid(), 100.0);
    EXPECT_DOUBLE_EQ(ob.bids()[1].price, 99.0);
    EXPECT_DOUBLE_EQ(ob.bids()[2].price, 98.0);
}

TEST_F(OrderBookTest, SnapshotSortsAsks) {
    PriceLevel bids[1] = {{99.0, 1.0}};
    PriceLevel asks[3] = {{103.0, 1.0}, {101.0, 2.0}, {102.0, 3.0}};
    ob.apply_snapshot(bids, 1, asks, 3, 1);

    EXPECT_DOUBLE_EQ(ob.best_ask(), 101.0);
    EXPECT_DOUBLE_EQ(ob.asks()[1].price, 102.0);
    EXPECT_DOUBLE_EQ(ob.asks()[2].price, 103.0);
}

// ─── Spread / Mid / Microprice ──────────────────────────────────────────────

TEST_F(OrderBookTest, Spread) {
    apply_basic_snapshot();
    EXPECT_DOUBLE_EQ(ob.spread(), 0.5);
}

TEST_F(OrderBookTest, MidPrice) {
    apply_basic_snapshot();
    EXPECT_DOUBLE_EQ(ob.mid_price(), 100.25);
}

TEST_F(OrderBookTest, Microprice) {
    apply_basic_snapshot();
    // microprice = (bid * ask_qty + ask * bid_qty) / (bid_qty + ask_qty)
    // = (100.0 * 1.0 + 100.5 * 1.0) / (1.0 + 1.0) = 100.25
    EXPECT_DOUBLE_EQ(ob.microprice(), 100.25);
}

TEST_F(OrderBookTest, MicropriceAsymmetric) {
    PriceLevel bids[1] = {{100.0, 3.0}};
    PriceLevel asks[1] = {{101.0, 1.0}};
    ob.apply_snapshot(bids, 1, asks, 1, 1);

    // microprice = (100 * 1 + 101 * 3) / (3 + 1) = 403/4 = 100.75
    EXPECT_NEAR(ob.microprice(), 100.75, 1e-10);
}

// ─── Imbalance ──────────────────────────────────────────────────────────────

TEST_F(OrderBookTest, ImbalanceSymmetric) {
    PriceLevel bids[2] = {{100.0, 5.0}, {99.0, 5.0}};
    PriceLevel asks[2] = {{101.0, 5.0}, {102.0, 5.0}};
    ob.apply_snapshot(bids, 2, asks, 2, 1);

    EXPECT_NEAR(ob.imbalance(2), 0.0, 1e-10);
}

TEST_F(OrderBookTest, ImbalanceBidHeavy) {
    PriceLevel bids[2] = {{100.0, 10.0}, {99.0, 10.0}};
    PriceLevel asks[2] = {{101.0, 1.0}, {102.0, 1.0}};
    ob.apply_snapshot(bids, 2, asks, 2, 1);

    // (20 - 2) / (20 + 2) = 18/22
    EXPECT_NEAR(ob.imbalance(2), 18.0 / 22.0, 1e-10);
}

TEST_F(OrderBookTest, ImbalanceDepthLimit) {
    // Asymmetric book: bids have increasing qty, asks have constant qty
    PriceLevel bids[5] = {
        {100.0, 1.0}, {99.5, 2.0}, {99.0, 4.0}, {98.5, 8.0}, {98.0, 16.0}
    };
    PriceLevel asks[5] = {
        {100.5, 1.0}, {101.0, 1.0}, {101.5, 1.0}, {102.0, 1.0}, {102.5, 1.0}
    };
    ob.apply_snapshot(bids, 5, asks, 5, 1);

    double imb1 = ob.imbalance(1);
    double imb5 = ob.imbalance(5);
    // imb1 = (1-1)/(1+1) = 0, imb5 = (31-5)/(31+5) > 0
    EXPECT_DOUBLE_EQ(imb1, 0.0);
    EXPECT_GT(imb5, 0.0);
    EXPECT_NE(imb1, imb5);
}

// ─── Delta Updates ──────────────────────────────────────────────────────────

TEST_F(OrderBookTest, DeltaUpdateQty) {
    apply_basic_snapshot();

    PriceLevel bid_update[1] = {{100.0, 5.0}};
    PriceLevel no_asks[0] = {};
    EXPECT_TRUE(ob.apply_delta(bid_update, 1, no_asks, 0, 2));

    EXPECT_DOUBLE_EQ(ob.best_bid_qty(), 5.0);
    EXPECT_DOUBLE_EQ(ob.best_bid(), 100.0);
}

TEST_F(OrderBookTest, DeltaRemoveLevel) {
    apply_basic_snapshot();

    // Remove best bid (qty = 0)
    PriceLevel bid_remove[1] = {{100.0, 0.0}};
    PriceLevel no_asks[0] = {};
    EXPECT_TRUE(ob.apply_delta(bid_remove, 1, no_asks, 0, 2));

    EXPECT_DOUBLE_EQ(ob.best_bid(), 99.5);
    EXPECT_EQ(ob.bid_count(), 4u);
}

TEST_F(OrderBookTest, DeltaAddNewLevel) {
    apply_basic_snapshot();

    // Add new best bid
    PriceLevel new_bid[1] = {{100.2, 2.0}};
    PriceLevel no_asks[0] = {};
    EXPECT_TRUE(ob.apply_delta(new_bid, 1, no_asks, 0, 2));

    EXPECT_DOUBLE_EQ(ob.best_bid(), 100.2);
    EXPECT_EQ(ob.bid_count(), 6u);
}

TEST_F(OrderBookTest, DeltaRejectsOldSeq) {
    apply_basic_snapshot(); // seq = 1

    PriceLevel bid[1] = {{100.0, 5.0}};
    PriceLevel no_asks[0] = {};
    // Seq 0 < 1 — should reject
    EXPECT_FALSE(ob.apply_delta(bid, 1, no_asks, 0, 0));
}

TEST_F(OrderBookTest, DeltaRejectsWithoutSnapshot) {
    // No snapshot applied
    PriceLevel bid[1] = {{100.0, 5.0}};
    PriceLevel no_asks[0] = {};
    EXPECT_FALSE(ob.apply_delta(bid, 1, no_asks, 0, 1));
}

// ─── Cancel Spike / Spread Change ───────────────────────────────────────────

TEST_F(OrderBookTest, CancelSpikeDetection) {
    apply_basic_snapshot();
    // best_bid_qty = 1.0

    // Update: drop best bid qty from 1.0 to 0.1
    PriceLevel bid_update[1] = {{100.0, 0.1}};
    PriceLevel no_asks[0] = {};
    ob.apply_delta(bid_update, 1, no_asks, 0, 2);

    double spike = ob.cancel_spike();
    EXPECT_GT(spike, 0.8); // (1.0 - 0.1) / 1.0 = 0.9
}

TEST_F(OrderBookTest, SpreadChangeRate) {
    apply_basic_snapshot();
    double initial_spread = ob.spread(); // 0.5

    // Remove best bid → spread widens
    PriceLevel bid_remove[1] = {{100.0, 0.0}};
    PriceLevel no_asks[0] = {};
    ob.apply_delta(bid_remove, 1, no_asks, 0, 2);

    double new_spread = ob.spread(); // 100.5 - 99.5 = 1.0
    double rate = ob.spread_change_rate();
    EXPECT_NEAR(rate, (new_spread - initial_spread) / initial_spread, 1e-10);
}

// ─── Liquidity Slope ────────────────────────────────────────────────────────

TEST_F(OrderBookTest, LiquiditySlopePositive) {
    apply_basic_snapshot();
    double slope = ob.liquidity_slope(5);
    EXPECT_GT(slope, 0.0);
}

// ─── Stress Test ────────────────────────────────────────────────────────────

TEST_F(OrderBookTest, Full200LevelSnapshot) {
    PriceLevel bids[200];
    PriceLevel asks[200];

    for (int i = 0; i < 200; ++i) {
        bids[i] = {50000.0 - i * 0.5, static_cast<double>(i + 1) * 0.01};
        asks[i] = {50000.5 + i * 0.5, static_cast<double>(i + 1) * 0.01};
    }

    ob.apply_snapshot(bids, 200, asks, 200, 1);
    EXPECT_TRUE(ob.valid());
    EXPECT_EQ(ob.bid_count(), 200u);
    EXPECT_EQ(ob.ask_count(), 200u);
    EXPECT_DOUBLE_EQ(ob.best_bid(), 50000.0);
    EXPECT_DOUBLE_EQ(ob.best_ask(), 50000.5);
}

TEST_F(OrderBookTest, HighFrequencyDeltaUpdates) {
    apply_basic_snapshot();

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_dist(95.0, 105.0);
    std::uniform_real_distribution<double> qty_dist(0.0, 10.0);

    int success = 0;
    for (int i = 0; i < 10000; ++i) {
        PriceLevel bid[1] = {{price_dist(rng), qty_dist(rng)}};
        PriceLevel ask[1] = {{price_dist(rng) + 0.5, qty_dist(rng)}};
        if (ob.apply_delta(bid, 1, ask, 1, ob.seq_id() + 1)) {
            ++success;
        }
    }

    EXPECT_EQ(success, 10000);
    EXPECT_TRUE(ob.valid());
}

TEST_F(OrderBookTest, UpdateLatency) {
    apply_basic_snapshot();

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100000; ++i) {
        PriceLevel bid[1] = {{99.5, static_cast<double>(i % 100)}};
        PriceLevel no_asks[0] = {};
        ob.apply_delta(bid, 1, no_asks, 0, ob.seq_id() + 1);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double avg_ns = static_cast<double>(ns) / 100000.0;

    // Should be well under 10µs per update
    EXPECT_LT(avg_ns, 10000.0);
    std::cout << "Average OB update latency: " << avg_ns << " ns" << std::endl;
}

// ─── Reset ──────────────────────────────────────────────────────────────────

TEST_F(OrderBookTest, ResetClearsState) {
    apply_basic_snapshot();
    EXPECT_TRUE(ob.valid());

    ob.reset();
    EXPECT_FALSE(ob.valid());
    EXPECT_EQ(ob.bid_count(), 0u);
    EXPECT_EQ(ob.ask_count(), 0u);
    EXPECT_DOUBLE_EQ(ob.best_bid(), 0.0);
}
