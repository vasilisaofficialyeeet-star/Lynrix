#include <gtest/gtest.h>
#include "../src/monitoring/chaos_engine.h"
#include "../src/monitoring/perf_signpost.h"
#include "../src/orderbook/orderbook_v3.h"

#include <cmath>
#include <thread>
#include <chrono>

using namespace bybit;

// ═══════════════════════════════════════════════════════════════════════════
// Chaos Engine Tests
// ═══════════════════════════════════════════════════════════════════════════

class ChaosEngineTest : public ::testing::Test {
protected:
    ChaosEngine chaos{42}; // deterministic seed
};

TEST_F(ChaosEngineTest, DefaultDisabled) {
    EXPECT_FALSE(chaos.is_enabled());
    EXPECT_FALSE(chaos.should_inject(ChaosFault::LatencySpike));
    EXPECT_FALSE(chaos.should_inject(ChaosFault::PacketLoss));
}

TEST_F(ChaosEngineTest, EnableDisableFault) {
    chaos.enable(ChaosFault::LatencySpike, {
        .probability = 1.0,
        .magnitude = 1000,
        .enabled = true
    });
    EXPECT_TRUE(chaos.is_enabled());
    EXPECT_TRUE(chaos.is_fault_enabled(ChaosFault::LatencySpike));
    EXPECT_FALSE(chaos.is_fault_enabled(ChaosFault::PacketLoss));

    chaos.disable(ChaosFault::LatencySpike);
    EXPECT_FALSE(chaos.is_enabled());
    EXPECT_FALSE(chaos.is_fault_enabled(ChaosFault::LatencySpike));
}

TEST_F(ChaosEngineTest, DisableAll) {
    chaos.enable(ChaosFault::LatencySpike, {.probability = 1.0, .enabled = true});
    chaos.enable(ChaosFault::PacketLoss, {.probability = 1.0, .enabled = true});
    EXPECT_TRUE(chaos.is_enabled());

    chaos.disable_all();
    EXPECT_FALSE(chaos.is_enabled());
    EXPECT_FALSE(chaos.is_fault_enabled(ChaosFault::LatencySpike));
    EXPECT_FALSE(chaos.is_fault_enabled(ChaosFault::PacketLoss));
}

TEST_F(ChaosEngineTest, ShouldInjectProbability100) {
    chaos.enable(ChaosFault::LatencySpike, {
        .probability = 1.0,
        .magnitude = 1000,
        .enabled = true
    });

    // 100% probability should always inject
    int injected = 0;
    for (int i = 0; i < 100; ++i) {
        if (chaos.should_inject(ChaosFault::LatencySpike)) ++injected;
    }
    EXPECT_EQ(injected, 100);
}

TEST_F(ChaosEngineTest, ShouldInjectProbability0) {
    chaos.enable(ChaosFault::LatencySpike, {
        .probability = 0.0,
        .magnitude = 1000,
        .enabled = true
    });

    int injected = 0;
    for (int i = 0; i < 1000; ++i) {
        if (chaos.should_inject(ChaosFault::LatencySpike)) ++injected;
    }
    EXPECT_EQ(injected, 0);
}

TEST_F(ChaosEngineTest, ShouldInjectRespectsBudget) {
    chaos.enable(ChaosFault::PacketLoss, {
        .probability = 1.0,
        .max_injections = 5,
        .enabled = true
    });

    int injected = 0;
    for (int i = 0; i < 100; ++i) {
        if (chaos.should_inject(ChaosFault::PacketLoss)) ++injected;
    }
    EXPECT_EQ(injected, 5);
}

TEST_F(ChaosEngineTest, ShouldInjectRespectsCooldown) {
    chaos.enable(ChaosFault::LatencySpike, {
        .probability = 1.0,
        .magnitude = 100,
        .cooldown_ns = 100'000'000, // 100ms cooldown
        .enabled = true
    });

    // First injection should succeed
    EXPECT_TRUE(chaos.should_inject(ChaosFault::LatencySpike));
    // Immediate second should fail due to cooldown
    EXPECT_FALSE(chaos.should_inject(ChaosFault::LatencySpike));
}

TEST_F(ChaosEngineTest, InjectLatencyProducesDelay) {
    chaos.enable(ChaosFault::LatencySpike, {
        .probability = 1.0,
        .magnitude = 10000, // up to 10 µs
        .enabled = true
    });

    uint64_t t0 = TscClock::now_ns();
    uint64_t actual = chaos.inject_latency();
    uint64_t elapsed = TscClock::now_ns() - t0;

    EXPECT_GT(actual, 0u);
    EXPECT_GE(elapsed, actual);
    EXPECT_EQ(chaos.stats().latency_spikes.load(), 1u);
}

