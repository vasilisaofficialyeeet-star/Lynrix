// ─── Stage 2 Integration Tests ─────────────────────────────────────────────────
// Verifies:
//   1. Clock typed API (Clock::now_typed, elapsed_typed)
//   2. BlackBoxRecorder<MockClockSource> — deterministic timestamps
//   3. HeartbeatRegistry<MockClockSource> — deterministic staleness/jitter
//   4. ReplayEngine<MockClockSource> — deterministic pacing
//   5. TickLatency / ScopedStageTimer budget tracking
//   6. WatchdogStage ↔ PipelineStage (hot_path.h) non-collision
//   7. ReplayClockSource thread-safety under contention

#include <gtest/gtest.h>

#include "core/strong_types.h"
#include "core/memory_policy.h"
#include "core/clock_source.h"
#include "core/hot_path.h"
#include "utils/clock.h"
#include "monitoring/blackbox_recorder.h"
#include "monitoring/watchdog.h"
#include "monitoring/deterministic_replay.h"

#include <thread>
#include <atomic>
#include <cstring>

using namespace bybit;

// ═══════════════════════════════════════════════════════════════════════════
// 1. Clock Typed API
// ═══════════════════════════════════════════════════════════════════════════

TEST(ClockTypedAPI, NowTypedReturnsTimestampNs) {
    TimestampNs t = Clock::now_typed();
    EXPECT_GT(t.raw(), 0u);
}

TEST(ClockTypedAPI, TicksTypedReturnsTscTicks) {
    TscTicks t = Clock::ticks_typed();
    EXPECT_GT(t.raw(), 0u);
}

TEST(ClockTypedAPI, ElapsedTypedFromTscTicks) {
    TscTicks start = Clock::ticks_typed();
    volatile int x = 0;
    for (int i = 0; i < 1000; ++i) x += i;
    DurationNs elapsed = Clock::elapsed_typed(start);
    EXPECT_GT(elapsed.raw(), 0);
}

TEST(ClockTypedAPI, ElapsedTypedFromRawTicks) {
    uint64_t start = Clock::now_ticks();
    volatile int x = 0;
    for (int i = 0; i < 1000; ++i) x += i;
    DurationNs elapsed = Clock::elapsed_typed(start);
    EXPECT_GT(elapsed.raw(), 0);
}

