#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>

namespace bybit {

// Lock-free latency histogram with microsecond buckets.
// Bucket boundaries: [0, 1), [1, 2), ..., [N-1, N) µs, [N, ∞) µs
class LatencyHistogram {
public:
    static constexpr size_t NUM_BUCKETS = 1024;

    LatencyHistogram() noexcept { reset(); }

    void record(uint64_t latency_ns) noexcept {
        uint64_t us = latency_ns / 1000;
        size_t bucket = std::min(static_cast<size_t>(us), NUM_BUCKETS - 1);
        buckets_[bucket].fetch_add(1, std::memory_order_relaxed);
        count_.fetch_add(1, std::memory_order_relaxed);
        sum_ns_.fetch_add(latency_ns, std::memory_order_relaxed);

        uint64_t cur_max = max_ns_.load(std::memory_order_relaxed);
        while (latency_ns > cur_max &&
               !max_ns_.compare_exchange_weak(cur_max, latency_ns, std::memory_order_relaxed)) {}

        uint64_t cur_min = min_ns_.load(std::memory_order_relaxed);
        while (latency_ns < cur_min &&
               !min_ns_.compare_exchange_weak(cur_min, latency_ns, std::memory_order_relaxed)) {}
    }

    uint64_t percentile(double p) const noexcept {
        uint64_t total = count_.load(std::memory_order_relaxed);
        if (total == 0) return 0;

        uint64_t target = static_cast<uint64_t>(std::ceil(p * static_cast<double>(total)));
        uint64_t cumulative = 0;

        for (size_t i = 0; i < NUM_BUCKETS; ++i) {
            cumulative += buckets_[i].load(std::memory_order_relaxed);
            if (cumulative >= target) {
                return i * 1000; // return in ns (bucket is in µs)
            }
        }
        return (NUM_BUCKETS - 1) * 1000;
    }

    double mean_ns() const noexcept {
        uint64_t c = count_.load(std::memory_order_relaxed);
        if (c == 0) return 0.0;
        return static_cast<double>(sum_ns_.load(std::memory_order_relaxed)) / static_cast<double>(c);
    }

    uint64_t max() const noexcept { return max_ns_.load(std::memory_order_relaxed); }
    uint64_t min() const noexcept { return min_ns_.load(std::memory_order_relaxed); }
    uint64_t count() const noexcept { return count_.load(std::memory_order_relaxed); }

    void reset() noexcept {
        for (auto& b : buckets_) b.store(0, std::memory_order_relaxed);
        count_.store(0, std::memory_order_relaxed);
        sum_ns_.store(0, std::memory_order_relaxed);
        max_ns_.store(0, std::memory_order_relaxed);
        min_ns_.store(UINT64_MAX, std::memory_order_relaxed);
    }

private:
    std::array<std::atomic<uint64_t>, NUM_BUCKETS> buckets_;
    std::atomic<uint64_t> count_{0};
    std::atomic<uint64_t> sum_ns_{0};
    std::atomic<uint64_t> max_ns_{0};
    std::atomic<uint64_t> min_ns_{UINT64_MAX};
};

// Named metric collection
struct Metrics {
    LatencyHistogram ws_message_latency;
    LatencyHistogram ob_update_latency;
    LatencyHistogram feature_calc_latency;
    LatencyHistogram model_inference_latency;
    LatencyHistogram risk_check_latency;
    LatencyHistogram order_submit_latency;
    LatencyHistogram end_to_end_latency;

    std::atomic<uint64_t> ob_updates_total{0};
    std::atomic<uint64_t> trades_total{0};
    std::atomic<uint64_t> signals_total{0};
    std::atomic<uint64_t> orders_sent_total{0};
    std::atomic<uint64_t> orders_filled_total{0};
    std::atomic<uint64_t> orders_cancelled_total{0};
    std::atomic<uint64_t> ws_reconnects_total{0};
    std::atomic<uint64_t> ob_resyncs_total{0};

    // Stage 3: market-data correctness counters
    std::atomic<uint64_t> seq_gaps_total{0};
    std::atomic<uint64_t> ob_invalidations_total{0};
    std::atomic<uint64_t> deltas_dropped_total{0};

    // #7: Rate limit tracking
    std::atomic<int> rate_limit_remaining{20};
    std::atomic<int> rate_limit_total{20};

    // #8: Order reconciliation
    std::atomic<uint64_t> reconciliation_mismatches{0};
};

} // namespace bybit