TEST_F(ChaosEngineTest, InjectPacketLoss) {
    chaos.enable(ChaosFault::PacketLoss, {
        .probability = 1.0,
        .enabled = true
    });

    bool dropped = chaos.inject_packet_loss();
    EXPECT_TRUE(dropped);
    EXPECT_EQ(chaos.stats().packets_dropped.load(), 1u);
}

TEST_F(ChaosEngineTest, GenerateFakeDelta) {
    chaos.enable(ChaosFault::FakeDelta, {
        .probability = 1.0,
        .magnitude = 100'000'000, // 1.0 price units
        .enabled = true
    });

    FakeDelta fd = chaos.generate_fake_delta(50000.0);
    EXPECT_GT(fd.bid_count + fd.ask_count, 0u);
    EXPECT_EQ(chaos.stats().fake_deltas.load(), 1u);
}

TEST_F(ChaosEngineTest, FakeDeltaDoesNotCrashOrderBook) {
    ChaosEngine det_chaos{123};
    det_chaos.enable(ChaosFault::FakeDelta, {
        .probability = 1.0,
        .magnitude = 100'000'000,
        .enabled = true
    });

    OrderBook ob;
    PriceLevel bids[] = {{50000.0, 1.0}, {49999.9, 2.0}};
    PriceLevel asks[] = {{50000.1, 1.0}, {50000.2, 2.0}};
    ob.apply_snapshot(bids, 2, asks, 2, 1);

    // Apply 1000 fake deltas — must never crash
    for (int i = 0; i < 1000; ++i) {
        FakeDelta fd = det_chaos.generate_fake_delta(50000.0);
        // Apply delta — OrderBook should handle gracefully
        ob.apply_delta(fd.bids, fd.bid_count, fd.asks, fd.ask_count,
                       static_cast<uint64_t>(i + 2));
    }
    // Should not crash, OB should still be valid
    EXPECT_TRUE(ob.valid());
}

TEST_F(ChaosEngineTest, InjectOOM) {
    chaos.enable(ChaosFault::OOMSimulation, {
        .probability = 1.0,
        .magnitude = 3,
        .enabled = true
    });

    // Should simulate 3 consecutive OOM failures
    EXPECT_TRUE(chaos.inject_oom());
    EXPECT_EQ(chaos.stats().oom_simulations.load(), 1u);
}

TEST_F(ChaosEngineTest, GenerateCorruptedJSON) {
    chaos.enable(ChaosFault::CorruptedJSON, {
        .probability = 1.0,
        .magnitude = 0xFF,
        .enabled = true
    });

    char buf[65536];
    for (int i = 0; i < 100; ++i) {
        size_t len = chaos.generate_corrupted_json(buf, sizeof(buf));
        // Should not crash, len should be reasonable
        EXPECT_LE(len, sizeof(buf) - 1);
    }
    EXPECT_EQ(chaos.stats().corrupted_jsons.load(), 100u);
}

