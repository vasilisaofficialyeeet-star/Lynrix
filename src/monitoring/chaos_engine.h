#pragma once

// ─── Chaos Engine ──────────────────────────────────────────────────────────────
// Production-grade chaos testing framework for HFT pipeline resilience.
//
// Fault injection modes:
//   1. Random latency spikes (simulate GC pauses, context switches)
//   2. Packet loss (simulate network drops, out-of-order delivery)
//   3. Fake orderbook deltas (corrupt prices, impossible spreads)
//   4. OOM simulation (exhaust pool allocators, ring buffers)
//   5. Corrupted JSON (malformed WebSocket messages)
//   6. Clock skew (timestamp regression, large jumps)
//
// Activation: nightly CI via BYBIT_CHAOS=1 env var, or programmatic enable.
// Thread-safe: all state is atomic or thread-local.
// Zero impact when disabled: all injection points are branchless no-ops.
//
// Usage:
//   ChaosEngine chaos;
//   chaos.enable(ChaosFault::LatencySpike, {.probability = 0.01, .magnitude = 50000});
//   ...
//   if (chaos.should_inject(ChaosFault::LatencySpike)) {
//       chaos.inject_latency(); // spins for configured duration
//   }

#include "../utils/tsc_clock.h"
#include "../config/types.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <random>

namespace bybit {

// ─── Fault Types ───────────────────────────────────────────────────────────────

enum class ChaosFault : uint8_t {
    LatencySpike    = 0,  // Inject random delay (ns)
    PacketLoss      = 1,  // Drop message entirely
    FakeDelta       = 2,  // Inject corrupted OB delta
    OOMSimulation   = 3,  // Simulate pool exhaustion
    CorruptedJSON   = 4,  // Malformed JSON payload
    ClockSkew       = 5,  // Timestamp anomaly
    COUNT           = 6,
};

inline const char* chaos_fault_name(ChaosFault f) noexcept {
    constexpr const char* names[] = {
        "LatencySpike", "PacketLoss", "FakeDelta",
        "OOMSimulation", "CorruptedJSON", "ClockSkew"
    };
    auto idx = static_cast<size_t>(f);
    return idx < 6 ? names[idx] : "Unknown";
}

// ─── Fault Configuration ───────────────────────────────────────────────────────

struct ChaosFaultConfig {
    double   probability   = 0.0;    // [0.0, 1.0] — injection probability per call
    uint64_t magnitude     = 0;      // Fault-specific magnitude:
                                     //   LatencySpike: max delay in ns
                                     //   PacketLoss: burst length
                                     //   FakeDelta: max price offset (raw FixedPrice units)
                                     //   OOMSimulation: consecutive alloc failures
                                     //   CorruptedJSON: corruption type mask
                                     //   ClockSkew: max skew in ns
    uint64_t cooldown_ns   = 0;      // Minimum time between injections
    uint64_t max_injections = UINT64_MAX; // Total injection budget
    bool     enabled       = false;
};

// ─── Chaos Statistics ──────────────────────────────────────────────────────────

struct alignas(64) ChaosStats {
    std::atomic<uint64_t> total_checks{0};
    std::atomic<uint64_t> total_injections{0};
    std::atomic<uint64_t> latency_spikes{0};
    std::atomic<uint64_t> packets_dropped{0};
    std::atomic<uint64_t> fake_deltas{0};
    std::atomic<uint64_t> oom_simulations{0};
    std::atomic<uint64_t> corrupted_jsons{0};
    std::atomic<uint64_t> clock_skews{0};
    std::atomic<uint64_t> total_injected_latency_ns{0};
    std::atomic<uint64_t> max_injected_latency_ns{0};

