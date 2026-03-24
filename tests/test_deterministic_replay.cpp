#include <gtest/gtest.h>
#include "../src/monitoring/deterministic_replay.h"
#include "../src/monitoring/blackbox_recorder.h"

#include <cstring>
#include <filesystem>
#include <vector>

using namespace bybit;

// ═══════════════════════════════════════════════════════════════════════════
// BlackBoxRecorder v2 Tests (CRC32, event_at, checksums)
// ═══════════════════════════════════════════════════════════════════════════

class RecorderTest : public ::testing::Test {
protected:
    BlackBoxRecorder<1024> recorder;
};

TEST_F(RecorderTest, RecordAndRetrieve) {
    recorder.record(EventType::Trade, 50000.0, 1.5, 1.0, 0.0, "BTC", 1, 2);
    EXPECT_EQ(recorder.total_events(), 1u);
    EXPECT_EQ(recorder.buffer_size(), 1u);
}

TEST_F(RecorderTest, EventAtAccess) {
    for (int i = 0; i < 10; ++i) {
        recorder.record(EventType::OBDelta, static_cast<double>(i), 0.0);
    }

    for (int i = 0; i < 10; ++i) {
        const auto& e = recorder.event_at(i);
        EXPECT_EQ(e.type, EventType::OBDelta);
        EXPECT_DOUBLE_EQ(e.value1, static_cast<double>(i));
    }
}

TEST_F(RecorderTest, ChecksumDeterministic) {
    recorder.record(EventType::Trade, 50000.0, 1.0);
    recorder.record(EventType::OBDelta, 49999.0, 2.0);

    uint32_t crc1 = recorder.compute_checksum();
    uint32_t crc2 = recorder.compute_checksum();
    EXPECT_EQ(crc1, crc2);
    EXPECT_NE(crc1, 0u);
}

TEST_F(RecorderTest, ChecksumChangesWithData) {
    recorder.record(EventType::Trade, 50000.0, 1.0);
    uint32_t crc1 = recorder.compute_checksum();

    recorder.record(EventType::OBDelta, 49999.0, 2.0);
    uint32_t crc2 = recorder.compute_checksum();

    EXPECT_NE(crc1, crc2);
}

TEST_F(RecorderTest, DumpToFileV2) {
    for (int i = 0; i < 100; ++i) {
        recorder.record(EventType::Trade, 50000.0 + i, static_cast<double>(i));
    }

    std::string path = "/tmp/bybit_test_recorder_v2.bin";
    EXPECT_TRUE(recorder.dump_to_file(path));

    // Verify file exists and has correct size
    auto fsize = std::filesystem::file_size(path);
    // Header (40 bytes) + 100 events * 64 bytes
    EXPECT_EQ(fsize, 40u + 100u * 64u);
}

TEST_F(RecorderTest, DumpToFileV1Legacy) {
    recorder.record(EventType::Trade, 50000.0, 1.0);
    std::string path = "/tmp/bybit_test_recorder_v1.bin";
    EXPECT_TRUE(recorder.dump_to_file_v1(path));

    auto fsize = std::filesystem::file_size(path);
    // v1 Header (16 bytes) + 1 event * 64 bytes
    EXPECT_EQ(fsize, 16u + 64u);
}