TEST_F(ChaosEngineTest, InjectClockSkew) {
    chaos.enable(ChaosFault::ClockSkew, {
        .probability = 1.0,
        .magnitude = 1'000'000'000, // 1 second
        .enabled = true
    });

    uint64_t real_ts = 1'000'000'000'000ULL; // 1000 seconds
    uint64_t skewed = chaos.inject_clock_skew(real_ts);

    // Skewed should be within ±1s of real
    int64_t diff = static_cast<int64_t>(skewed) - static_cast<int64_t>(real_ts);
    EXPECT_LE(std::abs(diff), 1'000'000'000LL);
    EXPECT_EQ(chaos.stats().clock_skews.load(), 1u);
}

TEST_F(ChaosEngineTest, NightlyProfile) {
    chaos.enable_nightly_profile();
    EXPECT_TRUE(chaos.is_enabled());

    // All faults should be enabled
    for (int i = 0; i < static_cast<int>(ChaosFault::COUNT); ++i) {
        EXPECT_TRUE(chaos.is_fault_enabled(static_cast<ChaosFault>(i)));
    }
}

TEST_F(ChaosEngineTest, FlashCrashScenario) {
    chaos.enable_flash_crash_scenario();
    EXPECT_TRUE(chaos.is_enabled());
    EXPECT_TRUE(chaos.is_fault_enabled(ChaosFault::LatencySpike));
    EXPECT_TRUE(chaos.is_fault_enabled(ChaosFault::PacketLoss));
    EXPECT_TRUE(chaos.is_fault_enabled(ChaosFault::FakeDelta));
}

TEST_F(ChaosEngineTest, StatsTracking) {
    chaos.enable(ChaosFault::LatencySpike, {
        .probability = 1.0,
        .magnitude = 100,
        .enabled = true
    });

    for (int i = 0; i < 10; ++i) {
        chaos.should_inject(ChaosFault::LatencySpike);
    }
    EXPECT_EQ(chaos.stats().total_checks.load(), 10u);
    EXPECT_EQ(chaos.stats().total_injections.load(), 10u);
    EXPECT_EQ(chaos.injection_count(ChaosFault::LatencySpike), 10u);
}

TEST_F(ChaosEngineTest, ResetStats) {
    chaos.enable(ChaosFault::LatencySpike, {
        .probability = 1.0,
        .magnitude = 100,
        .enabled = true
    });
    chaos.should_inject(ChaosFault::LatencySpike);

    chaos.reset_stats();
    EXPECT_EQ(chaos.stats().total_checks.load(), 0u);
    EXPECT_EQ(chaos.stats().total_injections.load(), 0u);
    EXPECT_EQ(chaos.injection_count(ChaosFault::LatencySpike), 0u);
}

TEST_F(ChaosEngineTest, FaultNameLookup) {
    EXPECT_STREQ(chaos_fault_name(ChaosFault::LatencySpike), "LatencySpike");
    EXPECT_STREQ(chaos_fault_name(ChaosFault::PacketLoss), "PacketLoss");
    EXPECT_STREQ(chaos_fault_name(ChaosFault::FakeDelta), "FakeDelta");
    EXPECT_STREQ(chaos_fault_name(ChaosFault::OOMSimulation), "OOMSimulation");
    EXPECT_STREQ(chaos_fault_name(ChaosFault::CorruptedJSON), "CorruptedJSON");
    EXPECT_STREQ(chaos_fault_name(ChaosFault::ClockSkew), "ClockSkew");
}

TEST_F(ChaosEngineTest, FastPathWhenDisabled) {
    // Measure overhead of should_inject when globally disabled
    uint64_t t0 = TscClock::now_ns();
    for (int i = 0; i < 10000; ++i) {
        chaos.should_inject(ChaosFault::LatencySpike);
    }
    uint64_t elapsed = TscClock::now_ns() - t0;
    double per_call_ns = static_cast<double>(elapsed) / 10000.0;

    // Should be < 50 ns per call when disabled (just atomic load)
    EXPECT_LT(per_call_ns, 100.0);
}

TEST_F(ChaosEngineTest, EnvCheck) {
    // Just verify the function doesn't crash
    // (actual env var may or may not be set)
    bool env = chaos_enabled_by_env();
    (void)env;
}

// ═══════════════════════════════════════════════════════════════════════════
// PerfSignpost Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(PerfSignpostTest, StageCounterRecord) {
    StagePerfCounter counter;
    counter.record(1000);
    counter.record(2000);
    counter.record(3000);

    EXPECT_EQ(counter.invocation_count.load(), 3u);
    EXPECT_EQ(counter.histogram.count(), 3);
    EXPECT_GT(counter.histogram.mean(), 0.0);
}

TEST(PerfSignpostTest, StageCounterSummary) {
    StagePerfCounter counter;
    for (int i = 0; i < 1000; ++i) {
        counter.record(1000 + i * 10); // 1µs to 11µs
    }

    auto s = counter.summarize("TestStage");
    EXPECT_STREQ(s.name, "TestStage");
    EXPECT_EQ(s.count, 1000);
    EXPECT_GT(s.mean_us, 0.0);
    EXPECT_GT(s.p50_us, 0.0);
    EXPECT_GT(s.p99_us, 0.0);
}

TEST(PerfSignpostTest, EngineBeginStage) {
    PerfSignpostEngine engine;

    for (int i = 0; i < 100; ++i) {
        auto guard = engine.begin_stage(WatchdogStage::OrderBook);
        // Simulate ~1µs work
        volatile int x = 0;
        for (int j = 0; j < 100; ++j) x += j;
    }

    auto& ctr = engine.counter(WatchdogStage::OrderBook);
    EXPECT_EQ(ctr.invocation_count.load(), 100u);
    EXPECT_GT(ctr.histogram.count(), 0);
}

TEST(PerfSignpostTest, ManualRecord) {
    PerfSignpostEngine engine;
    engine.record_stage(WatchdogStage::Features, 5000);
    engine.record_stage(WatchdogStage::Features, 7000);

    auto& ctr = engine.counter(WatchdogStage::Features);
    EXPECT_EQ(ctr.invocation_count.load(), 2u);
}