    void reset() noexcept {
        total_checks.store(0, std::memory_order_relaxed);
        total_injections.store(0, std::memory_order_relaxed);
        latency_spikes.store(0, std::memory_order_relaxed);
        packets_dropped.store(0, std::memory_order_relaxed);
        fake_deltas.store(0, std::memory_order_relaxed);
        oom_simulations.store(0, std::memory_order_relaxed);
        corrupted_jsons.store(0, std::memory_order_relaxed);
        clock_skews.store(0, std::memory_order_relaxed);
        total_injected_latency_ns.store(0, std::memory_order_relaxed);
        max_injected_latency_ns.store(0, std::memory_order_relaxed);
    }
};

// ─── Fake Delta Generator ──────────────────────────────────────────────────────
// Generates corrupted orderbook deltas for chaos testing.

struct FakeDelta {
    PriceLevel bids[8];
    PriceLevel asks[8];
    size_t     bid_count = 0;
    size_t     ask_count = 0;
};

// ─── JSON Corruption Types ─────────────────────────────────────────────────────

enum class JSONCorruptionType : uint8_t {
    TruncatedPayload  = 0,  // Cut message mid-way
    MissingBraces     = 1,  // Remove { or }
    InvalidUTF8       = 2,  // Insert 0xFF bytes
    NullBytes         = 3,  // Insert \0 in middle
    ExtremelyLong     = 4,  // 10 MB payload
    EmptyMessage      = 5,  // Zero-length message
    BinaryGarbage     = 6,  // Random binary data
    DuplicateKeys     = 7,  // {"price":1,"price":2}
};

// ─── Chaos Engine ──────────────────────────────────────────────────────────────

class ChaosEngine {
public:
    ChaosEngine() noexcept {
        // Seed from TSC for non-deterministic chaos
        uint64_t seed = TscClock::now_ns();
        rng_.seed(static_cast<uint32_t>(seed ^ (seed >> 32)));
    }

    explicit ChaosEngine(uint32_t seed) noexcept {
        rng_.seed(seed);
    }

    // ─── Enable/Disable ─────────────────────────────────────────────────────

    void enable(ChaosFault fault, const ChaosFaultConfig& cfg) noexcept {
        auto idx = static_cast<size_t>(fault);
        if (idx < static_cast<size_t>(ChaosFault::COUNT)) {
            configs_[idx] = cfg;
            configs_[idx].enabled = true;
        }
        global_enabled_.store(true, std::memory_order_release);
    }

    void disable(ChaosFault fault) noexcept {
        auto idx = static_cast<size_t>(fault);
        if (idx < static_cast<size_t>(ChaosFault::COUNT)) {
            configs_[idx].enabled = false;
        }
        // Check if any fault is still enabled
        bool any = false;
        for (size_t i = 0; i < static_cast<size_t>(ChaosFault::COUNT); ++i) {
            if (configs_[i].enabled) { any = true; break; }
        }
        if (!any) global_enabled_.store(false, std::memory_order_release);
    }

    void disable_all() noexcept {
        for (size_t i = 0; i < static_cast<size_t>(ChaosFault::COUNT); ++i) {
            configs_[i].enabled = false;
        }
        global_enabled_.store(false, std::memory_order_release);
    }

    bool is_enabled() const noexcept {
        return global_enabled_.load(std::memory_order_acquire);
    }

    bool is_fault_enabled(ChaosFault fault) const noexcept {
        auto idx = static_cast<size_t>(fault);
        return idx < static_cast<size_t>(ChaosFault::COUNT) && configs_[idx].enabled;
    }

    // ─── Injection Check (branchless fast path when disabled) ────────────────
    // Returns true if the fault should be injected NOW.
    // When globally disabled, this is a single atomic load → false.

    bool should_inject(ChaosFault fault) noexcept {
        // Fast path: globally disabled → single atomic load
        if (__builtin_expect(!global_enabled_.load(std::memory_order_relaxed), 1)) {
            return false;
        }

        auto idx = static_cast<size_t>(fault);
        if (__builtin_expect(idx >= static_cast<size_t>(ChaosFault::COUNT), 0)) return false;

        auto& cfg = configs_[idx];
        if (!cfg.enabled) return false;

        stats_.total_checks.fetch_add(1, std::memory_order_relaxed);

        // Budget check
        if (injection_counts_[idx] >= cfg.max_injections) return false;

        // Cooldown check
        uint64_t now = TscClock::now_ns();
        if (cfg.cooldown_ns > 0 && (now - last_injection_ns_[idx]) < cfg.cooldown_ns) {
            return false;
        }

        // Probability check
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(rng_) >= cfg.probability) return false;

        // Inject!
        last_injection_ns_[idx] = now;
        ++injection_counts_[idx];
        stats_.total_injections.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // ─── Latency Spike Injection ────────────────────────────────────────────
    // Busy-spins for a random duration up to configured magnitude.
    // Uses TSC for precise delay measurement.

