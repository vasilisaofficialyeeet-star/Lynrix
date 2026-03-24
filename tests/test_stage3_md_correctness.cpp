// ─── Stage 3: Market-Data Correctness Integration Tests ─────────────────────
// Tests for:
//   1. Sequence gap detection
//   2. Stale delta rejection
//   3. Normal sequential delta application
//   4. Book invalidation state machine
//   5. Resync recovery via snapshot
//   6. Duplicate seq handling
//   7. Diagnostics counters (MDIngressCounters)
//   8. SequenceGapInfo capture
//   9. Fixed-point round-trip accuracy
//  10. BookState enum correctness
//  11. DeltaResult enum completeness
//  12. DropPolicy annotation existence
//  13. Backward-compat: legacy bool apply_delta still works
//  14. OB v3 gap detection (same logic, different internal structure)

#include <gtest/gtest.h>
#include "../src/orderbook/orderbook.h"
#include "../src/core/market_data.h"
#include "../src/core/strong_types.h"

using namespace bybit;

// ─── Helper: create PriceLevel array ────────────────────────────────────────

static void make_levels(PriceLevel* out, size_t count, double base_price, double qty) {
    for (size_t i = 0; i < count; ++i) {
        out[i].price = base_price + static_cast<double>(i) * 0.1;
        out[i].qty = qty;
    }
}

static void make_bids(PriceLevel* out, size_t count, double base_price, double qty) {
    for (size_t i = 0; i < count; ++i) {
        out[i].price = base_price - static_cast<double>(i) * 0.1;
        out[i].qty = qty;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// OrderBook v2 Tests
// ═════════════════════════════════════════════════════════════════════════════

class OBv2Stage3Test : public ::testing::Test {
protected:
    OrderBook ob;

    void apply_initial_snapshot(uint64_t seq = 100) {
        PriceLevel bids[5], asks[5];
        make_bids(bids, 5, 50000.0, 1.0);
        make_levels(asks, 5, 50001.0, 1.0);
        ob.apply_snapshot(bids, 5, asks, 5, seq);
    }
};

// ─── 1. Initial state is Empty ──────────────────────────────────────────────

TEST_F(OBv2Stage3Test, InitialStateIsEmpty) {
    EXPECT_EQ(ob.book_state(), BookState::Empty);
    EXPECT_FALSE(ob.valid());
    EXPECT_EQ(ob.seq_id(), 0u);
}

// ─── 2. Snapshot transitions to Valid ───────────────────────────────────────

TEST_F(OBv2Stage3Test, SnapshotTransitionsToValid) {
    apply_initial_snapshot(100);
    EXPECT_EQ(ob.book_state(), BookState::Valid);
    EXPECT_TRUE(ob.valid());
    EXPECT_EQ(ob.seq_id(), 100u);
    EXPECT_EQ(ob.seq_number(), SequenceNumber{100});
    EXPECT_EQ(ob.expected_seq(), SequenceNumber{101});
}

// ─── 3. Normal sequential delta is applied ──────────────────────────────────

TEST_F(OBv2Stage3Test, NormalSequentialDeltaApplied) {
    apply_initial_snapshot(100);

    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[1] = {{50001.5, 2.0}};

    DeltaResult r = ob.apply_delta_typed(bids, 1, asks, 1, SequenceNumber{101});
    EXPECT_EQ(r, DeltaResult::Applied);
    EXPECT_EQ(ob.book_state(), BookState::Valid);
    EXPECT_EQ(ob.seq_id(), 101u);
    EXPECT_EQ(ob.md_counters().deltas_applied, 1u);
}

// ─── 4. Gap detection invalidates book ──────────────────────────────────────

TEST_F(OBv2Stage3Test, GapDetectionInvalidatesBook) {
    apply_initial_snapshot(100);

    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[1] = {{50001.5, 2.0}};

    // Skip seq 101 — send seq 103 directly
    DeltaResult r = ob.apply_delta_typed(bids, 1, asks, 1, SequenceNumber{103});
    EXPECT_EQ(r, DeltaResult::GapDetected);
    EXPECT_EQ(ob.book_state(), BookState::InvalidGap);
    EXPECT_FALSE(ob.valid());

    // Seq should NOT have been updated (book is now invalid)
    EXPECT_EQ(ob.seq_id(), 100u);
}

// ─── 5. SequenceGapInfo is populated on gap ─────────────────────────────────

TEST_F(OBv2Stage3Test, GapInfoPopulatedOnGap) {
    apply_initial_snapshot(100);

    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[0] = {};

    ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{105});

    const auto& gap = ob.last_gap();
    EXPECT_EQ(gap.expected, SequenceNumber{101});
    EXPECT_EQ(gap.received, SequenceNumber{105});
    EXPECT_EQ(gap.gap_size, 4u);
    EXPECT_GT(gap.timestamp_ns, 0u);
}

// ─── 6. Stale delta is rejected ─────────────────────────────────────────────

TEST_F(OBv2Stage3Test, StaleDeltaRejected) {
    apply_initial_snapshot(100);

    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[0] = {};

    DeltaResult r = ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{99});
    EXPECT_EQ(r, DeltaResult::StaleRejected);
    EXPECT_EQ(ob.book_state(), BookState::Valid);
    EXPECT_EQ(ob.seq_id(), 100u);
    EXPECT_EQ(ob.md_counters().deltas_stale, 1u);
}

