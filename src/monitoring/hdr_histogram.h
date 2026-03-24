#pragma once

// ─── Lock-Free HdrHistogram ─────────────────────────────────────────────────
// Per-stage latency tracking with O(1) record, O(1) percentile query.
// No locks, no allocations in hot path. Designed for nanosecond-resolution timing.
//
// Range: 1 ns to ~134 seconds (37 bits), 3 significant digits.
// Memory: ~32 KB per histogram (compact for cache efficiency).
//
// Usage:
//   HdrHistogram hist;
//   hist.record(latency_ns);
//   double p99 = hist.percentile(99.0);

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>

namespace bybit {

class HdrHistogram {
public:
    // Config: track 1 ns to 134 seconds with 3 significant digits
    static constexpr int64_t  LOWEST_VALUE    = 1;
    static constexpr int64_t  HIGHEST_VALUE   = 134'000'000'000LL; // ~134s in ns
    static constexpr int      SIGNIFICANT_DIGITS = 3;

    // Derived constants
    static constexpr int      SUB_BUCKET_BITS = 10; // 2^10 = 1024 sub-buckets
    static constexpr int      SUB_BUCKET_COUNT = 1 << SUB_BUCKET_BITS;
    static constexpr int      SUB_BUCKET_HALF  = SUB_BUCKET_COUNT / 2;
    static constexpr int      BUCKET_COUNT     = 28; // covers up to 2^37
    static constexpr size_t   COUNTS_LEN       = (BUCKET_COUNT + 1) * SUB_BUCKET_HALF;

    HdrHistogram() noexcept { reset(); }

    // ─── Record a value (hot path — lock-free) ─────────────────────────────

    void record(int64_t value) noexcept {
        if (__builtin_expect(value < LOWEST_VALUE, 0)) value = LOWEST_VALUE;
        if (__builtin_expect(value > HIGHEST_VALUE, 0)) value = HIGHEST_VALUE;

        size_t idx = count_index(value);
        if (__builtin_expect(idx < COUNTS_LEN, 1)) {
            counts_[idx].fetch_add(1, std::memory_order_relaxed);
        }
        total_count_.fetch_add(1, std::memory_order_relaxed);

        // Update min/max atomically (relaxed — approximate is fine)
        int64_t cur_min = min_.load(std::memory_order_relaxed);
        while (value < cur_min) {
            if (min_.compare_exchange_weak(cur_min, value, std::memory_order_relaxed)) break;
        }
        int64_t cur_max = max_.load(std::memory_order_relaxed);
        while (value > cur_max) {
            if (max_.compare_exchange_weak(cur_max, value, std::memory_order_relaxed)) break;
        }

        // Running sum for mean (relaxed)
        sum_.fetch_add(value, std::memory_order_relaxed);
    }

    // Convenience: record in microseconds
    void record_us(double us) noexcept {
        record(static_cast<int64_t>(us * 1000.0));
    }

    // ─── Queries ────────────────────────────────────────────────────────────

    // Percentile query: returns value at given percentile (0-100)
    int64_t percentile(double pct) const noexcept {
        int64_t total = total_count_.load(std::memory_order_acquire);
        if (total == 0) return 0;

        double target = (pct / 100.0) * total;
        int64_t cumulative = 0;

        for (size_t i = 0; i < COUNTS_LEN; ++i) {
            cumulative += counts_[i].load(std::memory_order_relaxed);
            if (static_cast<double>(cumulative) >= target) {
                return value_from_index(i);
            }
        }
        return max_.load(std::memory_order_relaxed);
    }

    // Common percentiles (ns)
    int64_t p50() const noexcept { return percentile(50.0); }
    int64_t p90() const noexcept { return percentile(90.0); }
    int64_t p95() const noexcept { return percentile(95.0); }
    int64_t p99() const noexcept { return percentile(99.0); }
    int64_t p999() const noexcept { return percentile(99.9); }

    // Percentiles in microseconds
    double p50_us() const noexcept { return static_cast<double>(p50()) / 1000.0; }
    double p90_us() const noexcept { return static_cast<double>(p90()) / 1000.0; }
    double p95_us() const noexcept { return static_cast<double>(p95()) / 1000.0; }
    double p99_us() const noexcept { return static_cast<double>(p99()) / 1000.0; }
    double p999_us() const noexcept { return static_cast<double>(p999()) / 1000.0; }

