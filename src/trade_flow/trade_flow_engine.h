#pragma once

#include "../config/types.h"
#include "../utils/clock.h"
#include <array>
#include <cstdint>
#include <cstddef>
#include <cmath>

namespace bybit {

// Ring buffer for trades with rolling window computation.
// Single-threaded writer expected; readers must snapshot.
class TradeFlowEngine {
public:
    static constexpr size_t CAPACITY = 8192;
    static constexpr size_t MASK = CAPACITY - 1;

    // Rolling window definitions (milliseconds)
    static constexpr uint64_t WINDOW_100MS  = 100'000'000ULL;  // ns
    static constexpr uint64_t WINDOW_500MS  = 500'000'000ULL;
    static constexpr uint64_t WINDOW_2000MS = 2'000'000'000ULL;

    struct WindowStats {
        double buy_volume  = 0.0;
        double sell_volume = 0.0;
        double trade_rate  = 0.0;
        uint32_t trade_count = 0;
    };

    struct FlowSnapshot {
        WindowStats w100ms;
        WindowStats w500ms;
        WindowStats w2000ms;
        bool burst_detected = false;
    };

    TradeFlowEngine() noexcept { reset(); }

    void reset() noexcept {
        head_ = 0;
        count_ = 0;
        acc_100ms_ = {};
        acc_500ms_ = {};
        acc_2000ms_ = {};
        tail_100ms_ = 0;
        tail_500ms_ = 0;
        tail_2000ms_ = 0;
    }

    // E5: Rebuild cadence — every N trades, rebuild accumulators from ring buffer
    // to eliminate floating-point drift from incremental add/subtract.
    static constexpr size_t REBUILD_CADENCE = 1000;

    // S5: O(1) amortized — update accumulators incrementally
    void on_trade(const Trade& trade) noexcept {
        trades_[head_ & MASK] = trade;
        ++head_;
        if (count_ < CAPACITY) ++count_;
        ++trades_since_rebuild_;

        // Add to all accumulators
        double vol = trade.qty;
        if (trade.is_buyer_maker) {
            acc_100ms_.sell_volume += vol;
            acc_500ms_.sell_volume += vol;
            acc_2000ms_.sell_volume += vol;
        } else {
            acc_100ms_.buy_volume += vol;
            acc_500ms_.buy_volume += vol;
            acc_2000ms_.buy_volume += vol;
        }
        ++acc_100ms_.trade_count;
        ++acc_500ms_.trade_count;
        ++acc_2000ms_.trade_count;

        // Evict expired trades from each window
        uint64_t now = trade.timestamp_ns;
        evict_expired(now, WINDOW_100MS,  acc_100ms_,  tail_100ms_);
        evict_expired(now, WINDOW_500MS,  acc_500ms_,  tail_500ms_);
        evict_expired(now, WINDOW_2000MS, acc_2000ms_, tail_2000ms_);

        // E5: Periodic full rebuild to eliminate drift
        if (trades_since_rebuild_ >= REBUILD_CADENCE) {
            rebuild_accumulators(now);
            trades_since_rebuild_ = 0;
        }
    }

    // S5: O(1) snapshot from cached accumulators
    FlowSnapshot compute() const noexcept {
        FlowSnapshot snap;

        snap.w100ms  = finalize_stats(acc_100ms_,  WINDOW_100MS);
        snap.w500ms  = finalize_stats(acc_500ms_,  WINDOW_500MS);
        snap.w2000ms = finalize_stats(acc_2000ms_, WINDOW_2000MS);

        // Burst detection: 100ms rate > 3x the 2s average rate
        if (snap.w2000ms.trade_rate > 0.0) {
            snap.burst_detected = (snap.w100ms.trade_rate > 3.0 * snap.w2000ms.trade_rate);
        }

        return snap;
    }

    // Aggression ratio: buy_vol / (buy_vol + sell_vol) for given window
    double aggression_ratio(uint64_t window_ns) const noexcept {
        auto stats = compute_window(Clock::now_ns(), window_ns);
        double total = stats.buy_volume + stats.sell_volume;
        if (total < 1e-12) return 0.5;
        return stats.buy_volume / total;
    }

    // Volume acceleration: (vol_100ms / 0.1s) / (vol_2000ms / 2.0s)
    double volume_acceleration() const noexcept {
        auto snap = compute();
        double rate_short = (snap.w100ms.buy_volume + snap.w100ms.sell_volume) / 0.1;
        double rate_long  = (snap.w2000ms.buy_volume + snap.w2000ms.sell_volume) / 2.0;
        if (rate_long < 1e-12) return 0.0;
        return rate_short / rate_long;
    }

    // Trade velocity: trades/sec in 500ms window
    double trade_velocity() const noexcept {
        auto stats = compute_window(Clock::now_ns(), WINDOW_500MS);
        return stats.trade_rate;
    }