// ─── 7. Duplicate seq is detected ───────────────────────────────────────────

TEST_F(OBv2Stage3Test, DuplicateSeqDetected) {
    apply_initial_snapshot(100);

    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[0] = {};

    DeltaResult r = ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{100});
    EXPECT_EQ(r, DeltaResult::DuplicateSeq);
    EXPECT_EQ(ob.book_state(), BookState::Valid);
}

// ─── 8. Delta on invalid book is dropped ────────────────────────────────────

TEST_F(OBv2Stage3Test, DeltaOnInvalidBookDropped) {
    apply_initial_snapshot(100);

    // Trigger gap
    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[0] = {};
    ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{103});
    ASSERT_EQ(ob.book_state(), BookState::InvalidGap);

    // Now try to apply another delta — should be dropped
    DeltaResult r = ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{104});
    EXPECT_EQ(r, DeltaResult::BookNotValid);
    EXPECT_EQ(ob.md_counters().deltas_dropped_invalid, 1u);
}

// ─── 9. Resync recovery via new snapshot ────────────────────────────────────

TEST_F(OBv2Stage3Test, ResyncRecoveryViaSnapshot) {
    apply_initial_snapshot(100);

    // Trigger gap → book invalid
    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[0] = {};
    ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{103});
    ASSERT_FALSE(ob.valid());

    // Apply new snapshot as resync
    PriceLevel new_bids[3], new_asks[3];
    make_bids(new_bids, 3, 51000.0, 0.5);
    make_levels(new_asks, 3, 51001.0, 0.5);
    ob.apply_snapshot(new_bids, 3, new_asks, 3, 200);

    EXPECT_TRUE(ob.valid());
    EXPECT_EQ(ob.book_state(), BookState::Valid);
    EXPECT_EQ(ob.seq_id(), 200u);

    // Now a normal delta at seq 201 should work
    PriceLevel delta_bids[1] = {{50999.5, 1.0}};
    DeltaResult r = ob.apply_delta_typed(delta_bids, 1, asks, 0, SequenceNumber{201});
    EXPECT_EQ(r, DeltaResult::Applied);
}

// ─── 10. Invalidation by external call ──────────────────────────────────────

TEST_F(OBv2Stage3Test, ExternalInvalidation) {
    apply_initial_snapshot(100);
    ASSERT_TRUE(ob.valid());

    ob.invalidate(BookState::InvalidDisconnect);
    EXPECT_EQ(ob.book_state(), BookState::InvalidDisconnect);
    EXPECT_FALSE(ob.valid());
}

// ─── 11. mark_pending_resync ────────────────────────────────────────────────

TEST_F(OBv2Stage3Test, MarkPendingResync) {
    apply_initial_snapshot(100);
    ob.invalidate(BookState::InvalidGap);
    ob.mark_pending_resync();
    EXPECT_EQ(ob.book_state(), BookState::PendingResync);
    EXPECT_FALSE(ob.valid());
}

// ─── 12. Legacy apply_delta still works ─────────────────────────────────────