    // Stats
    int64_t min() const noexcept { return min_.load(std::memory_order_relaxed); }
    int64_t max() const noexcept { return max_.load(std::memory_order_relaxed); }
    int64_t count() const noexcept { return total_count_.load(std::memory_order_relaxed); }

    double mean() const noexcept {
        int64_t c = total_count_.load(std::memory_order_relaxed);
        if (c == 0) return 0.0;
        return static_cast<double>(sum_.load(std::memory_order_relaxed)) / c;
    }

    double mean_us() const noexcept { return mean() / 1000.0; }

    double stddev() const noexcept {
        int64_t c = total_count_.load(std::memory_order_relaxed);
        if (c < 2) return 0.0;
        double m = mean();
        // Approximate: use histogram bin centers for variance
        double var = 0.0;
        for (size_t i = 0; i < COUNTS_LEN; ++i) {
            int64_t cnt = counts_[i].load(std::memory_order_relaxed);
            if (cnt == 0) continue;
            double val = static_cast<double>(value_from_index(i));
            double diff = val - m;
            var += diff * diff * cnt;
        }
        return std::sqrt(var / c);
    }

    // Reset all counters
    void reset() noexcept {
        for (size_t i = 0; i < COUNTS_LEN; ++i) {
            counts_[i].store(0, std::memory_order_relaxed);
        }
        total_count_.store(0, std::memory_order_relaxed);
        min_.store(HIGHEST_VALUE, std::memory_order_relaxed);
        max_.store(0, std::memory_order_relaxed);
        sum_.store(0, std::memory_order_relaxed);
    }

private:
    // ─── Index computation ──────────────────────────────────────────────────
    // Maps a value to its histogram bucket index.

    static size_t count_index(int64_t value) noexcept {
        int bucket = bucket_index(value);
        int sub = sub_bucket_index(value, bucket);
        return static_cast<size_t>(bucket * SUB_BUCKET_HALF + sub);
    }

    static int bucket_index(int64_t value) noexcept {
        int pow2ceil = 64 - __builtin_clzll(static_cast<uint64_t>(value | SUB_BUCKET_COUNT));
        return std::max(0, pow2ceil - (SUB_BUCKET_BITS + 1));
    }

    static int sub_bucket_index(int64_t value, int bucket) noexcept {
        return static_cast<int>(static_cast<uint64_t>(value) >> bucket);
    }

    static int64_t value_from_index(size_t idx) noexcept {
        int bucket = static_cast<int>(idx / SUB_BUCKET_HALF);
        int sub = static_cast<int>(idx % SUB_BUCKET_HALF) + SUB_BUCKET_HALF;
        if (bucket == 0) {
            sub -= SUB_BUCKET_HALF;
        }
        int64_t value = static_cast<int64_t>(sub) << std::max(0, bucket - 1);
        return value > 0 ? value : 1;
    }

    // ─── Data ───────────────────────────────────────────────────────────────

    std::array<std::atomic<int64_t>, COUNTS_LEN> counts_{};
    std::atomic<int64_t> total_count_{0};
    std::atomic<int64_t> min_{HIGHEST_VALUE};
    std::atomic<int64_t> max_{0};
    std::atomic<int64_t> sum_{0};
};

// ─── Per-Stage Latency Tracker ──────────────────────────────────────────────
// Wraps HdrHistogram with stage identification and snapshot support.

struct StageLatency {
    HdrHistogram histogram;
    const char*  name = "unknown";

    void record(uint64_t duration_ns) noexcept {
        histogram.record(static_cast<int64_t>(duration_ns));
    }

    struct Snapshot {
        double p50_us;
        double p99_us;
        double p999_us;
        double mean_us;
        double max_us;
        int64_t count;
    };

    Snapshot snapshot() const noexcept {
        return {
            histogram.p50_us(),
            histogram.p99_us(),
            histogram.p999_us(),
            histogram.mean_us(),
            static_cast<double>(histogram.max()) / 1000.0,
            histogram.count()
        };
    }
};

} // namespace bybit
