// ─── Stage 4: Hot-Path Cleanup and Contract Enforcement Tests ───────────────
// Tests for:
//   1. DeferredWorkQueue push/pop/overflow
//   2. DeferredWorkQueue drop-on-full with overflow counter
//   3. deferred_log helper correctness
//   4. HotPathCounters trivial copyability and reset
//   5. TickColdWork trivial copyability and clear
//   6. LoadShedState shed rate computation
//   7. ScopedStageTimer budget violation detection
//   8. PipelineStage budgets are defined for all stages
//   9. TickLatency hot_path_ns() computation
//  10. DeferredWork layout (exactly 64 bytes)
//  11. Path classification macros exist (compile-time)
//  12. BYBIT_CACHELINE constant correctness

#include <gtest/gtest.h>
#include "../src/core/hot_path.h"
#include "../src/core/memory_policy.h"

using namespace bybit;

// ═════════════════════════════════════════════════════════════════════════════
// DeferredWorkQueue Tests
// ═════════════════════════════════════════════════════════════════════════════

class DeferredWorkQueueTest : public ::testing::Test {
protected:
    DeferredWorkQueue q;
};

TEST_F(DeferredWorkQueueTest, InitiallyEmpty) {
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(q.overflow_count(), 0u);
}

TEST_F(DeferredWorkQueueTest, PushPopSingle) {
    DeferredWork w{};
    w.timestamp_ns = 12345;
    w.type = static_cast<uint8_t>(DeferredWorkType::LogInfo);
    w.values[0] = 1.0;
    std::strncpy(w.tag, "test", sizeof(w.tag));

    EXPECT_TRUE(q.push(w));
    EXPECT_EQ(q.size(), 1u);

    DeferredWork out{};
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out.timestamp_ns, 12345u);
    EXPECT_EQ(out.type, static_cast<uint8_t>(DeferredWorkType::LogInfo));
    EXPECT_DOUBLE_EQ(out.values[0], 1.0);
    EXPECT_STREQ(out.tag, "test");

    EXPECT_TRUE(q.empty());
}

TEST_F(DeferredWorkQueueTest, PushPopMultiple) {
    for (size_t i = 0; i < 10; ++i) {
        DeferredWork w{};
        w.timestamp_ns = i;
        EXPECT_TRUE(q.push(w));
    }
    EXPECT_EQ(q.size(), 10u);

    for (size_t i = 0; i < 10; ++i) {
        DeferredWork out{};
        EXPECT_TRUE(q.pop(out));
        EXPECT_EQ(out.timestamp_ns, i);
    }
    EXPECT_TRUE(q.empty());
}

TEST_F(DeferredWorkQueueTest, FillToCapacity) {
    for (size_t i = 0; i < DeferredWorkQueue::CAPACITY; ++i) {
        DeferredWork w{};
        w.timestamp_ns = i;
        EXPECT_TRUE(q.push(w));
    }
    EXPECT_EQ(q.size(), DeferredWorkQueue::CAPACITY);
    EXPECT_EQ(q.overflow_count(), 0u);
}

TEST_F(DeferredWorkQueueTest, OverflowDropsAndCounts) {
    // Fill completely
    for (size_t i = 0; i < DeferredWorkQueue::CAPACITY; ++i) {
        DeferredWork w{};
        w.timestamp_ns = i;
        q.push(w);
    }

    // Overflow attempts
    DeferredWork extra{};
    extra.timestamp_ns = 9999;
    EXPECT_FALSE(q.push(extra));
    EXPECT_FALSE(q.push(extra));
    EXPECT_FALSE(q.push(extra));

    EXPECT_EQ(q.overflow_count(), 3u);
    EXPECT_EQ(q.size(), DeferredWorkQueue::CAPACITY);

    // Drain still works
    DeferredWork out{};
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out.timestamp_ns, 0u); // first item pushed
}