    size_t size() const noexcept { return count_; }

private:
    // S5: Evict trades older than window from accumulator
    void evict_expired(uint64_t now_ns, uint64_t window_ns,
                       WindowStats& acc, size_t& tail) noexcept {
        uint64_t cutoff = (now_ns > window_ns) ? (now_ns - window_ns) : 0;
        size_t oldest_valid = (head_ > count_) ? (head_ - count_) : 0;
        while (tail < head_ && tail >= oldest_valid) {
            const Trade& t = trades_[tail & MASK];
            if (t.timestamp_ns >= cutoff) break;
            // Subtract from accumulator
            if (t.is_buyer_maker) {
                acc.sell_volume -= t.qty;
            } else {
                acc.buy_volume -= t.qty;
            }
            if (acc.trade_count > 0) --acc.trade_count;
            ++tail;
        }
        // Clamp negative drift from floating point (belt-and-suspenders)
        if (acc.buy_volume < 0.0) acc.buy_volume = 0.0;
        if (acc.sell_volume < 0.0) acc.sell_volume = 0.0;
    }

    // E5: Rebuild all accumulators from ring buffer — O(count_) but bounded
    // by CAPACITY. Called every REBUILD_CADENCE trades. This eliminates
    // floating-point drift from thousands of incremental add/subtract ops.
    void rebuild_accumulators(uint64_t now_ns) noexcept {
        acc_100ms_  = {};
        acc_500ms_  = {};
        acc_2000ms_ = {};

        uint64_t cutoff_100  = (now_ns > WINDOW_100MS)  ? (now_ns - WINDOW_100MS)  : 0;
        uint64_t cutoff_500  = (now_ns > WINDOW_500MS)  ? (now_ns - WINDOW_500MS)  : 0;
        uint64_t cutoff_2000 = (now_ns > WINDOW_2000MS) ? (now_ns - WINDOW_2000MS) : 0;

        size_t start_idx = (head_ > count_) ? (head_ - count_) : 0;

        // Reset tails to start — will be advanced below
        tail_100ms_  = head_;
        tail_500ms_  = head_;
        tail_2000ms_ = head_;

        // Scan from oldest to newest within the ring
        for (size_t i = start_idx; i < head_; ++i) {
            const Trade& t = trades_[i & MASK];

            if (t.timestamp_ns >= cutoff_2000) {
                if (tail_2000ms_ > i) tail_2000ms_ = i;
                if (t.is_buyer_maker) {
                    acc_2000ms_.sell_volume += t.qty;
                } else {
                    acc_2000ms_.buy_volume += t.qty;
                }
                ++acc_2000ms_.trade_count;
            }
            if (t.timestamp_ns >= cutoff_500) {
                if (tail_500ms_ > i) tail_500ms_ = i;
                if (t.is_buyer_maker) {
                    acc_500ms_.sell_volume += t.qty;
                } else {
                    acc_500ms_.buy_volume += t.qty;
                }
                ++acc_500ms_.trade_count;
            }
            if (t.timestamp_ns >= cutoff_100) {
                if (tail_100ms_ > i) tail_100ms_ = i;
                if (t.is_buyer_maker) {
                    acc_100ms_.sell_volume += t.qty;
                } else {
                    acc_100ms_.buy_volume += t.qty;
                }
                ++acc_100ms_.trade_count;
            }
        }
        ++rebuild_count_;
    }

    size_t rebuild_count() const noexcept { return rebuild_count_; }

    static WindowStats finalize_stats(const WindowStats& acc, uint64_t window_ns) noexcept {
        WindowStats out = acc;
        double window_sec = static_cast<double>(window_ns) / 1e9;
        out.trade_rate = static_cast<double>(acc.trade_count) / window_sec;
        return out;
    }

    // Legacy full-scan for aggression_ratio/volume_acceleration with arbitrary windows
    WindowStats compute_window(uint64_t now_ns, uint64_t window_ns) const noexcept {
        WindowStats stats;
        if (count_ == 0) return stats;

        uint64_t cutoff = (now_ns > window_ns) ? (now_ns - window_ns) : 0;
        size_t start_idx = (head_ > count_) ? (head_ - count_) : 0;

        for (size_t i = head_; i > start_idx; --i) {
            const Trade& t = trades_[(i - 1) & MASK];
            if (t.timestamp_ns < cutoff) break;

            if (t.is_buyer_maker) {
                stats.sell_volume += t.qty;
            } else {
                stats.buy_volume += t.qty;
            }
            ++stats.trade_count;
        }

        double window_sec = static_cast<double>(window_ns) / 1e9;
        stats.trade_rate = static_cast<double>(stats.trade_count) / window_sec;

        return stats;
    }

    std::array<Trade, CAPACITY> trades_{};
    size_t head_ = 0;
    size_t count_ = 0;

    // S5: Incremental rolling accumulators
    WindowStats acc_100ms_{};
    WindowStats acc_500ms_{};
    WindowStats acc_2000ms_{};
    size_t tail_100ms_ = 0;
    size_t tail_500ms_ = 0;
    size_t tail_2000ms_ = 0;

    // E5: Periodic rebuild tracking
    size_t trades_since_rebuild_ = 0;
    size_t rebuild_count_ = 0;
};

} // namespace bybit