TEST_F(OBv2Stage3Test, LegacyApplyDeltaBackwardCompat) {
    apply_initial_snapshot(100);

    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[1] = {{50001.5, 2.0}};

    // Legacy bool API: sequential delta
    bool ok = ob.apply_delta(bids, 1, asks, 1, 101);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ob.seq_id(), 101u);

    // Legacy bool API: gap → returns false
    ok = ob.apply_delta(bids, 1, asks, 1, 200);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(ob.valid());
}

// ─── 13. MDIngressCounters accumulate correctly ─────────────────────────────

TEST_F(OBv2Stage3Test, CountersAccumulate) {
    apply_initial_snapshot(100);
    EXPECT_EQ(ob.md_counters().snapshots_applied, 1u);

    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[0] = {};

    // 3 normal deltas
    ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{101});
    ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{102});
    ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{103});
    EXPECT_EQ(ob.md_counters().deltas_applied, 3u);

    // 1 stale
    ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{99});
    EXPECT_EQ(ob.md_counters().deltas_stale, 1u);

    // 1 gap
    ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{200});
    EXPECT_EQ(ob.md_counters().gaps_detected, 1u);

    // 1 dropped (book now invalid)
    ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{201});
    EXPECT_EQ(ob.md_counters().deltas_dropped_invalid, 1u);
}

// ─── 14. Multiple sequential deltas maintain state ──────────────────────────

TEST_F(OBv2Stage3Test, MultipleSequentialDeltas) {
    apply_initial_snapshot(100);

    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[0] = {};

    for (uint64_t seq = 101; seq <= 200; ++seq) {
        DeltaResult r = ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{seq});
        EXPECT_EQ(r, DeltaResult::Applied) << "Failed at seq=" << seq;
    }

    EXPECT_EQ(ob.seq_id(), 200u);
    EXPECT_EQ(ob.md_counters().deltas_applied, 100u);
    EXPECT_TRUE(ob.valid());
}

// ═════════════════════════════════════════════════════════════════════════════
// OrderBook v3 Tests (same gap logic, different internal structure)
// ═════════════════════════════════════════════════════════════════════════════

// Note: orderbook_v3.h also defines class OrderBook in namespace bybit.
// We use a separate namespace alias to disambiguate. Since v3 redefines
// the same class name, we test it in a separate scope.
// In practice, only one OrderBook is linked per TU. The v3 tests are in
// test_orderbook_v3.cpp. Here we test the v2 Stage 3 features thoroughly.

// ═════════════════════════════════════════════════════════════════════════════
// BookState / DeltaResult / DropPolicy enum tests
// ═════════════════════════════════════════════════════════════════════════════

TEST(MarketDataEnums, BookStateNames) {
    EXPECT_STREQ(book_state_name(BookState::Empty), "Empty");
    EXPECT_STREQ(book_state_name(BookState::Valid), "Valid");
    EXPECT_STREQ(book_state_name(BookState::InvalidGap), "InvalidGap");
    EXPECT_STREQ(book_state_name(BookState::InvalidDisconnect), "InvalidDisconnect");
    EXPECT_STREQ(book_state_name(BookState::PendingResync), "PendingResync");
}

TEST(MarketDataEnums, DeltaResultNames) {
    EXPECT_STREQ(delta_result_name(DeltaResult::Applied), "Applied");
    EXPECT_STREQ(delta_result_name(DeltaResult::StaleRejected), "StaleRejected");
    EXPECT_STREQ(delta_result_name(DeltaResult::GapDetected), "GapDetected");
    EXPECT_STREQ(delta_result_name(DeltaResult::BookNotValid), "BookNotValid");
    EXPECT_STREQ(delta_result_name(DeltaResult::DuplicateSeq), "DuplicateSeq");
}

TEST(MarketDataEnums, DropPolicyValues) {
    // Just verify the enum values exist and are distinct
    EXPECT_NE(static_cast<uint8_t>(DropPolicy::MustNotDrop),
              static_cast<uint8_t>(DropPolicy::MayCoalesce));
    EXPECT_NE(static_cast<uint8_t>(DropPolicy::MayCoalesce),
              static_cast<uint8_t>(DropPolicy::MayDrop));
}