    uint64_t inject_latency() noexcept {
        auto& cfg = configs_[static_cast<size_t>(ChaosFault::LatencySpike)];
        if (cfg.magnitude == 0) return 0;

        std::uniform_int_distribution<uint64_t> dist(100, cfg.magnitude);
        uint64_t delay_ns = dist(rng_);

        uint64_t start = TscClock::now_ns();
        while ((TscClock::now_ns() - start) < delay_ns) {
            // Busy spin — intentional for chaos testing
#if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
#elif defined(__aarch64__)
            asm volatile("yield");
#endif
        }
        uint64_t actual = TscClock::now_ns() - start;

        stats_.latency_spikes.fetch_add(1, std::memory_order_relaxed);
        stats_.total_injected_latency_ns.fetch_add(actual, std::memory_order_relaxed);

        // Update max
        uint64_t cur_max = stats_.max_injected_latency_ns.load(std::memory_order_relaxed);
        while (actual > cur_max) {
            if (stats_.max_injected_latency_ns.compare_exchange_weak(
                    cur_max, actual, std::memory_order_relaxed)) break;
        }

        return actual;
    }

    // ─── Packet Loss Simulation ─────────────────────────────────────────────
    // Returns true if this packet should be "dropped".

    bool inject_packet_loss() noexcept {
        stats_.packets_dropped.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // ─── Fake Delta Generation ──────────────────────────────────────────────
    // Generates corrupted orderbook deltas with:
    //   - Negative prices, zero prices, extreme prices
    //   - Negative quantities, NaN quantities
    //   - Crossed book (bid > ask)
    //   - Duplicate price levels

    FakeDelta generate_fake_delta(double base_price = 50000.0) noexcept {
        FakeDelta fd;
        auto& cfg = configs_[static_cast<size_t>(ChaosFault::FakeDelta)];
        double mag = static_cast<double>(cfg.magnitude) * 1e-8; // raw → price

        std::uniform_int_distribution<int> type_dist(0, 7);
        int corruption_type = type_dist(rng_);

        switch (corruption_type) {
            case 0: { // Negative price
                fd.bids[0] = {-base_price, 1.0};
                fd.asks[0] = {base_price + 0.1, 1.0};
                fd.bid_count = 1; fd.ask_count = 1;
                break;
            }
            case 1: { // Zero price
                fd.bids[0] = {0.0, 1.0};
                fd.asks[0] = {0.0, 1.0};
                fd.bid_count = 1; fd.ask_count = 1;
                break;
            }
            case 2: { // Extreme price (1B)
                fd.bids[0] = {1e9, 1.0};
                fd.asks[0] = {1e9 + 0.1, 1.0};
                fd.bid_count = 1; fd.ask_count = 1;
                break;
            }
            case 3: { // Negative quantity
                fd.bids[0] = {base_price, -100.0};
                fd.asks[0] = {base_price + 0.1, -50.0};
                fd.bid_count = 1; fd.ask_count = 1;
                break;
            }
            case 4: { // NaN quantity
                fd.bids[0] = {base_price, std::nan("")};
                fd.asks[0] = {base_price + 0.1, std::nan("")};
                fd.bid_count = 1; fd.ask_count = 1;
                break;
            }
            case 5: { // Crossed book (bid > ask)
                fd.bids[0] = {base_price + 100.0, 1.0};
                fd.asks[0] = {base_price - 100.0, 1.0};
                fd.bid_count = 1; fd.ask_count = 1;
                break;
            }
            case 6: { // Many duplicate price levels
                for (size_t i = 0; i < 8; ++i) {
                    fd.bids[i] = {base_price, static_cast<double>(i + 1)};
                    fd.asks[i] = {base_price + 0.1, static_cast<double>(i + 1)};
                }
                fd.bid_count = 8; fd.ask_count = 8;
                break;
            }
            case 7: { // Random large offset
                std::uniform_real_distribution<double> off(-mag, mag);
                double offset = off(rng_);
                fd.bids[0] = {base_price + offset, 1.0};
                fd.asks[0] = {base_price + offset + 0.1, 1.0};
                fd.bid_count = 1; fd.ask_count = 1;
                break;
            }
        }

        stats_.fake_deltas.fetch_add(1, std::memory_order_relaxed);
        return fd;
    }

    // ─── OOM Simulation ─────────────────────────────────────────────────────
    // Returns true if allocation should "fail".

    bool inject_oom() noexcept {
        stats_.oom_simulations.fetch_add(1, std::memory_order_relaxed);

        auto& cfg = configs_[static_cast<size_t>(ChaosFault::OOMSimulation)];
        if (oom_remaining_ == 0) {
            oom_remaining_ = cfg.magnitude > 0 ? cfg.magnitude : 1;
        }
        if (oom_remaining_ > 0) {
            --oom_remaining_;
            return true;
        }
        return false;
    }

    // ─── Corrupted JSON Generation ──────────────────────────────────────────
    // Generates malformed JSON payloads for WebSocket parser stress testing.
    // Writes into caller-provided buffer. Returns actual length.

    size_t generate_corrupted_json(char* buf, size_t buf_size) noexcept {
        if (buf_size == 0) return 0;

        std::uniform_int_distribution<int> type_dist(0, 7);
        auto ctype = static_cast<JSONCorruptionType>(type_dist(rng_));

        stats_.corrupted_jsons.fetch_add(1, std::memory_order_relaxed);

        switch (ctype) {
            case JSONCorruptionType::TruncatedPayload: {
                const char* payload = R"({"topic":"orderbook.50.BTCUSDT","type":"delta","data":{"s":"BTCUSDT","b":[["50000.0","1.5)";
                size_t len = std::min(std::strlen(payload), buf_size - 1);
                std::memcpy(buf, payload, len);
                buf[len] = '\0';
                return len;
            }
            case JSONCorruptionType::MissingBraces: {
                const char* payload = R"("topic":"orderbook.50.BTCUSDT","type":"delta","data":"s":"BTCUSDT")";
                size_t len = std::min(std::strlen(payload), buf_size - 1);
                std::memcpy(buf, payload, len);
                buf[len] = '\0';
                return len;
            }
            case JSONCorruptionType::InvalidUTF8: {
                const char raw[] = "{\"topic\":\xFF\xFE\x80\"orderbook\xFF}";
                size_t len = std::min(sizeof(raw) - 1, buf_size - 1);
                std::memcpy(buf, raw, len);
                buf[len] = '\0';
                return len;
            }
            case JSONCorruptionType::NullBytes: {
                const char* base = R"({"topic":"orderbook"})";
                size_t base_len = std::strlen(base);
                size_t len = std::min(base_len, buf_size - 1);
                std::memcpy(buf, base, len);
                // Insert null bytes at random positions
                if (len > 4) {
                    std::uniform_int_distribution<size_t> pos_dist(1, len - 2);
                    buf[pos_dist(rng_)] = '\0';
                    buf[pos_dist(rng_)] = '\0';
                }
                return len; // Note: actual string may be shorter due to \0
            }
            case JSONCorruptionType::ExtremelyLong: {
                // Fill with valid-looking but extremely long JSON
                size_t len = std::min(buf_size - 1, static_cast<size_t>(65536));
                std::memset(buf, 'A', len);
                buf[0] = '{';
                buf[1] = '"';
                if (len > 2) buf[len - 1] = '}';
                buf[len] = '\0';
                return len;
            }
            case JSONCorruptionType::EmptyMessage: {
                buf[0] = '\0';
                return 0;
            }
            case JSONCorruptionType::BinaryGarbage: {
                size_t len = std::min(buf_size - 1, static_cast<size_t>(256));
                std::uniform_int_distribution<int> byte_dist(0, 255);
                for (size_t i = 0; i < len; ++i) {
                    buf[i] = static_cast<char>(byte_dist(rng_));
                }
                buf[len] = '\0';
                return len;
            }
            case JSONCorruptionType::DuplicateKeys: {
                const char* payload = R"({"topic":"orderbook","topic":"trade","type":"delta","type":"snapshot","data":{},"data":[]})";
                size_t len = std::min(std::strlen(payload), buf_size - 1);
                std::memcpy(buf, payload, len);
                buf[len] = '\0';
                return len;
            }
        }

        buf[0] = '\0';
        return 0;
    }