TEST_F(DeferredWorkQueueTest, ClearResetsQueue) {
    DeferredWork w{};
    q.push(w);
    q.push(w);
    EXPECT_EQ(q.size(), 2u);

    q.clear();
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST_F(DeferredWorkQueueTest, ResetOverflow) {
    // Fill and overflow
    for (size_t i = 0; i < DeferredWorkQueue::CAPACITY + 5; ++i) {
        DeferredWork w{};
        q.push(w);
    }
    EXPECT_EQ(q.overflow_count(), 5u);
    q.reset_overflow();
    EXPECT_EQ(q.overflow_count(), 0u);
}

TEST_F(DeferredWorkQueueTest, PopOnEmptyReturnsFalse) {
    DeferredWork out{};
    EXPECT_FALSE(q.pop(out));
}

TEST_F(DeferredWorkQueueTest, WrapAround) {
    // Push/pop multiple rounds to exercise ring wrap-around
    for (int round = 0; round < 5; ++round) {
        for (size_t i = 0; i < DeferredWorkQueue::CAPACITY / 2; ++i) {
            DeferredWork w{};
            w.timestamp_ns = round * 1000 + i;
            EXPECT_TRUE(q.push(w));
        }
        for (size_t i = 0; i < DeferredWorkQueue::CAPACITY / 2; ++i) {
            DeferredWork out{};
            EXPECT_TRUE(q.pop(out));
            EXPECT_EQ(out.timestamp_ns, static_cast<uint64_t>(round * 1000 + i));
        }
        EXPECT_TRUE(q.empty());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// deferred_log Helper Tests
// ═════════════════════════════════════════════════════════════════════════════

TEST(DeferredLogTest, EnqueuesCorrectly) {
    DeferredWorkQueue q;
    bool ok = deferred_log(q, DeferredWorkType::LogWarn, "LATENCY", 42.0, 99.0);
    EXPECT_TRUE(ok);
    EXPECT_EQ(q.size(), 1u);

    DeferredWork out{};
    q.pop(out);
    EXPECT_EQ(out.type, static_cast<uint8_t>(DeferredWorkType::LogWarn));
    EXPECT_STREQ(out.tag, "LATENCY");
    EXPECT_DOUBLE_EQ(out.values[0], 42.0);
    EXPECT_DOUBLE_EQ(out.values[1], 99.0);
    EXPECT_GT(out.timestamp_ns, 0u);
}

TEST(DeferredLogTest, NullTagSafe) {
    DeferredWorkQueue q;
    bool ok = deferred_log(q, DeferredWorkType::LogDebug, nullptr, 1.0);
    EXPECT_TRUE(ok);

    DeferredWork out{};
    q.pop(out);
    EXPECT_EQ(out.tag[0], '\0');
}

TEST(DeferredLogTest, TagTruncatedTo12Chars) {
    DeferredWorkQueue q;
    deferred_log(q, DeferredWorkType::LogInfo, "ABCDEFGHIJKLMNOP"); // 16 chars

    DeferredWork out{};
    q.pop(out);
    // Should be truncated to 12 chars + null
    EXPECT_EQ(std::strlen(out.tag), 12u);
    EXPECT_EQ(out.tag[12], '\0');
}

// ═════════════════════════════════════════════════════════════════════════════
// HotPathCounters Tests
// ═════════════════════════════════════════════════════════════════════════════

TEST(HotPathCountersTest, TrivialCopyable) {
    static_assert(std::is_trivially_copyable_v<HotPathCounters>);
}

TEST(HotPathCountersTest, CacheLineAligned) {
    static_assert(alignof(HotPathCounters) == BYBIT_CACHELINE);
}

TEST(HotPathCountersTest, ResetZerosAll) {
    HotPathCounters c;
    c.ticks_total = 100;
    c.signals_generated = 5;
    c.budget_exceeded_count = 2;
    c.cold_shed_count = 1;
    c.deferred_overflow = 3;
    c.features_computed = 100;
    c.models_inferred = 80;
    c.orders_dispatched = 5;

    c.reset();

    EXPECT_EQ(c.ticks_total, 0u);
    EXPECT_EQ(c.signals_generated, 0u);
    EXPECT_EQ(c.budget_exceeded_count, 0u);
    EXPECT_EQ(c.cold_shed_count, 0u);
    EXPECT_EQ(c.deferred_overflow, 0u);
    EXPECT_EQ(c.features_computed, 0u);
    EXPECT_EQ(c.models_inferred, 0u);
    EXPECT_EQ(c.orders_dispatched, 0u);
}

TEST(HotPathCountersTest, IncrementWorks) {
    HotPathCounters c{};
    ++c.ticks_total;
    ++c.ticks_total;
    ++c.signals_generated;
    EXPECT_EQ(c.ticks_total, 2u);
    EXPECT_EQ(c.signals_generated, 1u);
}

// ═════════════════════════════════════════════════════════════════════════════
// TickColdWork Tests
// ═════════════════════════════════════════════════════════════════════════════

TEST(TickColdWorkTest, TrivialCopyable) {
    static_assert(std::is_trivially_copyable_v<TickColdWork>);
}

TEST(TickColdWorkTest, ClearResetsAll) {
    TickColdWork w;
    w.log_features = true;
    w.log_signal = true;
    w.regime_changed = true;
    w.feat_latency_ns = 5000;
    w.signal_price = 50000.0;
    w.regime_confidence = 0.95;

    w.clear();

    EXPECT_FALSE(w.log_features);
    EXPECT_FALSE(w.log_signal);
    EXPECT_FALSE(w.regime_changed);
    EXPECT_EQ(w.feat_latency_ns, 0u);
    EXPECT_DOUBLE_EQ(w.signal_price, 0.0);
    EXPECT_DOUBLE_EQ(w.regime_confidence, 0.0);
}

// ═════════════════════════════════════════════════════════════════════════════
// LoadShedState Tests
// ═════════════════════════════════════════════════════════════════════════════

TEST(LoadShedStateTest, InitialState) {
    LoadShedState ls;
    EXPECT_EQ(ls.shed_count, 0u);
    EXPECT_EQ(ls.total_ticks, 0u);
    EXPECT_DOUBLE_EQ(ls.shed_rate_pct(), 0.0);
}

TEST(LoadShedStateTest, ShedRateComputation) {
    LoadShedState ls;

    // 10 normal ticks
    for (int i = 0; i < 10; ++i) {
        ls.record_tick(false, i);
    }
    EXPECT_EQ(ls.total_ticks, 10u);
    EXPECT_EQ(ls.shed_count, 0u);
    EXPECT_DOUBLE_EQ(ls.shed_rate_pct(), 0.0);

    // 10 shed ticks
    for (int i = 10; i < 20; ++i) {
        ls.record_tick(true, i);
    }
    EXPECT_EQ(ls.total_ticks, 20u);
    EXPECT_EQ(ls.shed_count, 10u);
    EXPECT_DOUBLE_EQ(ls.shed_rate_pct(), 50.0);
    EXPECT_EQ(ls.last_shed_tick, 19u);
}

// ═════════════════════════════════════════════════════════════════════════════
// ScopedStageTimer Tests
// ═════════════════════════════════════════════════════════════════════════════

TEST(ScopedStageTimerTest, RecordsNonZeroElapsed) {
    uint64_t elapsed = 0;
    {
        ScopedStageTimer _t(PipelineStage::FeatureCompute, elapsed);
        // Do a tiny amount of work
        volatile int x = 0;
        for (int i = 0; i < 100; ++i) x += i;
    }
    // Should have recorded some time (at least a few ns)
    EXPECT_GT(elapsed, 0u);
}

TEST(ScopedStageTimerTest, BudgetViolationCounterAccessible) {
    // Just verify API exists and is accessible
    uint64_t v = ScopedStageTimer::budget_violations();
    EXPECT_GE(v, 0u); // may be > 0 from other tests
    ScopedStageTimer::reset_budget_violations();
    EXPECT_EQ(ScopedStageTimer::budget_violations(), 0u);
}

// ═════════════════════════════════════════════════════════════════════════════
// PipelineStage Budget Coverage
// ═════════════════════════════════════════════════════════════════════════════

TEST(PipelineStageBudgets, AllStagesHaveBudgets) {
    // Verify STAGE_BUDGET_NS has entries for all stages
    constexpr size_t count = static_cast<size_t>(PipelineStage::COUNT);
    EXPECT_EQ(STAGE_BUDGET_NS.size(), count);

    // Hot stages (0-6) must have non-zero budgets
    EXPECT_GT(STAGE_BUDGET_NS[0], 0u); // OBValidate
    EXPECT_GT(STAGE_BUDGET_NS[1], 0u); // FeatureCompute
    EXPECT_GT(STAGE_BUDGET_NS[2], 0u); // RegimeDetect
    EXPECT_GT(STAGE_BUDGET_NS[3], 0u); // MLInference
    EXPECT_GT(STAGE_BUDGET_NS[4], 0u); // SignalGenerate
    EXPECT_GT(STAGE_BUDGET_NS[5], 0u); // RiskCheck
    EXPECT_GT(STAGE_BUDGET_NS[6], 0u); // ExecutionDecide

    // Cold stages may have 0 budget (unbounded)
    EXPECT_EQ(STAGE_BUDGET_NS[9], 0u);  // LogDeferred
    EXPECT_EQ(STAGE_BUDGET_NS[10], 0u); // AnalyticsRL
}

TEST(PipelineStageBudgets, TotalHotBudgetReasonable) {
    EXPECT_EQ(TOTAL_HOT_BUDGET_NS, 100'000u); // 100us hard cap
}

TEST(PipelineStageBudgets, StageNamesComplete) {
    for (size_t i = 0; i < static_cast<size_t>(PipelineStage::COUNT); ++i) {
        const char* name = stage_name(static_cast<PipelineStage>(i));
        EXPECT_NE(name, nullptr);
        EXPECT_STRNE(name, "Unknown");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// TickLatency Tests
// ═════════════════════════════════════════════════════════════════════════════

TEST(TickLatencyTest, HotPathNsSumsCorrectly) {
    TickLatency tl{};
    tl.stage_ns[0] = 50;     // OBValidate
    tl.stage_ns[1] = 5000;   // FeatureCompute
    tl.stage_ns[2] = 500;    // RegimeDetect
    tl.stage_ns[3] = 50000;  // MLInference
    tl.stage_ns[4] = 500;    // SignalGenerate
    tl.stage_ns[5] = 500;    // RiskCheck
    tl.stage_ns[6] = 2000;   // ExecutionDecide
    tl.stage_ns[7] = 0;      // OrderSubmit (not counted in hot_path_ns for stages 0-8)
    tl.stage_ns[8] = 100;    // MarkToMarket
    tl.stage_ns[11] = 200;   // UIPublish

    uint64_t hot = tl.hot_path_ns();
    // hot_path_ns sums stages 0-8 + 11
    uint64_t expected = 50 + 5000 + 500 + 50000 + 500 + 500 + 2000 + 0 + 100 + 200;
    EXPECT_EQ(hot, expected);
}

TEST(TickLatencyTest, CacheLineAligned) {
    static_assert(alignof(TickLatency) == 128);
}

// ═════════════════════════════════════════════════════════════════════════════
// DeferredWork Layout Tests
// ═════════════════════════════════════════════════════════════════════════════

TEST(DeferredWorkLayout, ExactSize) {
    static_assert(sizeof(DeferredWork) == 64);
    static_assert(alignof(DeferredWork) == 64);
}

TEST(DeferredWorkLayout, TypeEnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(DeferredWorkType::LogDebug), 0);
    EXPECT_EQ(static_cast<uint8_t>(DeferredWorkType::LogInfo), 1);
    EXPECT_EQ(static_cast<uint8_t>(DeferredWorkType::LogWarn), 2);
    EXPECT_EQ(static_cast<uint8_t>(DeferredWorkType::LogError), 3);
    EXPECT_EQ(static_cast<uint8_t>(DeferredWorkType::RecordEvent), 4);
    EXPECT_EQ(static_cast<uint8_t>(DeferredWorkType::RecordSignal), 5);
    EXPECT_EQ(static_cast<uint8_t>(DeferredWorkType::RecordPnL), 6);
    EXPECT_EQ(static_cast<uint8_t>(DeferredWorkType::RecordRegime), 7);
}

// ═════════════════════════════════════════════════════════════════════════════
// Memory Policy / IsolatedCounter Tests
// ═════════════════════════════════════════════════════════════════════════════

TEST(IsolatedCounterTest, CacheLineIsolated) {
    static_assert(sizeof(IsolatedCounter) == BYBIT_CACHELINE);
    static_assert(alignof(IsolatedCounter) == BYBIT_CACHELINE);
}

TEST(IsolatedCounterTest, IncrementAndLoad) {
    IsolatedCounter c;
    EXPECT_EQ(c.load(), 0u);
    c.increment();
    c.increment();
    EXPECT_EQ(c.load(), 2u);
    c.fetch_add(10);
    EXPECT_EQ(c.load(), 12u);
}

TEST(CacheLinePaddedTest, CorrectSize) {
    static_assert(sizeof(CacheLinePadded<uint64_t>) == BYBIT_CACHELINE);
    static_assert(sizeof(CacheLinePadded<double>) == BYBIT_CACHELINE);
}

TEST(CacheLinePaddedTest, NoFalseSharing) {
    // Two adjacent CacheLinePadded should be on different cache lines
    struct TwoCounters {
        CacheLinePadded<std::atomic<uint64_t>> a;
        CacheLinePadded<std::atomic<uint64_t>> b;
    };

    TwoCounters tc;
    uintptr_t addr_a = reinterpret_cast<uintptr_t>(&tc.a);
    uintptr_t addr_b = reinterpret_cast<uintptr_t>(&tc.b);
    EXPECT_GE(addr_b - addr_a, BYBIT_CACHELINE);
}

// ═════════════════════════════════════════════════════════════════════════════
// BYBIT_CACHELINE Constant Test
// ═════════════════════════════════════════════════════════════════════════════

TEST(CacheLineConstant, AppleSilicon128) {
#if defined(__APPLE__) && defined(__aarch64__)
    EXPECT_EQ(BYBIT_CACHELINE, 128u);
#else
    EXPECT_EQ(BYBIT_CACHELINE, 64u);
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
// DeferredWorkQueue Capacity
// ═════════════════════════════════════════════════════════════════════════════

TEST(DeferredWorkQueueCapacity, IsPowerOf2) {
    constexpr size_t cap = DeferredWorkQueue::CAPACITY;
    EXPECT_GT(cap, 0u);
    EXPECT_EQ(cap & (cap - 1), 0u); // power of 2 check
}

TEST(DeferredWorkQueueCapacity, MaskCorrect) {
    EXPECT_EQ(DeferredWorkQueue::MASK, DeferredWorkQueue::CAPACITY - 1);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
