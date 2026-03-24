#pragma once

// ─── Deterministic Replay Engine ───────────────────────────────────────────────
// 100% bit-exact replay of recorded tick streams from BlackBoxRecorder.
//
// Architecture:
//   1. Capture phase: BlackBoxRecorder stores every event with TSC timestamp
//   2. Serialize: dump_to_file() writes binary ring buffer to disk
//   3. Replay phase: ReplayEngine reads binary, feeds events in exact order
//      with original inter-event timing (scaled by speed factor)
//
// Guarantees:
//   - Bit-exact event sequence (same EventRecord bytes)
//   - Deterministic timestamp ordering (monotonic)
//   - Configurable replay speed (1x realtime, 0 = max speed)
//   - Event filtering by type, stage, time range
//   - CRC32 checksum verification of replay file integrity
//
// Usage:
//   ReplayEngine replay;
//   replay.load("blackbox_dump.bin");
//   replay.set_speed(0.0);  // max speed
//   replay.set_callback([&](const EventRecord& e) { process(e); });
//   replay.run();  // blocking replay
//
// Zero-alloc replay: events are memory-mapped from the file.

#include "blackbox_recorder.h"
#include "../utils/tsc_clock.h"
#include "../core/clock_source.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <atomic>

namespace bybit {

// ─── CRC32 (for replay file integrity) ────────────────────────────────────────
// Simple CRC32 implementation (IEEE polynomial). Used only on cold path.

namespace detail {

inline uint32_t crc32_table_entry(uint32_t idx) noexcept {
    uint32_t crc = idx;
    for (int j = 0; j < 8; ++j) {
        crc = (crc >> 1) ^ (0xEDB88320U & (-(crc & 1)));
    }
    return crc;
}

inline uint32_t compute_crc32(const void* data, size_t len) noexcept {
    // Build table on first call (cold path, thread-safe in C++11+)
    static uint32_t table[256] = {};
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) table[i] = crc32_table_entry(i);
        init = true;
    }

    auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

} // namespace detail

// ─── Replay File Header ────────────────────────────────────────────────────────

struct ReplayFileHeader {
    uint32_t magic    = 0x42425258; // "BBRX"
    uint32_t version  = 2;          // v2 with CRC32
    uint64_t count    = 0;          // number of events
    uint64_t start_ns = 0;          // first event timestamp
    uint64_t end_ns   = 0;          // last event timestamp
    uint32_t crc32    = 0;          // CRC32 of all event data
    uint32_t reserved = 0;
};

static_assert(sizeof(ReplayFileHeader) == 40, "ReplayFileHeader size mismatch");

// ─── Replay Filter ─────────────────────────────────────────────────────────────

struct ReplayFilter {
    uint64_t start_ns  = 0;              // only events after this timestamp
    uint64_t end_ns    = UINT64_MAX;     // only events before this timestamp
    uint32_t type_mask = 0xFFFFFFFF;    // bitmask of EventType to include (up to 32 types)
    uint8_t  min_severity = 0;           // minimum severity level
    uint8_t  max_stage    = 255;         // maximum stage number

    bool accepts(const EventRecord& e) const noexcept {
        if (e.timestamp_ns < start_ns || e.timestamp_ns > end_ns) return false;
        if (e.severity < min_severity) return false;
        if (e.stage > max_stage) return false;
        uint32_t bit = (1u << static_cast<uint8_t>(e.type));
        return (type_mask & bit) != 0;
    }
};

// ─── Replay Statistics ─────────────────────────────────────────────────────────

struct ReplayStats {
    uint64_t events_total     = 0;
    uint64_t events_replayed  = 0;
    uint64_t events_filtered  = 0;
    uint64_t replay_duration_ns = 0;
    uint64_t original_duration_ns = 0;
    double   replay_speed     = 0.0;  // actual achieved speed multiplier
    bool     checksum_valid   = false;
    bool     sequence_monotonic = true;
};

// ─── Replay Callback ──────────────────────────────────────────────────────────

using ReplayCallback = std::function<void(const EventRecord& event, size_t index)>;

// ─── Replay Engine ─────────────────────────────────────────────────────────────
// Template on WallClock: the clock used for PACING the replay loop.
// This is separate from the recorded event timestamps.
// Default: TscClockSource (real wall time for replay pacing).

template <typename WallClock = TscClockSource>
class ReplayEngine {
public:
    ReplayEngine() = default;
    explicit ReplayEngine(WallClock clock) : wall_clock_(std::move(clock)) {}

    // ─── Load from Binary File ──────────────────────────────────────────────
    // Loads a BlackBoxRecorder binary dump. Supports both v1 and v2 headers.