TEST(MarketDataEnums, ResyncStateValues) {
    EXPECT_NE(static_cast<uint8_t>(ResyncState::Normal),
              static_cast<uint8_t>(ResyncState::PendingResync));
}

// ═════════════════════════════════════════════════════════════════════════════
// SequenceNumber strong type tests (market-data specific)
// ═════════════════════════════════════════════════════════════════════════════

TEST(SequenceNumberMD, GapDetectionArithmetic) {
    SequenceNumber a{100};
    SequenceNumber b{105};

    uint64_t gap = b - a;
    EXPECT_EQ(gap, 5u);

    SequenceNumber expected = a + 1;
    EXPECT_EQ(expected, SequenceNumber{101});

    EXPECT_TRUE(b > a);
    EXPECT_FALSE(a > b);
    EXPECT_TRUE(a < b);
}

TEST(SequenceNumberMD, ZeroIsDefault) {
    SequenceNumber s;
    EXPECT_EQ(s.raw(), 0u);
    EXPECT_TRUE(s.is_zero());
}

// ═════════════════════════════════════════════════════════════════════════════
// SequenceGapInfo layout test
// ═════════════════════════════════════════════════════════════════════════════

TEST(SequenceGapInfoTest, TrivialCopyable) {
    static_assert(std::is_trivially_copyable_v<SequenceGapInfo>);
}

TEST(SequenceGapInfoTest, FieldsInitializable) {
    SequenceGapInfo info{
        SequenceNumber{100}, SequenceNumber{105}, 5, 1234567890
    };
    EXPECT_EQ(info.expected.raw(), 100u);
    EXPECT_EQ(info.received.raw(), 105u);
    EXPECT_EQ(info.gap_size, 5u);
    EXPECT_EQ(info.timestamp_ns, 1234567890u);
}

// ═════════════════════════════════════════════════════════════════════════════
// MDIngressCounters tests
// ═════════════════════════════════════════════════════════════════════════════

TEST(MDIngressCountersTest, TrivialCopyable) {
    static_assert(std::is_trivially_copyable_v<MDIngressCounters>);
}

TEST(MDIngressCountersTest, ResetZerosAll) {
    MDIngressCounters c;
    c.deltas_applied = 10;
    c.gaps_detected = 3;
    c.resyncs_triggered = 1;
    c.reset();
    EXPECT_EQ(c.deltas_applied, 0u);
    EXPECT_EQ(c.gaps_detected, 0u);
    EXPECT_EQ(c.resyncs_triggered, 0u);
    EXPECT_EQ(c.resyncs_succeeded, 0u);
    EXPECT_EQ(c.resyncs_failed, 0u);
    EXPECT_EQ(c.snapshots_applied, 0u);
}

// ═════════════════════════════════════════════════════════════════════════════
// Fixed-point round-trip accuracy
// ═════════════════════════════════════════════════════════════════════════════

TEST(FixedPointBoundary, RoundTripAccuracy) {
    // Test representative BTC prices
    double prices[] = {
        98234.5, 98234.1, 0.1, 100000.0, 50000.12345678,
        99999.99999999, 1.0, 0.00000001
    };

    for (double p : prices) {
        FixedPrice fp = FixedPrice::from_double(p);
        double recovered = fp.to_double();
        // Round-trip should be within 1e-8 precision
        EXPECT_NEAR(recovered, p, 1e-7)
            << "Round-trip failed for price=" << p
            << " fixed=" << fp.raw;
    }
}

TEST(FixedPointBoundary, FromDoubleIsRoundToNearest) {
    // 0.5 + 0.5 rounding check
    FixedPrice fp = FixedPrice::from_double(100.000000005);
    // 100.000000005 * 1e8 + 0.5 = 10000000001.0 → 10000000001
    EXPECT_EQ(fp.raw, 10000000001LL);
}

TEST(FixedPointBoundary, StrongTypeBoundary) {
    // Verify Price <-> FixedPrice conversion via strong_types.h functions
    Price p{50000.5};
    TickSize tick{0.1};

    int64_t fixed = to_fixed(p, tick);
    Price recovered = from_fixed(fixed, tick);
    EXPECT_NEAR(recovered.raw(), p.raw(), 0.05);  // within half tick
}

