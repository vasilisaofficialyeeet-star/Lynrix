#pragma once

// ─── Black-Box Recorder ─────────────────────────────────────────────────────
// Lock-free circular event recorder for post-mortem analysis.
// Records every pipeline event (OB update, trade, signal, order, error)
// into a fixed-size ring buffer with nanosecond timestamps.
//
// Features:
//   - Zero-allocation recording (pre-allocated ring buffer)
//   - Lock-free SPSC design (recorder thread writes, dump thread reads)
//   - Binary format for minimal overhead (~64 bytes per event)
//   - Flush to disk on crash/emergency via mmap
//   - Last N minutes of events always available for debugging
//
// Inspired by aviation flight data recorders.

#include "../config/types.h"
#include "../utils/clock.h"
#include "../core/strong_types.h"
#include "../core/memory_policy.h"
#include "../core/clock_source.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace bybit {

// ─── Event Types ────────────────────────────────────────────────────────────

enum class EventType : uint8_t {
    OBSnapshot   = 0,
    OBDelta      = 1,
    Trade        = 2,
    FeatureCalc  = 3,
    ModelInfer   = 4,
    SignalGen    = 5,
    RiskCheck    = 6,
    OrderSubmit  = 7,
    OrderFill    = 8,
    OrderCancel  = 9,
    OrderAmend   = 10,
    WSConnect    = 11,
    WSDisconnect = 12,
    WSError      = 13,
    CircuitTrip  = 14,
    Emergency    = 15,
    Heartbeat    = 16,
    Custom       = 17,
};

inline const char* event_type_name(EventType t) noexcept {
    constexpr const char* names[] = {
        "OBSnapshot", "OBDelta", "Trade", "FeatureCalc", "ModelInfer",
        "SignalGen", "RiskCheck", "OrderSubmit", "OrderFill", "OrderCancel",
        "OrderAmend", "WSConnect", "WSDisconnect", "WSError",
        "CircuitTrip", "Emergency", "Heartbeat", "Custom"
    };
    auto idx = static_cast<size_t>(t);
    return idx < 18 ? names[idx] : "Unknown";
}

// ─── Event Record (64 bytes, cache-line friendly) ───────────────────────────

struct alignas(64) EventRecord {
    uint64_t  timestamp_ns;     // 8 bytes — when
    EventType type;             // 1 byte  — what
    uint8_t   severity;         // 1 byte  — 0=debug, 1=info, 2=warn, 3=error
    uint8_t   stage;            // 1 byte  — pipeline stage
    uint8_t   reserved;         // 1 byte  — padding
    uint32_t  sequence;         // 4 bytes — monotonic sequence number
    double    value1;           // 8 bytes — primary value (price, latency, etc.)
    double    value2;           // 8 bytes — secondary value
    double    value3;           // 8 bytes — tertiary value
    double    value4;           // 8 bytes — quaternary value
    char      tag[16];          // 16 bytes — short label/symbol
};

static_assert(sizeof(EventRecord) == 64, "EventRecord must be exactly 64 bytes");
BYBIT_VERIFY_TRIVIAL(EventRecord);

// ─── Black-Box Recorder ─────────────────────────────────────────────────────
// Template on ClockSource for deterministic replay injection.
// Production: BlackBoxRecorder<65536, TscClockSource> (default)
// Replay:     BlackBoxRecorder<65536, ReplayClockSource>
// The clock is stored by value — TscClockSource is stateless (0 bytes).

template <size_t Capacity = 65536, typename ClockSource = TscClockSource>
class BlackBoxRecorder {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

public:
    BlackBoxRecorder() noexcept {
        head_.store(0, std::memory_order_relaxed);
        seq_.store(0, std::memory_order_relaxed);
    }

    explicit BlackBoxRecorder(ClockSource clock) noexcept
        : clock_(std::move(clock)) {
        head_.store(0, std::memory_order_relaxed);
        seq_.store(0, std::memory_order_relaxed);
    }

    // Access the clock source (for replay control)
    ClockSource& clock() noexcept { return clock_; }
    const ClockSource& clock() const noexcept { return clock_; }

    // ─── Record Event (hot path — lock-free, zero-alloc) ────────────────────

    void record(EventType type, double v1 = 0.0, double v2 = 0.0,
                double v3 = 0.0, double v4 = 0.0,
                const char* tag = nullptr, uint8_t severity = 1,
                uint8_t stage = 0) noexcept {
        uint32_t seq = seq_.fetch_add(1, std::memory_order_relaxed);
        size_t idx = head_.fetch_add(1, std::memory_order_relaxed) & MASK;

        auto& evt = buffer_[idx];
        evt.timestamp_ns = clock_.now().raw();
        evt.type = type;
        evt.severity = severity;
        evt.stage = stage;
        evt.reserved = 0;
        evt.sequence = seq;
        evt.value1 = v1;
        evt.value2 = v2;
        evt.value3 = v3;
        evt.value4 = v4;

        if (tag) {
            std::strncpy(evt.tag, tag, sizeof(evt.tag) - 1);
            evt.tag[sizeof(evt.tag) - 1] = '\0';
        } else {
            evt.tag[0] = '\0';
        }
    }