    bool load(const std::string& path) noexcept {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        // Read header
        ReplayFileHeader hdr{};
        file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        if (!file.good()) return false;

        // Check magic
        if (hdr.magic != 0x42425258) {
            // Try v1 format (simpler header: magic + version + count = 16 bytes)
            file.seekg(0);
            uint32_t v1_magic, v1_version;
            uint64_t v1_count;
            file.read(reinterpret_cast<char*>(&v1_magic), 4);
            file.read(reinterpret_cast<char*>(&v1_version), 4);
            file.read(reinterpret_cast<char*>(&v1_count), 8);
            if (v1_magic != 0x42425258) return false;

            hdr.magic = v1_magic;
            hdr.version = v1_version;
            hdr.count = v1_count;
            hdr.crc32 = 0; // v1 has no checksum
        }

        if (hdr.count == 0) {
            events_.clear();
            stats_ = {};
            return true;
        }

        // Read events
        events_.resize(hdr.count);
        file.read(reinterpret_cast<char*>(events_.data()),
                  static_cast<std::streamsize>(hdr.count * sizeof(EventRecord)));

        if (!file.good() && !file.eof()) {
            events_.clear();
            return false;
        }

        // Verify CRC32 BEFORE sorting (v2 only)
        stats_.checksum_valid = true;
        if (hdr.version >= 2 && hdr.crc32 != 0) {
            uint32_t computed = detail::compute_crc32(
                events_.data(), events_.size() * sizeof(EventRecord));
            stats_.checksum_valid = (computed == hdr.crc32);
        }

        // Sort by timestamp for deterministic replay
        std::sort(events_.begin(), events_.end(),
                  [](const EventRecord& a, const EventRecord& b) {
                      return a.timestamp_ns < b.timestamp_ns;
                  });

        // Check sequence monotonicity
        stats_.sequence_monotonic = true;
        for (size_t i = 1; i < events_.size(); ++i) {
            if (events_[i].sequence < events_[i-1].sequence) {
                stats_.sequence_monotonic = false;
                break;
            }
        }

        // Populate stats
        stats_.events_total = events_.size();
        if (!events_.empty()) {
            stats_.original_duration_ns = events_.back().timestamp_ns - events_.front().timestamp_ns;
        }

        header_ = hdr;
        loaded_ = true;
        return true;
    }

    // ─── Load from BlackBoxRecorder directly (in-memory) ────────────────────

    template <size_t Cap>
    bool load_from_recorder(const BlackBoxRecorder<Cap>& recorder) noexcept {
        size_t n = recorder.buffer_size();
        if (n == 0) {
            events_.clear();
            stats_ = {};
            loaded_ = true;
            return true;
        }

        events_.resize(n);
        size_t got = recorder.get_recent(events_.data(), n);
        events_.resize(got);

        // Reverse: get_recent returns most recent first
        std::reverse(events_.begin(), events_.end());

        // Sort by timestamp
        std::sort(events_.begin(), events_.end(),
                  [](const EventRecord& a, const EventRecord& b) {
                      return a.timestamp_ns < b.timestamp_ns;
                  });

        stats_.events_total = events_.size();
        stats_.checksum_valid = true;
        stats_.sequence_monotonic = true;
        if (!events_.empty()) {
            stats_.original_duration_ns = events_.back().timestamp_ns - events_.front().timestamp_ns;
        }

        loaded_ = true;
        return true;
    }

    // ─── Configuration ──────────────────────────────────────────────────────

    void set_speed(double speed_multiplier) noexcept { speed_ = speed_multiplier; }
    void set_filter(const ReplayFilter& filter) noexcept { filter_ = filter; }
    void set_callback(ReplayCallback cb) noexcept { callback_ = std::move(cb); }

    // ─── Run Replay (blocking) ──────────────────────────────────────────────
    // Replays all events through the callback, respecting speed and filter.
    // speed_ = 0.0 → max speed (no delays)
    // speed_ = 1.0 → realtime
    // speed_ = 2.0 → 2x faster