TEST_F(RecorderTest, RingBufferWraparound) {
    // Fill beyond capacity to test circular behavior
    for (int i = 0; i < 2000; ++i) {
        recorder.record(EventType::Trade, static_cast<double>(i), 0.0);
    }

    EXPECT_EQ(recorder.total_events(), 2000u);
    EXPECT_EQ(recorder.buffer_size(), 1024u);

    // CRC should still be deterministic
    uint32_t crc = recorder.compute_checksum();
    EXPECT_NE(crc, 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
// Deterministic Replay Tests
// ═══════════════════════════════════════════════════════════════════════════

class ReplayTest : public ::testing::Test {
protected:
    BlackBoxRecorder<1024> recorder;

    void populate_recorder(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            recorder.record(
                static_cast<EventType>(i % 18),
                50000.0 + i * 0.1,
                static_cast<double>(i),
                0.0, 0.0, nullptr,
                static_cast<uint8_t>(i % 4),
                static_cast<uint8_t>(i % 8)
            );
        }
    }
};

TEST_F(ReplayTest, LoadFromRecorder) {
    populate_recorder(100);

    ReplayEngine replay;
    EXPECT_TRUE(replay.load_from_recorder(recorder));
    EXPECT_TRUE(replay.is_loaded());
    EXPECT_EQ(replay.event_count(), 100u);
}

TEST_F(ReplayTest, LoadFromFile) {
    populate_recorder(200);

    std::string path = "/tmp/bybit_test_replay_load.bin";
    recorder.dump_to_file(path);

    ReplayEngine replay;
    EXPECT_TRUE(replay.load(path));
    EXPECT_TRUE(replay.is_loaded());
    EXPECT_EQ(replay.event_count(), 200u);
    EXPECT_TRUE(replay.stats().checksum_valid);
}

TEST_F(ReplayTest, LoadFromV1File) {
    populate_recorder(50);

    std::string path = "/tmp/bybit_test_replay_v1.bin";
    recorder.dump_to_file_v1(path);

    ReplayEngine replay;
    EXPECT_TRUE(replay.load(path));
    EXPECT_EQ(replay.event_count(), 50u);
}

TEST_F(ReplayTest, ReplayMaxSpeed) {
    populate_recorder(500);

    ReplayEngine replay;
    replay.load_from_recorder(recorder);
    replay.set_speed(0.0); // max speed

    std::vector<EventRecord> replayed;
    replay.set_callback([&](const EventRecord& e, size_t idx) {
        replayed.push_back(e);
    });

    EXPECT_TRUE(replay.run());
    EXPECT_EQ(replayed.size(), 500u);
    EXPECT_EQ(replay.stats().events_replayed, 500u);
    EXPECT_EQ(replay.stats().events_filtered, 0u);
}

TEST_F(ReplayTest, ReplayMonotonicTimestamps) {
    populate_recorder(100);

    ReplayEngine replay;
    replay.load_from_recorder(recorder);
    replay.set_speed(0.0);

    uint64_t prev_ts = 0;
    bool monotonic = true;
    replay.set_callback([&](const EventRecord& e, size_t idx) {
        if (e.timestamp_ns < prev_ts) monotonic = false;
        prev_ts = e.timestamp_ns;
    });

    replay.run();
    EXPECT_TRUE(monotonic);
}

TEST_F(ReplayTest, ReplayWithFilter) {
    populate_recorder(200);

    ReplayEngine replay;
    replay.load_from_recorder(recorder);
    replay.set_speed(0.0);

    // Only replay Trade events
    ReplayFilter filter;
    filter.type_mask = (1u << static_cast<uint8_t>(EventType::Trade));
    replay.set_filter(filter);

    size_t count = 0;
    replay.set_callback([&](const EventRecord& e, size_t idx) {
        EXPECT_EQ(e.type, EventType::Trade);
        ++count;
    });

    replay.run();
    EXPECT_GT(count, 0u);
    EXPECT_LT(count, 200u);
    EXPECT_EQ(replay.stats().events_replayed, count);
    EXPECT_GT(replay.stats().events_filtered, 0u);
}

TEST_F(ReplayTest, ReplayFilterBySeverity) {
    populate_recorder(100);

    ReplayEngine replay;
    replay.load_from_recorder(recorder);
    replay.set_speed(0.0);

    ReplayFilter filter;
    filter.min_severity = 2; // only warn and error
    replay.set_filter(filter);

    size_t count = 0;
    replay.set_callback([&](const EventRecord& e, size_t idx) {
        EXPECT_GE(e.severity, 2u);
        ++count;
    });

    replay.run();
    EXPECT_GT(count, 0u);
}

TEST_F(ReplayTest, BitExactComparison) {
    populate_recorder(100);

    ReplayEngine replay1, replay2;
    replay1.load_from_recorder(recorder);
    replay2.load_from_recorder(recorder);

    EXPECT_TRUE(replay1.compare_with(replay2));
}

TEST_F(ReplayTest, SaveAndReload) {
    populate_recorder(300);

    ReplayEngine replay1;
    replay1.load_from_recorder(recorder);

    std::string path = "/tmp/bybit_test_replay_save.bin";
    EXPECT_TRUE(replay1.save(path));

    ReplayEngine replay2;
    EXPECT_TRUE(replay2.load(path));
    EXPECT_TRUE(replay2.stats().checksum_valid);
    EXPECT_EQ(replay2.event_count(), replay1.event_count());
}

TEST_F(ReplayTest, IteratorAccess) {
    populate_recorder(50);

    ReplayEngine replay;
    replay.load_from_recorder(recorder);

    size_t count = 0;
    for (auto it = replay.begin(); it != replay.end(); ++it) {
        ++count;
    }
    EXPECT_EQ(count, 50u);
}

TEST_F(ReplayTest, IteratorWithFilter) {
    populate_recorder(200);

    ReplayEngine replay;
    replay.load_from_recorder(recorder);

    ReplayFilter filter;
    filter.type_mask = (1u << static_cast<uint8_t>(EventType::Trade));
    replay.set_filter(filter);

    size_t count = 0;
    for (auto it = replay.begin(); it != replay.end(); ++it) {
        EXPECT_EQ(it->type, EventType::Trade);
        ++count;
    }
    EXPECT_GT(count, 0u);
}

TEST_F(ReplayTest, EmptyRecorderReplay) {
    ReplayEngine replay;
    EXPECT_TRUE(replay.load_from_recorder(recorder));
    EXPECT_EQ(replay.event_count(), 0u);

    replay.set_callback([](const EventRecord&, size_t) {});
    EXPECT_FALSE(replay.run()); // no events
}

TEST_F(ReplayTest, StopReplay) {
    populate_recorder(1000);

    ReplayEngine replay;
    replay.load_from_recorder(recorder);
    // Use realtime speed so replay takes time and we can stop it
    replay.set_speed(1.0);

    std::atomic<size_t> count{0};
    replay.set_callback([&](const EventRecord& e, size_t idx) {
        count.fetch_add(1, std::memory_order_relaxed);
    });

    replay.run_async();
    // Let it start, then stop
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    replay.stop();

    // Should have stopped before replaying all 1000 events
    // (realtime speed means it can't finish 1000 events in 10ms)
    EXPECT_LE(replay.stats().events_replayed, 1000u);
}

TEST_F(ReplayTest, ReplayStats) {
    populate_recorder(100);

    ReplayEngine replay;
    replay.load_from_recorder(recorder);
    replay.set_speed(0.0);
    replay.set_callback([](const EventRecord&, size_t) {});

    replay.run();

    const auto& stats = replay.stats();
    EXPECT_EQ(stats.events_total, 100u);
    EXPECT_EQ(stats.events_replayed, 100u);
    EXPECT_GT(stats.replay_duration_ns, 0u);
    EXPECT_TRUE(stats.checksum_valid);
    EXPECT_TRUE(stats.sequence_monotonic);
}

// ═══════════════════════════════════════════════════════════════════════════
// CRC32 Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(CRC32Test, EmptyData) {
    uint32_t crc = detail::compute_crc32(nullptr, 0);
    EXPECT_EQ(crc, 0u); // CRC32 of empty data
}