    // ─── Convenience recorders ──────────────────────────────────────────────

    void record_ob_update(double mid_price, double spread, size_t bid_levels,
                          size_t ask_levels) noexcept {
        record(EventType::OBDelta, mid_price, spread,
               static_cast<double>(bid_levels), static_cast<double>(ask_levels),
               nullptr, 0, 2);
    }

    void record_trade(double price, double qty, bool is_buyer_maker) noexcept {
        record(EventType::Trade, price, qty,
               is_buyer_maker ? 1.0 : 0.0, 0.0, nullptr, 0, 2);
    }

    void record_signal(double price, double confidence, bool is_buy,
                       double fill_prob) noexcept {
        record(EventType::SignalGen, price, confidence,
               is_buy ? 1.0 : -1.0, fill_prob, nullptr, 1, 5);
    }

    void record_order(const char* order_id, double price, double qty,
                      bool is_buy) noexcept {
        record(EventType::OrderSubmit, price, qty,
               is_buy ? 1.0 : -1.0, 0.0, order_id, 1, 7);
    }

    void record_fill(const char* order_id, double price, double qty) noexcept {
        record(EventType::OrderFill, price, qty, 0.0, 0.0, order_id, 1, 7);
    }

    void record_latency(uint8_t stage, double latency_us) noexcept {
        record(EventType::FeatureCalc, latency_us, 0.0, 0.0, 0.0,
               nullptr, 0, stage);
    }

    void record_error(const char* msg, uint8_t stage = 0) noexcept {
        record(EventType::Custom, 0.0, 0.0, 0.0, 0.0, msg, 3, stage);
    }

    void record_circuit_trip(const char* reason, double drawdown,
                             int consec_losses) noexcept {
        record(EventType::CircuitTrip, drawdown,
               static_cast<double>(consec_losses), 0.0, 0.0, reason, 3, 6);
    }

    // ─── Dump to File v2 (cold path) ─────────────────────────────────────────
    // Writes all events in chronological order to binary file.
    // v2 header includes CRC32 checksum for replay integrity verification.

    bool dump_to_file(const std::string& path) const noexcept {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        uint64_t count = std::min(static_cast<uint64_t>(seq_.load(std::memory_order_acquire)),
                                  static_cast<uint64_t>(Capacity));
        size_t head = head_.load(std::memory_order_acquire);
        size_t start = (count >= Capacity) ? head : 0;

        // Collect events into contiguous buffer for CRC32
        std::vector<EventRecord> ordered(count);
        for (uint64_t i = 0; i < count; ++i) {
            size_t idx = (start + i) & MASK;
            ordered[i] = buffer_[idx];
        }

        // Compute CRC32 over event data
        uint32_t crc = compute_crc32_ieee(
            ordered.data(), ordered.size() * sizeof(EventRecord));

        // v2 header: magic(4) + version(4) + count(8) + start_ns(8) + end_ns(8) + crc32(4) + reserved(4) = 40 bytes
        uint32_t magic   = 0x42425258; // "BBRX"
        uint32_t version = 2;
        uint64_t start_ns = count > 0 ? ordered.front().timestamp_ns : 0;
        uint64_t end_ns   = count > 0 ? ordered.back().timestamp_ns : 0;
        uint32_t reserved = 0;

        file.write(reinterpret_cast<const char*>(&magic), 4);
        file.write(reinterpret_cast<const char*>(&version), 4);
        file.write(reinterpret_cast<const char*>(&count), 8);
        file.write(reinterpret_cast<const char*>(&start_ns), 8);
        file.write(reinterpret_cast<const char*>(&end_ns), 8);
        file.write(reinterpret_cast<const char*>(&crc), 4);
        file.write(reinterpret_cast<const char*>(&reserved), 4);

        // Events
        file.write(reinterpret_cast<const char*>(ordered.data()),
                   static_cast<std::streamsize>(count * sizeof(EventRecord)));

        return file.good();
    }

    // ─── Legacy v1 dump (backward compat) ────────────────────────────────────

    bool dump_to_file_v1(const std::string& path) const noexcept {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t magic = 0x42425258;
        uint32_t version = 1;
        uint64_t count = std::min(static_cast<uint64_t>(seq_.load(std::memory_order_acquire)),
                                  static_cast<uint64_t>(Capacity));
        file.write(reinterpret_cast<const char*>(&magic), 4);
        file.write(reinterpret_cast<const char*>(&version), 4);
        file.write(reinterpret_cast<const char*>(&count), 8);

        size_t head = head_.load(std::memory_order_acquire);
        size_t start = (count >= Capacity) ? head : 0;

        for (uint64_t i = 0; i < count; ++i) {
            size_t idx = (start + i) & MASK;
            file.write(reinterpret_cast<const char*>(&buffer_[idx]),
                       sizeof(EventRecord));
        }

        return file.good();
    }