TEST(PerfSignpostTest, Snapshot) {
    PerfSignpostEngine engine;
    engine.record_stage(WatchdogStage::Parser, 1000);
    engine.record_stage(WatchdogStage::OrderBook, 2000);
    engine.record_stage(WatchdogStage::Model, 5000);

    auto snap = engine.snapshot();
    EXPECT_EQ(snap.stage_count, 8u);
}

TEST(PerfSignpostTest, Reset) {
    PerfSignpostEngine engine;
    engine.record_stage(WatchdogStage::Parser, 1000);
    engine.reset();
    EXPECT_EQ(engine.counter(WatchdogStage::Parser).invocation_count.load(), 0u);
}

TEST(PerfSignpostTest, ExportFlamegraph) {
    PerfSignpostEngine engine;
    for (int i = 0; i < 100; ++i) {
        engine.record_stage(WatchdogStage::OrderBook, 1000 + i * 10);
        engine.record_stage(WatchdogStage::Features, 2000 + i * 5);
    }

    bool ok = engine.export_flamegraph("/tmp/bybit_test_flamegraph.txt");
    EXPECT_TRUE(ok);
}

TEST(PerfSignpostTest, ExportSummary) {
    PerfSignpostEngine engine;
    for (int i = 0; i < 100; ++i) {
        engine.record_stage(WatchdogStage::Execution, 3000 + i * 10);
    }

    bool ok = engine.export_summary("/tmp/bybit_test_summary.csv");
    EXPECT_TRUE(ok);
}

// ═══════════════════════════════════════════════════════════════════════════
// Chaos + OrderBook Stress Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ChaosStressTest, OrderBookSurvivesAllFaultTypes) {
    ChaosEngine chaos{999};
    chaos.enable_nightly_profile();

    OrderBook ob;
    PriceLevel bids[] = {
        {50000.0, 1.0}, {49999.9, 2.0}, {49999.8, 3.0},
        {49999.7, 4.0}, {49999.6, 5.0}
    };
    PriceLevel asks[] = {
        {50000.1, 1.0}, {50000.2, 2.0}, {50000.3, 3.0},
        {50000.4, 4.0}, {50000.5, 5.0}
    };
    ob.apply_snapshot(bids, 5, asks, 5, 1);

    // Run 10000 iterations with all chaos enabled
    for (uint64_t i = 2; i < 10002; ++i) {
        if (chaos.should_inject(ChaosFault::FakeDelta)) {
            FakeDelta fd = chaos.generate_fake_delta(50000.0);
            ob.apply_delta(fd.bids, fd.bid_count, fd.asks, fd.ask_count, i);
        } else {
            // Normal delta
            PriceLevel b[] = {{50000.0, 1.0 + (i % 10) * 0.1}};
            PriceLevel a[] = {{50000.1, 1.0 + (i % 10) * 0.1}};
            ob.apply_delta(b, 1, a, 1, i);
        }

        // Read operations must not crash
        volatile double mid = ob.mid_price();
        volatile double sp = ob.spread();
        volatile double imb = ob.imbalance(5);
        (void)mid; (void)sp; (void)imb;
    }

    EXPECT_TRUE(ob.valid());
}

// ═══════════════════════════════════════════════════════════════════════════
// Watchdog Histogram Export Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(WatchdogHistogramTest, HeartbeatRecordsHistogram) {
    HeartbeatRegistry registry;

    for (int i = 0; i < 1000; ++i) {
        registry.heartbeat(WatchdogStage::OrderBook,
                           static_cast<uint64_t>(100 + i));
    }

    auto& hist = registry.histogram(WatchdogStage::OrderBook);
    EXPECT_EQ(hist.count(), 1000);
    EXPECT_GT(hist.mean(), 0.0);
    EXPECT_GT(hist.p99(), 0);
}

TEST(WatchdogHistogramTest, ExportHistograms) {
    HeartbeatRegistry registry;

    registry.heartbeat(WatchdogStage::Parser, 500);
    registry.heartbeat(WatchdogStage::OrderBook, 1000);
    registry.heartbeat(WatchdogStage::Features, 2000);

    bool ok = registry.export_histograms("/tmp/bybit_test_histograms.csv");
    EXPECT_TRUE(ok);
}

TEST(WatchdogHistogramTest, ResetHistograms) {
    HeartbeatRegistry registry;
    registry.heartbeat(WatchdogStage::Parser, 500);
    registry.reset_histograms();
    EXPECT_EQ(registry.histogram(WatchdogStage::Parser).count(), 0);
}