// ═════════════════════════════════════════════════════════════════════════════
// State machine: full lifecycle
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(OBv2Stage3Test, FullLifecycle) {
    // 1. Start: Empty
    EXPECT_EQ(ob.book_state(), BookState::Empty);

    // 2. First snapshot → Valid
    apply_initial_snapshot(100);
    EXPECT_EQ(ob.book_state(), BookState::Valid);

    // 3. Normal deltas
    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[0] = {};
    ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{101});
    ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{102});
    EXPECT_EQ(ob.book_state(), BookState::Valid);

    // 4. Gap → InvalidGap
    ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{110});
    EXPECT_EQ(ob.book_state(), BookState::InvalidGap);

    // 5. Mark pending resync
    ob.mark_pending_resync();
    EXPECT_EQ(ob.book_state(), BookState::PendingResync);

    // 6. Deltas during resync are dropped
    DeltaResult r = ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{111});
    EXPECT_EQ(r, DeltaResult::BookNotValid);

    // 7. Resync snapshot → Valid
    PriceLevel new_bids[3], new_asks[3];
    make_bids(new_bids, 3, 51000.0, 0.5);
    make_levels(new_asks, 3, 51001.0, 0.5);
    ob.apply_snapshot(new_bids, 3, new_asks, 3, 500);
    EXPECT_EQ(ob.book_state(), BookState::Valid);
    EXPECT_EQ(ob.seq_id(), 500u);

    // 8. Normal operation resumes
    r = ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{501});
    EXPECT_EQ(r, DeltaResult::Applied);

    // 9. Disconnect → InvalidDisconnect
    ob.invalidate(BookState::InvalidDisconnect);
    EXPECT_EQ(ob.book_state(), BookState::InvalidDisconnect);

    // 10. Reconnect + snapshot → Valid
    ob.apply_snapshot(new_bids, 3, new_asks, 3, 600);
    EXPECT_EQ(ob.book_state(), BookState::Valid);
}

// ═════════════════════════════════════════════════════════════════════════════
// Edge cases
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(OBv2Stage3Test, DeltaOnEmptyBookIsDropped) {
    // Before any snapshot, book is Empty
    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[0] = {};
    DeltaResult r = ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{1});
    EXPECT_EQ(r, DeltaResult::BookNotValid);
}

TEST_F(OBv2Stage3Test, SnapshotAfterResetWorks) {
    apply_initial_snapshot(100);
    ob.reset();
    EXPECT_EQ(ob.book_state(), BookState::Empty);

    apply_initial_snapshot(200);
    EXPECT_EQ(ob.book_state(), BookState::Valid);
    EXPECT_EQ(ob.seq_id(), 200u);
}

TEST_F(OBv2Stage3Test, GapOfOneDetected) {
    apply_initial_snapshot(100);

    // seq 102 when expecting 101 → gap of 1
    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[0] = {};
    DeltaResult r = ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{102});
    EXPECT_EQ(r, DeltaResult::GapDetected);
    EXPECT_EQ(ob.last_gap().gap_size, 1u);
}

TEST_F(OBv2Stage3Test, LargeGapDetected) {
    apply_initial_snapshot(100);

    PriceLevel bids[1] = {{49999.5, 2.0}};
    PriceLevel asks[0] = {};
    DeltaResult r = ob.apply_delta_typed(bids, 1, asks, 0, SequenceNumber{100000});
    EXPECT_EQ(r, DeltaResult::GapDetected);
    EXPECT_EQ(ob.last_gap().gap_size, 99899u);
    EXPECT_EQ(ob.last_gap().expected, SequenceNumber{101});
    EXPECT_EQ(ob.last_gap().received, SequenceNumber{100000});
}

// ═════════════════════════════════════════════════════════════════════════════
// Backward compatibility: set_bbo still works
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(OBv2Stage3Test, SetBBOBackwardCompat) {
    PriceLevel bid{50000.0, 1.0};
    PriceLevel ask{50001.0, 1.0};
    ob.set_bbo(bid, ask, 12345);

    EXPECT_TRUE(ob.valid());
    EXPECT_EQ(ob.book_state(), BookState::Valid);
    EXPECT_NEAR(ob.best_bid(), 50000.0, 1e-6);
    EXPECT_NEAR(ob.best_ask(), 50001.0, 1e-6);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