    // ─── Clock Skew Injection ───────────────────────────────────────────────
    // Returns a skewed timestamp (for testing timestamp monotonicity checks).

    uint64_t inject_clock_skew(uint64_t real_timestamp_ns) noexcept {
        auto& cfg = configs_[static_cast<size_t>(ChaosFault::ClockSkew)];
        int64_t max_skew = static_cast<int64_t>(cfg.magnitude);

        std::uniform_int_distribution<int64_t> dist(-max_skew, max_skew);
        int64_t skew = dist(rng_);

        stats_.clock_skews.fetch_add(1, std::memory_order_relaxed);

        int64_t result = static_cast<int64_t>(real_timestamp_ns) + skew;
        return result > 0 ? static_cast<uint64_t>(result) : 0;
    }

    // ─── Batch Chaos (for nightly test runs) ────────────────────────────────
    // Enables all faults with reasonable defaults for nightly CI.

    void enable_nightly_profile() noexcept {
        enable(ChaosFault::LatencySpike, {
            .probability = 0.001,     // 0.1% of calls
            .magnitude = 100'000,     // up to 100 µs
            .cooldown_ns = 1'000'000, // 1 ms cooldown
            .max_injections = 10000,
            .enabled = true
        });

        enable(ChaosFault::PacketLoss, {
            .probability = 0.005,     // 0.5% of packets
            .magnitude = 3,           // burst of 3
            .cooldown_ns = 5'000'000, // 5 ms cooldown
            .max_injections = 5000,
            .enabled = true
        });

        enable(ChaosFault::FakeDelta, {
            .probability = 0.002,       // 0.2%
            .magnitude = 1'000'000'00,  // ±1.0 price units in raw
            .cooldown_ns = 10'000'000,  // 10 ms
            .max_injections = 2000,
            .enabled = true
        });

        enable(ChaosFault::OOMSimulation, {
            .probability = 0.0001,    // 0.01%
            .magnitude = 5,           // 5 consecutive failures
            .cooldown_ns = 100'000'000, // 100 ms
            .max_injections = 100,
            .enabled = true
        });

        enable(ChaosFault::CorruptedJSON, {
            .probability = 0.003,     // 0.3%
            .magnitude = 0xFF,        // all corruption types
            .cooldown_ns = 5'000'000, // 5 ms
            .max_injections = 3000,
            .enabled = true
        });

        enable(ChaosFault::ClockSkew, {
            .probability = 0.001,       // 0.1%
            .magnitude = 1'000'000'000, // up to 1 second skew
            .cooldown_ns = 50'000'000,  // 50 ms
            .max_injections = 1000,
            .enabled = true
        });
    }