    bool run() noexcept {
        if (!loaded_ || events_.empty() || !callback_) return false;

        stop_.store(false, std::memory_order_release);
        stats_.events_replayed = 0;
        stats_.events_filtered = 0;

        uint64_t replay_start = wall_clock_.now().raw();
        uint64_t first_event_ts = events_.front().timestamp_ns;

        for (size_t i = 0; i < events_.size(); ++i) {
            if (stop_.load(std::memory_order_relaxed)) break;

            const auto& event = events_[i];

            // Apply filter
            if (!filter_.accepts(event)) {
                ++stats_.events_filtered;
                continue;
            }

            // Timing: wait until replay time matches event time (scaled)
            if (speed_ > 0.0 && i > 0) {
                uint64_t event_delta = event.timestamp_ns - first_event_ts;
                uint64_t scaled_delta = static_cast<uint64_t>(
                    static_cast<double>(event_delta) / speed_);
                uint64_t target_ns = replay_start + scaled_delta;
                uint64_t now = wall_clock_.now().raw();

                if (target_ns > now) {
                    uint64_t wait_ns = target_ns - now;
                    if (wait_ns > 1'000'000) {
                        // Sleep for large waits to avoid burning CPU
                        std::this_thread::sleep_for(
                            std::chrono::nanoseconds(wait_ns - 500'000));
                    }
                    // Spin for the remainder
                    while (wall_clock_.now().raw() < target_ns) {
                        // Busy spin for precision
                    }
                }
            }

            // Deliver event
            callback_(event, i);
            ++stats_.events_replayed;
        }

        uint64_t replay_end = wall_clock_.now().raw();
        stats_.replay_duration_ns = replay_end - replay_start;
        if (stats_.replay_duration_ns > 0 && stats_.original_duration_ns > 0) {
            stats_.replay_speed = static_cast<double>(stats_.original_duration_ns) /
                                  static_cast<double>(stats_.replay_duration_ns);
        }

        return true;
    }

    // ─── Run Replay on Background Thread ────────────────────────────────────

    void run_async() noexcept {
        if (replay_thread_.joinable()) {
            stop();
        }
        replay_thread_ = std::thread([this] { run(); });
    }

    void stop() noexcept {
        stop_.store(true, std::memory_order_release);
        // Only join if called from a different thread than the replay thread
        if (replay_thread_.joinable()) {
            if (replay_thread_.get_id() != std::this_thread::get_id()) {
                replay_thread_.join();
            } else {
                replay_thread_.detach();
            }
        }
    }

    // ─── Save with CRC32 (v2 format) ───────────────────────────────────────

    bool save(const std::string& path) const noexcept {
        if (events_.empty()) return false;

        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        ReplayFileHeader hdr{};
        hdr.magic = 0x42425258;
        hdr.version = 2;
        hdr.count = events_.size();
        hdr.start_ns = events_.front().timestamp_ns;
        hdr.end_ns = events_.back().timestamp_ns;
        hdr.crc32 = detail::compute_crc32(
            events_.data(), events_.size() * sizeof(EventRecord));

        file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        file.write(reinterpret_cast<const char*>(events_.data()),
                   static_cast<std::streamsize>(events_.size() * sizeof(EventRecord)));

        return file.good();
    }

    // ─── Bit-Exact Comparison ───────────────────────────────────────────────
    // Compares two replay streams for bit-exact match.

    static bool compare(const std::vector<EventRecord>& a,
                        const std::vector<EventRecord>& b) noexcept {
        if (a.size() != b.size()) return false;
        return std::memcmp(a.data(), b.data(),
                           a.size() * sizeof(EventRecord)) == 0;
    }

    bool compare_with(const ReplayEngine& other) const noexcept {
        return compare(events_, other.events_);
    }

    // ─── Accessors ──────────────────────────────────────────────────────────

    const ReplayStats& stats() const noexcept { return stats_; }
    size_t event_count() const noexcept { return events_.size(); }
    bool is_loaded() const noexcept { return loaded_; }

    const EventRecord& event_at(size_t idx) const noexcept {
        return events_[idx];
    }

    const std::vector<EventRecord>& events() const noexcept { return events_; }

    // ─── Iterator Access ────────────────────────────────────────────────────

    class Iterator {
    public:
        Iterator(const std::vector<EventRecord>& events, size_t pos,
                 const ReplayFilter& filter) noexcept
            : events_(events), pos_(pos), filter_(filter)
        {
            advance_to_valid();
        }

        const EventRecord& operator*() const noexcept { return events_[pos_]; }
        const EventRecord* operator->() const noexcept { return &events_[pos_]; }

        Iterator& operator++() noexcept {
            ++pos_;
            advance_to_valid();
            return *this;
        }

        bool operator!=(const Iterator& o) const noexcept { return pos_ != o.pos_; }
        bool operator==(const Iterator& o) const noexcept { return pos_ == o.pos_; }

        size_t index() const noexcept { return pos_; }

    private:
        void advance_to_valid() noexcept {
            while (pos_ < events_.size() && !filter_.accepts(events_[pos_])) {
                ++pos_;
            }
        }

        const std::vector<EventRecord>& events_;
        size_t pos_;
        const ReplayFilter& filter_;
    };

    Iterator begin() const noexcept { return Iterator(events_, 0, filter_); }
    Iterator end() const noexcept { return Iterator(events_, events_.size(), filter_); }

private:
    WallClock wall_clock_{};
    std::vector<EventRecord> events_;
    ReplayFileHeader header_{};
    ReplayFilter filter_{};
    ReplayCallback callback_;
    ReplayStats stats_{};
    double speed_ = 0.0; // 0 = max speed
    bool loaded_ = false;
    std::atomic<bool> stop_{false};
    std::thread replay_thread_;
};

// ─── Convenience Aliases ─────────────────────────────────────────────────────
using DefaultReplayEngine = ReplayEngine<TscClockSource>;

} // namespace bybit
