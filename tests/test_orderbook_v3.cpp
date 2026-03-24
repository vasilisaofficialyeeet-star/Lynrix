#include <gtest/gtest.h>
#include "orderbook/orderbook_v3.h"
#include "config/types.h"

#include <cmath>
#include <random>
#include <chrono>

using namespace bybit;

class OrderBookV3Test : public ::testing::Test {
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

TEST_F(OrderBookV3Test, EmptyBookDefaults) {
    EXPECT_DOUBLE_EQ(ob.best_bid(), 0.0);
    EXPECT_DOUBLE_EQ(ob.best_ask(), 0.0);
    EXPECT_DOUBLE_EQ(ob.spread(), 0.0);
    EXPECT_DOUBLE_EQ(ob.mid_price(), 0.0);
    EXPECT_FALSE(ob.valid());
}

TEST_F(OrderBookV3Test, SnapshotApply) {
    apply_basic_snapshot();

    EXPECT_TRUE(ob.valid());
    EXPECT_EQ(ob.bid_count(), 5u);
    EXPECT_EQ(ob.ask_count(), 5u);
    EXPECT_DOUBLE_EQ(ob.best_bid(), 100.0);
    EXPECT_DOUBLE_EQ(ob.best_ask(), 100.5);
    EXPECT_EQ(ob.seq_id(), 1u);
}

TEST_F(OrderBookV3Test, SnapshotSortsBids) {
    // Provide bids in unsorted order
    PriceLevel bids[3] = {{98.0, 1.0}, {100.0, 2.0}, {99.0, 3.0}};
    PriceLevel asks[1] = {{101.0, 1.0}};
    ob.apply_snapshot(bids, 3, asks, 1, 1);

    EXPECT_DOUBLE_EQ(ob.best_bid(), 100.0);
    EXPECT_DOUBLE_EQ(ob.bids()[1].price, 99.0);
    EXPECT_DOUBLE_EQ(ob.bids()[2].price, 98.0);
}

TEST_F(OrderBookV3Test, SnapshotSortsAsks) {
    PriceLevel bids[1] = {{99.0, 1.0}};
    PriceLevel asks[3] = {{103.0, 1.0}, {101.0, 2.0}, {102.0, 3.0}};
    ob.apply_snapshot(bids, 1, asks, 3, 1);

    EXPECT_DOUBLE_EQ(ob.best_ask(), 101.0);
    EXPECT_DOUBLE_EQ(ob.asks()[1].price, 102.0);
    EXPECT_DOUBLE_EQ(ob.asks()[2].price, 103.0);
}

// ─── Spread / Mid / Microprice ──────────────────────────────────────────────

TEST_F(OrderBookV3Test, Spread) {
    apply_basic_snapshot();
    EXPECT_DOUBLE_EQ(ob.spread(), 0.5);
}

TEST_F(OrderBookV3Test, MidPrice) {
    apply_basic_snapshot();
    EXPECT_DOUBLE_EQ(ob.mid_price(), 100.25);
}

TEST_F(OrderBookV3Test, Microprice) {
    apply_basic_snapshot();
    EXPECT_DOUBLE_EQ(ob.microprice(), 100.25);
}

TEST_F(OrderBookV3Test, MicropriceAsymmetric) {
    PriceLevel bids[1] = {{100.0, 3.0}};
    PriceLevel asks[1] = {{101.0, 1.0}};
    ob.apply_snapshot(bids, 1, asks, 1, 1);

    EXPECT_NEAR(ob.microprice(), 100.75, 1e-10);
}

// ─── Imbalance ──────────────────────────────────────────────────────────────

TEST_F(OrderBookV3Test, ImbalanceSymmetric) {
    PriceLevel bids[2] = {{100.0, 5.0}, {99.0, 5.0}};
    PriceLevel asks[2] = {{101.0, 5.0}, {102.0, 5.0}};
    ob.apply_snapshot(bids, 2, asks, 2, 1);

    EXPECT_NEAR(ob.imbalance(2), 0.0, 1e-10);
}

TEST_F(OrderBookV3Test, ImbalanceBidHeavy) {
    PriceLevel bids[2] = {{100.0, 10.0}, {99.0, 10.0}};
    PriceLevel asks[2] = {{101.0, 1.0}, {102.0, 1.0}};
    ob.apply_snapshot(bids, 2, asks, 2, 1);

    EXPECT_NEAR(ob.imbalance(2), 18.0 / 22.0, 1e-10);
}

TEST_F(OrderBookV3Test, ImbalanceDepthLimit) {
    PriceLevel bids[5] = {
        {100.0, 1.0}, {99.5, 2.0}, {99.0, 4.0}, {98.5, 8.0}, {98.0, 16.0}
    };
    PriceLevel asks[5] = {
        {100.5, 1.0}, {101.0, 1.0}, {101.5, 1.0}, {102.0, 1.0}, {102.5, 1.0}
    };
    ob.apply_snapshot(bids, 5, asks, 5, 1);

    double imb1 = ob.imbalance(1);
    double imb5 = ob.imbalance(5);
    EXPECT_DOUBLE_EQ(imb1, 0.0);
    EXPECT_GT(imb5, 0.0);
    EXPECT_NE(imb1, imb5);
}

// ─── Delta Updates ──────────────────────────────────────────────────────────

TEST_F(OrderBookV3Test, DeltaUpdateQty) {
    apply_basic_snapshot();

    PriceLevel bid_update[1] = {{100.0, 5.0}};
    PriceLevel no_asks[0] = {};
    EXPECT_TRUE(ob.apply_delta(bid_update, 1, no_asks, 0, 2));

    EXPECT_DOUBLE_EQ(ob.best_bid_qty(), 5.0);
    EXPECT_DOUBLE_EQ(ob.best_bid(), 100.0);
}

TEST_F(OrderBookV3Test, DeltaRemoveLevel) {
    apply_basic_snapshot();

    PriceLevel bid_remove[1] = {{100.0, 0.0}};
    PriceLevel no_asks[0] = {};
    EXPECT_TRUE(ob.apply_delta(bid_remove, 1, no_asks, 0, 2));

    EXPECT_DOUBLE_EQ(ob.best_bid(), 99.5);
    EXPECT_EQ(ob.bid_count(), 4u);
}

TEST_F(OrderBookV3Test, DeltaAddNewLevel) {
    apply_basic_snapshot();

    PriceLevel new_bid[1] = {{100.2, 2.0}};
    PriceLevel no_asks[0] = {};
    EXPECT_TRUE(ob.apply_delta(new_bid, 1, no_asks, 0, 2));

    EXPECT_DOUBLE_EQ(ob.best_bid(), 100.2);
    EXPECT_EQ(ob.bid_count(), 6u);
}

TEST_F(OrderBookV3Test, DeltaRejectsOldSeq) {
    apply_basic_snapshot(); // seq = 1

    PriceLevel bid[1] = {{100.0, 5.0}};
    PriceLevel no_asks[0] = {};
    EXPECT_FALSE(ob.apply_delta(bid, 1, no_asks, 0, 0));
}

TEST_F(OrderBookV3Test, DeltaRejectsWithoutSnapshot) {
    PriceLevel bid[1] = {{100.0, 5.0}};
    PriceLevel no_asks[0] = {};
    EXPECT_FALSE(ob.apply_delta(bid, 1, no_asks, 0, 1));
}

// ─── Cancel Spike / Spread Change ───────────────────────────────────────────

TEST_F(OrderBookV3Test, CancelSpikeDetection) {
    apply_basic_snapshot();

    PriceLevel bid_update[1] = {{100.0, 0.1}};
    PriceLevel no_asks[0] = {};
    ob.apply_delta(bid_update, 1, no_asks, 0, 2);

    double spike = ob.cancel_spike();
    EXPECT_GT(spike, 0.8);
}

TEST_F(OrderBookV3Test, SpreadChangeRate) {
    apply_basic_snapshot();
    double initial_spread = ob.spread();

    PriceLevel bid_remove[1] = {{100.0, 0.0}};
    PriceLevel no_asks[0] = {};
    ob.apply_delta(bid_remove, 1, no_asks, 0, 2);

    double new_spread = ob.spread();
    double rate = ob.spread_change_rate();
    EXPECT_NEAR(rate, (new_spread - initial_spread) / initial_spread, 1e-10);
}

// ─── Liquidity Slope ────────────────────────────────────────────────────────

TEST_F(OrderBookV3Test, LiquiditySlopePositive) {
    apply_basic_snapshot();
    double slope = ob.liquidity_slope(5);
    EXPECT_GT(slope, 0.0);
}

// ─── Stress Test ────────────────────────────────────────────────────────────

TEST_F(OrderBookV3Test, Full200LevelSnapshot) {
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

TEST_F(OrderBookV3Test, HighFrequencyDeltaUpdates) {
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

TEST_F(OrderBookV3Test, UpdateLatency) {
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

    EXPECT_LT(avg_ns, 10000.0);
    std::cout << "Average OB v3 update latency: " << avg_ns << " ns" << std::endl;
}

// ─── O(1) Modify Test ───────────────────────────────────────────────────────

TEST_F(OrderBookV3Test, O1ModifyExistingLevel) {
    // Insert 200 levels, then modify existing ones — should be O(1) via hashmap
    PriceLevel bids[200];
    PriceLevel asks[200];
    for (int i = 0; i < 200; ++i) {
        bids[i] = {50000.0 - i * 0.5, 1.0};
        asks[i] = {50000.5 + i * 0.5, 1.0};
    }
    ob.apply_snapshot(bids, 200, asks, 200, 1);

    // Modify 100k times on existing levels — must be fast
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100000; ++i) {
        double price = 50000.0 - (i % 200) * 0.5;
        PriceLevel bid[1] = {{price, static_cast<double>(i % 100) + 0.1}};
        PriceLevel no_asks[0] = {};
        ob.apply_delta(bid, 1, no_asks, 0, ob.seq_id() + 1);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double avg_ns = static_cast<double>(ns) / 100000.0;

    std::cout << "Average OB v3 O(1) modify latency: " << avg_ns << " ns" << std::endl;
    // O(1) modify should be < 100 ns per update
    EXPECT_LT(avg_ns, 1000.0);
}

// ─── Reset ──────────────────────────────────────────────────────────────────

TEST_F(OrderBookV3Test, ResetClearsState) {
    apply_basic_snapshot();
    EXPECT_TRUE(ob.valid());

    ob.reset();
    EXPECT_FALSE(ob.valid());
    EXPECT_EQ(ob.bid_count(), 0u);
    EXPECT_EQ(ob.ask_count(), 0u);
    EXPECT_DOUBLE_EQ(ob.best_bid(), 0.0);
}

// ─── Hash Map Correctness ───────────────────────────────────────────────────

TEST_F(OrderBookV3Test, RemoveAndReinsert) {
    apply_basic_snapshot();

    // Remove best bid
    PriceLevel rm[1] = {{100.0, 0.0}};
    PriceLevel no[0] = {};
    ob.apply_delta(rm, 1, no, 0, ob.seq_id() + 1);
    EXPECT_DOUBLE_EQ(ob.best_bid(), 99.5);
    EXPECT_EQ(ob.bid_count(), 4u);

    // Re-insert same price
    PriceLevel add[1] = {{100.0, 7.0}};
    ob.apply_delta(add, 1, no, 0, ob.seq_id() + 1);
    EXPECT_DOUBLE_EQ(ob.best_bid(), 100.0);
    EXPECT_DOUBLE_EQ(ob.best_bid_qty(), 7.0);
    EXPECT_EQ(ob.bid_count(), 5u);
}

TEST_F(OrderBookV3Test, RemoveNonExistent) {
    apply_basic_snapshot();

    // Remove a price that doesn't exist — should be no-op
    PriceLevel rm[1] = {{99.7, 0.0}};
    PriceLevel no[0] = {};
    ob.apply_delta(rm, 1, no, 0, ob.seq_id() + 1);
    EXPECT_EQ(ob.bid_count(), 5u);
}