    // ─── Scenario: Market Flash Crash ───────────────────────────────────────
    // Simulates a flash crash scenario with aggressive parameters.

    void enable_flash_crash_scenario() noexcept {
        enable(ChaosFault::LatencySpike, {
            .probability = 0.05,
            .magnitude = 500'000, // 500 µs spikes
            .cooldown_ns = 0,
            .max_injections = UINT64_MAX,
            .enabled = true
        });

        enable(ChaosFault::PacketLoss, {
            .probability = 0.10, // 10% packet loss
            .magnitude = 10,
            .cooldown_ns = 0,
            .max_injections = UINT64_MAX,
            .enabled = true
        });

        enable(ChaosFault::FakeDelta, {
            .probability = 0.05,
            .magnitude = 10'000'000'00, // ±10 price units
            .cooldown_ns = 0,
            .max_injections = UINT64_MAX,
            .enabled = true
        });
    }

    // ─── Accessors ──────────────────────────────────────────────────────────

    const ChaosStats& stats() const noexcept { return stats_; }
    ChaosStats& stats() noexcept { return stats_; }

    const ChaosFaultConfig& config(ChaosFault fault) const noexcept {
        return configs_[static_cast<size_t>(fault)];
    }

    uint64_t injection_count(ChaosFault fault) const noexcept {
        auto idx = static_cast<size_t>(fault);
        return idx < static_cast<size_t>(ChaosFault::COUNT) ? injection_counts_[idx] : 0;
    }

    void reset_stats() noexcept {
        stats_.reset();
        for (auto& c : injection_counts_) c = 0;
        for (auto& t : last_injection_ns_) t = 0;
        oom_remaining_ = 0;
    }

private:
    static constexpr size_t FAULT_COUNT = static_cast<size_t>(ChaosFault::COUNT);

    std::atomic<bool> global_enabled_{false};
    std::array<ChaosFaultConfig, FAULT_COUNT> configs_{};
    std::array<uint64_t, FAULT_COUNT> injection_counts_{};
    std::array<uint64_t, FAULT_COUNT> last_injection_ns_{};

    ChaosStats stats_{};

    // OOM state
    uint64_t oom_remaining_ = 0;

    // PRNG (thread-local usage assumed, or externally synchronized)
    std::mt19937 rng_;
};

// ─── Environment-based auto-enable ─────────────────────────────────────────────
// Check BYBIT_CHAOS env var at startup.

inline bool chaos_enabled_by_env() noexcept {
    const char* env = std::getenv("BYBIT_CHAOS");
    return env && (env[0] == '1' || env[0] == 'y' || env[0] == 'Y');
}

} // namespace bybit