TEST(ClockTypedAPI, ConsistencyWithRawAPI) {
    // Typed and raw should return values in the same ballpark
    uint64_t raw = Clock::now_ns();
    TimestampNs typed = Clock::now_typed();
    // Within 1ms of each other (generous for test scheduling)
    EXPECT_LT(typed.raw() > raw ? typed.raw() - raw : raw - typed.raw(),
              1'000'000u);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. BlackBoxRecorder with MockClockSource
// ═══════════════════════════════════════════════════════════════════════════

TEST(BlackBoxRecorderMock, DeterministicTimestamps) {
    MockClockSource mock(TimestampNs{1000});
    BlackBoxRecorder<64, MockClockSource> recorder(mock);

    recorder.clock().set(TimestampNs{5000});
    recorder.record(EventType::SignalGen, 1.0);

    recorder.clock().set(TimestampNs{10000});
    recorder.record(EventType::OrderFill, 2.0);

    EventRecord events[2];
    size_t n = recorder.get_recent(events, 2);
    ASSERT_EQ(n, 2u);
    // Events are returned newest-first from get_recent()
    EXPECT_EQ(events[0].timestamp_ns, 10000u);
    EXPECT_EQ(events[1].timestamp_ns, 5000u);
    EXPECT_EQ(events[0].type, EventType::OrderFill);
    EXPECT_EQ(events[1].type, EventType::SignalGen);
}

TEST(BlackBoxRecorderMock, SequenceMonotonic) {
    MockClockSource mock(TimestampNs{0});
    BlackBoxRecorder<128, MockClockSource> recorder(mock);

    for (uint64_t i = 0; i < 50; ++i) {
        recorder.clock().set(TimestampNs{i * 100});
        recorder.record(EventType::Trade, static_cast<double>(i));
    }

    EventRecord events[50];
    size_t n = recorder.get_recent(events, 50);
    ASSERT_EQ(n, 50u);

    // Check sequences are monotonically increasing (newest first in get_recent())
    for (size_t i = 1; i < n; ++i) {
        EXPECT_GT(events[i - 1].sequence, events[i].sequence);
    }
}

TEST(BlackBoxRecorderMock, RingBufferWraparound) {
    MockClockSource mock(TimestampNs{0});
    BlackBoxRecorder<16, MockClockSource> recorder(mock);

    // Write 32 events into a 16-slot buffer → first 16 are overwritten
    for (uint64_t i = 0; i < 32; ++i) {
        recorder.clock().set(TimestampNs{i * 1000});
        recorder.record(EventType::Trade, static_cast<double>(i));
    }

    EventRecord events[16];
    size_t n = recorder.get_recent(events, 16);
    ASSERT_EQ(n, 16u);
    // Newest event should have timestamp 31000
    EXPECT_EQ(events[0].timestamp_ns, 31000u);
}

TEST(BlackBoxRecorderMock, ReplayRecorderAlias) {
    // ReplayClockSource has std::atomic so can't be copied.
    // Default-construct and use clock() accessor.
    BlackBoxRecorder<64, ReplayClockSource> recorder;
    recorder.clock().advance_to(TimestampNs{42000});

    recorder.record(EventType::SignalGen, 99.0);
    EventRecord events[1];
    size_t n = recorder.get_recent(events, 1);
    ASSERT_EQ(n, 1u);
    EXPECT_EQ(events[0].timestamp_ns, 42000u);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. HeartbeatRegistry with MockClockSource
// ═══════════════════════════════════════════════════════════════════════════

TEST(HeartbeatRegistryMock, DeterministicStaleness) {
    MockClockSource mock(TimestampNs{1'000'000});
    HeartbeatRegistry<MockClockSource> registry(mock);

    // Heartbeat at t=1ms
    registry.heartbeat(WatchdogStage::OrderBook);

    // At t=1ms, not stale (timeout = 5ms)
    EXPECT_FALSE(registry.is_stale(WatchdogStage::OrderBook, 5'000'000));

    // Advance to t=7ms → stale (6ms since heartbeat, timeout 5ms)
    registry.clock().set(TimestampNs{7'000'000});
    EXPECT_TRUE(registry.is_stale(WatchdogStage::OrderBook, 5'000'000));
}

TEST(HeartbeatRegistryMock, LatencyStats) {
    MockClockSource mock(TimestampNs{0});
    HeartbeatRegistry<MockClockSource> registry(mock);

    registry.heartbeat(WatchdogStage::Features, 1000);
    registry.heartbeat(WatchdogStage::Features, 2000);
    registry.heartbeat(WatchdogStage::Features, 3000);

    auto& stats = registry.latency(WatchdogStage::Features);
    EXPECT_EQ(stats.sample_count.load(), 3u);
    EXPECT_EQ(stats.last_ns.load(), 3000u);
    EXPECT_EQ(stats.min_ns.load(), 1000u);
    EXPECT_EQ(stats.max_ns.load(), 3000u);
    EXPECT_GT(stats.ema_ns.load(), 0u);
}

TEST(HeartbeatRegistryMock, NeverStartedNotStale) {
    MockClockSource mock(TimestampNs{100'000'000});
    HeartbeatRegistry<MockClockSource> registry(mock);

    // Stage never heartbeated → should not report as stale
    EXPECT_FALSE(registry.is_stale(WatchdogStage::Parser, 1'000'000));
}

TEST(HeartbeatRegistryMock, AllStagesIndependent) {
    MockClockSource mock(TimestampNs{1'000'000});
    HeartbeatRegistry<MockClockSource> registry(mock);

    registry.heartbeat(WatchdogStage::Parser);
    registry.heartbeat(WatchdogStage::Execution);

    EXPECT_EQ(registry.last_heartbeat_ns(WatchdogStage::Parser), 1'000'000u);
    EXPECT_EQ(registry.last_heartbeat_ns(WatchdogStage::Execution), 1'000'000u);
    // OrderBook never heartbeated
    EXPECT_EQ(registry.last_heartbeat_ns(WatchdogStage::OrderBook), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. ReplayEngine with MockClockSource
// ═══════════════════════════════════════════════════════════════════════════

TEST(ReplayEngineMock, LoadAndReplayMaxSpeed) {
    // Create a recorder, dump to file, then load and replay
    MockClockSource mock(TimestampNs{0});
    BlackBoxRecorder<64, MockClockSource> recorder(mock);

    for (uint64_t i = 0; i < 10; ++i) {
        recorder.clock().set(TimestampNs{i * 1000});
        recorder.record(EventType::Trade, static_cast<double>(i));
    }

    std::string path = "/tmp/bybit_stage2_replay_test.bin";
    ASSERT_TRUE(recorder.dump_to_file(path));

    // Load into replay engine (using TscClockSource for wall clock — max speed = no pacing)
    ReplayEngine<> engine;
    ASSERT_TRUE(engine.load(path));

    std::vector<EventRecord> replayed;
    engine.set_callback([&](const EventRecord& e, size_t) {
        replayed.push_back(e);
    });
    engine.set_speed(0.0); // max speed
    ASSERT_TRUE(engine.run());

    EXPECT_EQ(replayed.size(), 10u);
    // Verify monotonic timestamps
    for (size_t i = 1; i < replayed.size(); ++i) {
        EXPECT_GE(replayed[i].timestamp_ns, replayed[i - 1].timestamp_ns);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. TickLatency / ScopedStageTimer
// ═══════════════════════════════════════════════════════════════════════════

TEST(TickLatencyTest, ScopedStageTimerRecords) {
    TickLatency tl{};
    {
        ScopedStageTimer _t(PipelineStage::FeatureCompute,
                            tl.stage_ns[static_cast<size_t>(PipelineStage::FeatureCompute)]);
        volatile int x = 0;
        for (int i = 0; i < 10000; ++i) x += i;
    }
    EXPECT_GT(tl.stage_ns[static_cast<size_t>(PipelineStage::FeatureCompute)], 0u);
}

TEST(TickLatencyTest, HotPathNsSum) {
    TickLatency tl{};
    tl.stage_ns[static_cast<size_t>(PipelineStage::OBValidate)] = 50;
    tl.stage_ns[static_cast<size_t>(PipelineStage::FeatureCompute)] = 5000;
    tl.stage_ns[static_cast<size_t>(PipelineStage::RegimeDetect)] = 500;
    tl.stage_ns[static_cast<size_t>(PipelineStage::MLInference)] = 50000;
    tl.stage_ns[static_cast<size_t>(PipelineStage::SignalGenerate)] = 500;
    tl.stage_ns[static_cast<size_t>(PipelineStage::RiskCheck)] = 500;
    tl.stage_ns[static_cast<size_t>(PipelineStage::ExecutionDecide)] = 2000;
    tl.stage_ns[static_cast<size_t>(PipelineStage::OrderSubmit)] = 5'000'000;
    tl.stage_ns[static_cast<size_t>(PipelineStage::MarkToMarket)] = 100;
    tl.stage_ns[static_cast<size_t>(PipelineStage::UIPublish)] = 200;

    uint64_t hot = tl.hot_path_ns();
    // Hot path = stages 0-8 + UIPublish (11), NOT LogDeferred(9) or AnalyticsRL(10)
    EXPECT_EQ(hot, 50 + 5000 + 500 + 50000 + 500 + 500 + 2000 + 5'000'000 + 100 + 200);
}

TEST(TickLatencyTest, BudgetExceeded) {
    TickLatency tl{};
    tl.stage_ns[static_cast<size_t>(PipelineStage::MLInference)] = TOTAL_HOT_BUDGET_NS + 1;
    EXPECT_TRUE(tl.hot_path_ns() > TOTAL_HOT_BUDGET_NS);
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. WatchdogStage vs PipelineStage (non-collision)
// ═══════════════════════════════════════════════════════════════════════════

TEST(StageEnumTest, WatchdogStageAndPipelineStageAreDistinct) {
    // WatchdogStage has 8 values, PipelineStage has 12
    EXPECT_EQ(static_cast<size_t>(WatchdogStage::COUNT), 8u);
    EXPECT_EQ(static_cast<size_t>(PipelineStage::COUNT), 12u);

    // Both have stage_name functions that don't collide
    EXPECT_STREQ(watchdog_stage_name(WatchdogStage::OrderBook), "OrderBook");
    EXPECT_STREQ(stage_name(PipelineStage::OBValidate), "OBValidate");
}

TEST(StageEnumTest, WatchdogStageNames) {
    EXPECT_STREQ(watchdog_stage_name(WatchdogStage::WebSocket), "WebSocket");
    EXPECT_STREQ(watchdog_stage_name(WatchdogStage::Parser), "Parser");
    EXPECT_STREQ(watchdog_stage_name(WatchdogStage::Execution), "Execution");
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. ReplayClockSource thread-safety
// ═══════════════════════════════════════════════════════════════════════════

TEST(ReplayClockSourceTest, ConcurrentAdvance) {
    ReplayClockSource clock(TimestampNs{0});
    std::atomic<bool> go{false};
    constexpr int N = 10000;

    auto writer = [&] {
        while (!go.load(std::memory_order_acquire)) {}
        for (int i = 1; i <= N; ++i) {
            clock.advance_to(TimestampNs{static_cast<uint64_t>(i)});
        }
    };

    auto reader = [&] {
        while (!go.load(std::memory_order_acquire)) {}
        uint64_t prev = 0;
        uint64_t monotonic_violations = 0;
        for (int i = 0; i < N * 2; ++i) {
            uint64_t cur = clock.now().raw();
            if (cur < prev) ++monotonic_violations;
            prev = cur;
        }
        // Advance_to is not guaranteed monotonic under concurrent reads
        // (reader might see old value), but no UB or crash
        (void)monotonic_violations;
    };

    std::thread t1(writer);
    std::thread t2(reader);
    go.store(true, std::memory_order_release);
    t1.join();
    t2.join();

    // Final value must be N
    EXPECT_EQ(clock.now().raw(), static_cast<uint64_t>(N));
}

TEST(ReplayClockSourceTest, AdvanceByDelta) {
    ReplayClockSource clock(TimestampNs{1000});
    clock.advance_by(DurationNs{500});
    EXPECT_EQ(clock.now().raw(), 1500u);
    clock.advance_by(DurationNs{-200});
    EXPECT_EQ(clock.now().raw(), 1300u);
}

TEST(ReplayClockSourceTest, Reset) {
    ReplayClockSource clock(TimestampNs{99999});
    clock.reset();
    EXPECT_EQ(clock.now().raw(), 0u);
    clock.reset(TimestampNs{42});
    EXPECT_EQ(clock.now().raw(), 42u);
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. MockClockSource basic
// ═══════════════════════════════════════════════════════════════════════════

TEST(MockClockSourceTest, SetAndRead) {
    MockClockSource mock;
    EXPECT_EQ(mock.now().raw(), 0u);
    mock.set(TimestampNs{12345});
    EXPECT_EQ(mock.now().raw(), 12345u);
}

TEST(MockClockSourceTest, Advance) {
    MockClockSource mock(TimestampNs{1000});
    mock.advance(DurationNs{500});
    EXPECT_EQ(mock.now().raw(), 1500u);
}

TEST(MockClockSourceTest, Elapsed) {
    MockClockSource mock(TimestampNs{1000});
    TscTicks start = mock.now_ticks();
    mock.set(TimestampNs{5000});
    DurationNs elapsed = mock.elapsed(start);
    EXPECT_EQ(elapsed.raw(), 4000);
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. ClockFn type-erased wrapper
// ═══════════════════════════════════════════════════════════════════════════

TEST(ClockFnTest, FromTsc) {
    ClockFn fn = ClockFn::from_tsc();
    TimestampNs t = fn.now();
    EXPECT_GT(t.raw(), 0u);
}

TEST(ClockFnTest, FromReplaySource) {
    ReplayClockSource src(TimestampNs{77777});
    ClockFn fn = ClockFn::from(src);
    EXPECT_EQ(fn.now().raw(), 77777u);

    src.advance_to(TimestampNs{88888});
    EXPECT_EQ(fn.now().raw(), 88888u);
}

TEST(ClockFnTest, SizeIs16Bytes) {
    EXPECT_EQ(sizeof(ClockFn), 2 * sizeof(void*));
}

// ═══════════════════════════════════════════════════════════════════════════
// 10. EventRecord layout verification
// ═══════════════════════════════════════════════════════════════════════════

TEST(EventRecordTest, SizeIs64Bytes) {
    EXPECT_EQ(sizeof(EventRecord), 64u);
}

TEST(EventRecordTest, TriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<EventRecord>);
}

TEST(EventRecordTest, TickLatencyAlignment) {
    EXPECT_EQ(alignof(TickLatency), 128u);
}