    // ─── Dump to Text (debugging) ───────────────────────────────────────────

    bool dump_to_text(const std::string& path) const noexcept {
        std::ofstream file(path);
        if (!file.is_open()) return false;

        uint64_t count = std::min(static_cast<uint64_t>(seq_.load(std::memory_order_acquire)),
                                  static_cast<uint64_t>(Capacity));
        size_t head = head_.load(std::memory_order_acquire);
        size_t start = (count >= Capacity) ? head : 0;

        file << "# BlackBox Dump: " << count << " events\n";
        file << "# seq,timestamp_ns,type,severity,stage,v1,v2,v3,v4,tag\n";

        for (size_t i = 0; i < count; ++i) {
            size_t idx = (start + i) & MASK;
            const auto& e = buffer_[idx];
            file << e.sequence << ","
                 << e.timestamp_ns << ","
                 << event_type_name(e.type) << ","
                 << static_cast<int>(e.severity) << ","
                 << static_cast<int>(e.stage) << ","
                 << e.value1 << "," << e.value2 << ","
                 << e.value3 << "," << e.value4 << ","
                 << e.tag << "\n";
        }

        return file.good();
    }

    // ─── Stats ──────────────────────────────────────────────────────────────

    uint64_t total_events() const noexcept {
        return seq_.load(std::memory_order_relaxed);
    }

    size_t buffer_size() const noexcept {
        return std::min(static_cast<size_t>(seq_.load(std::memory_order_relaxed)),
                        Capacity);
    }

    // Get last N events (most recent first)
    size_t get_recent(EventRecord* out, size_t max_count) const noexcept {
        size_t available = buffer_size();
        size_t n = std::min(max_count, available);
        size_t head = head_.load(std::memory_order_acquire);

        for (size_t i = 0; i < n; ++i) {
            size_t idx = (head - 1 - i) & MASK;
            out[i] = buffer_[idx];
        }
        return n;
    }

    static constexpr size_t capacity() noexcept { return Capacity; }

    // ─── Event accessor (for replay) ─────────────────────────────────────────
    // Access event at ring buffer position (chronological order).

    const EventRecord& event_at(size_t index) const noexcept {
        size_t count = buffer_size();
        size_t head = head_.load(std::memory_order_acquire);
        size_t start = (count >= Capacity) ? head : 0;
        size_t idx = (start + index) & MASK;
        return buffer_[idx];
    }

    // ─── Compute CRC32 of current buffer ─────────────────────────────────────

    uint32_t compute_checksum() const noexcept {
        size_t count = buffer_size();
        size_t head = head_.load(std::memory_order_acquire);
        size_t start = (count >= Capacity) ? head : 0;

        uint32_t crc = 0xFFFFFFFFU;
        for (size_t i = 0; i < count; ++i) {
            size_t idx = (start + i) & MASK;
            auto* bytes = reinterpret_cast<const uint8_t*>(&buffer_[idx]);
            for (size_t b = 0; b < sizeof(EventRecord); ++b) {
                crc = crc32_table_[(crc ^ bytes[b]) & 0xFF] ^ (crc >> 8);
            }
        }
        return crc ^ 0xFFFFFFFFU;
    }

private:
    // ─── CRC32 helpers ───────────────────────────────────────────────────────

    static uint32_t compute_crc32_ieee(const void* data, size_t len) noexcept {
        auto* bytes = static_cast<const uint8_t*>(data);
        uint32_t crc = 0xFFFFFFFFU;
        for (size_t i = 0; i < len; ++i) {
            crc = crc32_table_[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFU;
    }

    static constexpr uint32_t crc32_entry(uint32_t idx) noexcept {
        uint32_t crc = idx;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320U & (-(crc & 1)));
        }
        return crc;
    }

    // Compile-time CRC32 lookup table
    struct CRC32Table {
        uint32_t entries[256];
        constexpr CRC32Table() noexcept : entries{} {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t crc = i;
                for (int j = 0; j < 8; ++j) {
                    crc = (crc >> 1) ^ (0xEDB88320U & (-(crc & 1)));
                }
                entries[i] = crc;
            }
        }
    };

    static constexpr CRC32Table crc32_table_gen_{};
    static constexpr const uint32_t* crc32_table_ = crc32_table_gen_.entries;

    static constexpr size_t MASK = Capacity - 1;

    ClockSource clock_{};
    std::array<EventRecord, Capacity> buffer_{};
    alignas(128) std::atomic<size_t> head_{0};
    alignas(128) std::atomic<uint32_t> seq_{0};
};

// ─── Default Recorder Types ─────────────────────────────────────────────────
// 64K events × 64 bytes = 4 MB — fits in L3 cache on Apple Silicon

using DefaultRecorder = BlackBoxRecorder<65536, TscClockSource>;
using ReplayRecorder  = BlackBoxRecorder<65536, ReplayClockSource>;

} // namespace bybit