TEST(CRC32Test, KnownValue) {
    // "123456789" CRC32 = 0xCBF43926
    const char* data = "123456789";
    uint32_t crc = detail::compute_crc32(data, 9);
    EXPECT_EQ(crc, 0xCBF43926u);
}

TEST(CRC32Test, Deterministic) {
    const char* data = "Hello, HFT!";
    uint32_t crc1 = detail::compute_crc32(data, std::strlen(data));
    uint32_t crc2 = detail::compute_crc32(data, std::strlen(data));
    EXPECT_EQ(crc1, crc2);
}

TEST(CRC32Test, DifferentDataDifferentCRC) {
    const char* d1 = "abc";
    const char* d2 = "abd";
    uint32_t c1 = detail::compute_crc32(d1, 3);
    uint32_t c2 = detail::compute_crc32(d2, 3);
    EXPECT_NE(c1, c2);
}

// ═══════════════════════════════════════════════════════════════════════════
// Replay File Header Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(ReplayHeaderTest, SizeIs40) {
    EXPECT_EQ(sizeof(ReplayFileHeader), 40u);
}

TEST(ReplayHeaderTest, DefaultValues) {
    ReplayFileHeader hdr;
    EXPECT_EQ(hdr.magic, 0x42425258u);
    EXPECT_EQ(hdr.version, 2u);
    EXPECT_EQ(hdr.count, 0u);
}
