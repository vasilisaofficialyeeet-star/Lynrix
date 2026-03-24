#pragma once

// ─── High-Performance L2 OrderBook v3 ───────────────────────────────────────
// NON-PRODUCTION: This is an experimental/benchmark implementation.
// Production code uses orderbook.h (v2). Do NOT include both in the same TU.
//
// O(1) delta updates via open-addressing hashmap + intrusive doubly-linked list.
// Zero allocation in hot path — all storage from contiguous fixed-size pools.
//
// Architecture:
//   - PriceLevelNode: 64B, holds price (FixedPrice), qty, and intrusive list ptrs
//   - PriceMap: open-addressing Robin Hood hashmap (FixedPrice → pool index)
//   - Sorted order maintained by intrusive doubly-linked list (best→worst)
//   - CompactLevel 16B preserved for NEON-vectorized bulk queries
//
// Complexity:
//   - Delta update (modify existing): O(1) amortized (hash lookup)
//   - Delta update (insert new):      O(k) where k = levels to skip in linked list
//   - Delta update (remove):          O(1) (hash lookup + unlink)
//   - BBO access:                     O(1) (head of linked list)
//   - Imbalance/VWAP:                 O(depth) via linked list traversal
//
// Memory layout:
//   - Pool: MAX_LEVELS * sizeof(PriceLevelNode) contiguous
//   - HashMap: HASH_CAPACITY * sizeof(HashEntry) contiguous
//   - Total: ~88 KB for 500 levels (fits in L2 cache)
//
// Backward compatibility:
//   - Same public API as OrderBook v2 (drop-in replacement)
//   - Legacy PriceLevel accessors preserved for bridge
//   - CompactLevel accessors preserved for SIMD

#include "../config/types.h"
#include "../core/market_data.h"
#include "../utils/tsc_clock.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace bybit {

// ─── Fixed-Point Price (same as v2) ─────────────────────────────────────────

struct FixedPrice {
    int64_t raw = 0; // price * 1e8

    static FixedPrice from_double(double p) noexcept {
        return {static_cast<int64_t>(p * 1e8 + 0.5)};
    }
    double to_double() const noexcept {
        return static_cast<double>(raw) * 1e-8;
    }
    bool operator==(FixedPrice o) const noexcept { return raw == o.raw; }
    bool operator!=(FixedPrice o) const noexcept { return raw != o.raw; }
    bool operator<(FixedPrice o) const noexcept { return raw < o.raw; }
    bool operator>(FixedPrice o) const noexcept { return raw > o.raw; }
    bool operator<=(FixedPrice o) const noexcept { return raw <= o.raw; }
    bool operator>=(FixedPrice o) const noexcept { return raw >= o.raw; }
};

// ─── Compact Level (same as v2, for SIMD queries) ───────────────────────────

struct alignas(16) CompactLevel {
    FixedPrice price;
    double     qty = 0.0;
};

// ─── Intrusive Doubly-Linked Price Level Node ───────────────────────────────
// 48 bytes: FixedPrice(8) + qty(8) + prev(4) + next(4) + hash_next(4) + flags(4) + pad(16)
// Stored in contiguous pool. Links are indices, not pointers (cache-friendly).

static constexpr uint32_t NULL_IDX = UINT32_MAX;

struct alignas(64) PriceLevelNode {
    FixedPrice price;
    double     qty      = 0.0;
    uint32_t   prev     = NULL_IDX;   // linked list: toward worse price
    uint32_t   next     = NULL_IDX;   // linked list: toward better price
    uint32_t   hash_next = NULL_IDX;  // hash chain (Robin Hood overflow)
    uint32_t   active   = 0;         // 1 = in use, 0 = free
};

// ─── Open-Addressing HashMap ────────────────────────────────────────────────
// Maps FixedPrice → pool index. Robin Hood hashing for low probe distance.
// Capacity must be power of 2 and >= 2x MAX_LEVELS.

template <size_t CAPACITY>
class PriceHashMap {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0, "CAPACITY must be power of 2");

    struct Entry {
        FixedPrice key;
        uint32_t   value = NULL_IDX;  // pool index
        uint32_t   dist  = 0;        // probe distance from ideal slot
    };

public:
    PriceHashMap() noexcept { clear(); }

    void clear() noexcept {
        for (auto& e : entries_) {
            e.value = NULL_IDX;
            e.dist = 0;
        }
        size_ = 0;
    }

    // O(1) amortized lookup. Returns pool index or NULL_IDX.
    uint32_t find(FixedPrice key) const noexcept {
        uint32_t idx = hash(key) & MASK;
        uint32_t dist = 0;

        while (true) {
            const auto& e = entries_[idx];
            if (e.value == NULL_IDX) return NULL_IDX;        // empty slot
            if (__builtin_expect(e.dist < dist, 0)) return NULL_IDX; // Robin Hood: would have been here
            if (e.key == key) return e.value;                // found
            idx = (idx + 1) & MASK;
            ++dist;
        }
    }

    // Insert or update. Returns true if new insertion.
    bool insert(FixedPrice key, uint32_t value) noexcept {
        uint32_t idx = hash(key) & MASK;
        uint32_t dist = 0;
        Entry inserting{key, value, 0};

        while (true) {
            auto& e = entries_[idx];

            if (e.value == NULL_IDX) {
                // Empty slot — insert here
                inserting.dist = dist;
                e = inserting;
                ++size_;
                return true;
            }

            if (e.key == key) {
                // Key exists — update value
                e.value = value;
                return false;
            }

            // Robin Hood: if current entry has shorter probe distance, swap
            if (e.dist < dist) {
                inserting.dist = dist;
                std::swap(e, inserting);
                dist = inserting.dist;
            }

            idx = (idx + 1) & MASK;
            ++dist;
        }
    }

    // Remove key. Returns the old value or NULL_IDX.
    uint32_t remove(FixedPrice key) noexcept {
        uint32_t idx = hash(key) & MASK;
        uint32_t dist = 0;

        while (true) {
            auto& e = entries_[idx];
            if (e.value == NULL_IDX) return NULL_IDX;
            if (e.dist < dist) return NULL_IDX;

            if (e.key == key) {
                uint32_t old_val = e.value;
                // Backward shift deletion (keeps Robin Hood invariant)
                uint32_t cur = idx;
                while (true) {
                    uint32_t nxt = (cur + 1) & MASK;
                    auto& ne = entries_[nxt];
                    if (ne.value == NULL_IDX || ne.dist == 0) {
                        entries_[cur].value = NULL_IDX;
                        entries_[cur].dist = 0;
                        break;
                    }
                    entries_[cur] = ne;
                    entries_[cur].dist--;
                    cur = nxt;
                }
                --size_;
                return old_val;
            }

            idx = (idx + 1) & MASK;
            ++dist;
        }
    }

    size_t size() const noexcept { return size_; }

private:
    static constexpr uint32_t MASK = CAPACITY - 1;

    // Fast hash for FixedPrice: multiply-shift using Fibonacci constant
    static uint32_t hash(FixedPrice p) noexcept {
        uint64_t h = static_cast<uint64_t>(p.raw) * 0x9E3779B97F4A7C15ULL;
        return static_cast<uint32_t>(h >> 32);
    }

    std::array<Entry, CAPACITY> entries_;
    size_t size_ = 0;
};

// ─── Node Pool (Free-list based) ────────────────────────────────────────────
// Fixed-size contiguous pool for PriceLevelNode. O(1) alloc/free.

template <size_t MAX_NODES>
class NodePool {
public:
    NodePool() noexcept { clear(); }

    void clear() noexcept {
        // Build free list
        for (uint32_t i = 0; i < MAX_NODES; ++i) {
            nodes_[i] = PriceLevelNode{};
            nodes_[i].next = i + 1;  // reuse 'next' as free-list link
        }
        nodes_[MAX_NODES - 1].next = NULL_IDX;
        free_head_ = 0;
        count_ = 0;
    }

    uint32_t alloc() noexcept {
        if (__builtin_expect(free_head_ == NULL_IDX, 0)) return NULL_IDX;
        uint32_t idx = free_head_;
        free_head_ = nodes_[idx].next;
        nodes_[idx] = PriceLevelNode{};
        nodes_[idx].active = 1;
        ++count_;
        return idx;
    }

    void free(uint32_t idx) noexcept {
        nodes_[idx].active = 0;
        nodes_[idx].next = free_head_;
        free_head_ = idx;
        --count_;
    }

    PriceLevelNode& operator[](uint32_t idx) noexcept { return nodes_[idx]; }
    const PriceLevelNode& operator[](uint32_t idx) const noexcept { return nodes_[idx]; }

    size_t count() const noexcept { return count_; }

private:
    std::array<PriceLevelNode, MAX_NODES> nodes_;
    uint32_t free_head_ = 0;
    size_t count_ = 0;
};

// ─── OrderBook v3 (experimental / benchmark only) ──────────────────────────

class OrderBookV3 {
    static constexpr size_t MAX_LEVELS = MAX_OB_LEVELS;
    static constexpr size_t HASH_CAP   = 2048; // Must be power of 2, >= 2 * MAX_LEVELS

public:
    OrderBookV3() noexcept { reset(); }

    void reset() noexcept {
        bid_pool_.clear();
        ask_pool_.clear();
        bid_map_.clear();
        ask_map_.clear();
        bid_head_ = NULL_IDX;
        ask_head_ = NULL_IDX;
        bid_count_ = 0;
        ask_count_ = 0;
        seq_id_ = SequenceNumber{0};
        last_update_ns_ = 0;
        prev_best_bid_qty_ = 0.0;
        prev_best_ask_qty_ = 0.0;
        prev_spread_ = 0.0;
        update_count_ = 0;
        book_state_ = BookState::Empty;
    }

    // ─── Invalidation (Stage 3) ─────────────────────────────────────────────

    void invalidate(BookState reason) noexcept {
        book_state_ = reason;
    }

    void mark_pending_resync() noexcept {
        book_state_ = BookState::PendingResync;
    }

    // ─── Snapshot ───────────────────────────────────────────────────────────

    void apply_snapshot(const PriceLevel* bids, size_t bid_n,
                        const PriceLevel* asks, size_t ask_n,
                        uint64_t seq) noexcept {
        apply_snapshot_typed(bids, bid_n, asks, ask_n, SequenceNumber{seq});
    }

    void apply_snapshot_typed(const PriceLevel* bids, size_t bid_n,
                              const PriceLevel* asks, size_t ask_n,
                              SequenceNumber seq) noexcept {
        reset();

        // Sort bids descending by price before inserting
        // (use a temp buffer to avoid modifying caller's data)
        size_t bn = std::min(bid_n, MAX_LEVELS);
        size_t an = std::min(ask_n, MAX_LEVELS);

        // Insert bids: sort descending, then insert in order
        // This builds the linked list in correct order
        struct TempLevel { double price; double qty; };
        TempLevel temp_bids[MAX_OB_LEVELS];
        TempLevel temp_asks[MAX_OB_LEVELS];

        for (size_t i = 0; i < bn; ++i) {
            temp_bids[i] = {bids[i].price, bids[i].qty};
        }
        for (size_t i = 0; i < an; ++i) {
            temp_asks[i] = {asks[i].price, asks[i].qty};
        }

        // Sort bids descending
        std::sort(temp_bids, temp_bids + bn,
                  [](const TempLevel& a, const TempLevel& b) { return a.price > b.price; });
        // Sort asks ascending
        std::sort(temp_asks, temp_asks + an,
                  [](const TempLevel& a, const TempLevel& b) { return a.price < b.price; });

        // Build bid linked list (head = best bid = highest price)
        uint32_t prev_idx = NULL_IDX;
        for (size_t i = 0; i < bn; ++i) {
            uint32_t idx = bid_pool_.alloc();
            if (idx == NULL_IDX) break;

            auto& node = bid_pool_[idx];
            node.price = FixedPrice::from_double(temp_bids[i].price);
            node.qty = temp_bids[i].qty;
            node.prev = prev_idx;
            node.next = NULL_IDX;

            if (prev_idx != NULL_IDX) {
                bid_pool_[prev_idx].next = idx;
            } else {
                bid_head_ = idx; // first = best bid
            }

            bid_map_.insert(node.price, idx);
            prev_idx = idx;
            ++bid_count_;
        }

        // Build ask linked list (head = best ask = lowest price)
        prev_idx = NULL_IDX;
        for (size_t i = 0; i < an; ++i) {
            uint32_t idx = ask_pool_.alloc();
            if (idx == NULL_IDX) break;

            auto& node = ask_pool_[idx];
            node.price = FixedPrice::from_double(temp_asks[i].price);
            node.qty = temp_asks[i].qty;
            node.prev = prev_idx;
            node.next = NULL_IDX;

            if (prev_idx != NULL_IDX) {
                ask_pool_[prev_idx].next = idx;
            } else {
                ask_head_ = idx; // first = best ask
            }

            ask_map_.insert(node.price, idx);
            prev_idx = idx;
            ++ask_count_;
        }

        seq_id_ = seq;
        last_update_ns_ = TscClock::now_ns();
        ++update_count_;
        book_state_ = BookState::Valid;
    }

    // ─── Set BBO (backtesting) ──────────────────────────────────────────────

    void set_bbo(const PriceLevel& bid, const PriceLevel& ask, uint64_t ts) noexcept {
        if (bid_count_ > 0) prev_best_bid_qty_ = bid_pool_[bid_head_].qty;
        if (ask_count_ > 0) prev_best_ask_qty_ = ask_pool_[ask_head_].qty;
        prev_spread_ = spread();

        reset();
        PriceLevel bids[1] = {bid};
        PriceLevel asks[1] = {ask};
        apply_snapshot(bids, 1, asks, 1, 0);
        last_update_ns_ = ts;
    }

    // ─── Delta Update (O(1) lookup + O(1) modify/remove) ───────────────────

    // Legacy bool API — preserved for backward compatibility.
    // Returns true if delta was applied, false otherwise.
    bool apply_delta(const PriceLevel* bids, size_t bid_n,
                     const PriceLevel* asks, size_t ask_n,
                     uint64_t seq) noexcept {
        return apply_delta_typed(bids, bid_n, asks, ask_n,
                                 SequenceNumber{seq}) == DeltaResult::Applied;
    }

    // ─── Strict Delta (Stage 3) ─────────────────────────────────────────────
    // Returns DeltaResult with precise gap/stale/invalid feedback.
    // Gap detection: if seq > expected_seq, book is invalidated.

    DeltaResult apply_delta_typed(const PriceLevel* bids, size_t bid_n,
                                  const PriceLevel* asks, size_t ask_n,
                                  SequenceNumber seq) noexcept {
        if (__builtin_expect(book_state_ != BookState::Valid, 0)) {
            ++md_counters_.deltas_dropped_invalid;
            return DeltaResult::BookNotValid;
        }

        // Stale or duplicate: seq <= current
        if (__builtin_expect(seq <= seq_id_, 0)) {
            if (seq == seq_id_) {
                return DeltaResult::DuplicateSeq;
            }
            ++md_counters_.deltas_stale;
            return DeltaResult::StaleRejected;
        }

        // Gap detection: seq > seq_id_ + 1 means missing deltas
        const SequenceNumber expected = seq_id_ + 1;
        if (__builtin_expect(seq > expected, 0)) {
            last_gap_ = SequenceGapInfo{
                expected, seq,
                seq - expected,
                TscClock::now_ns()
            };
            ++md_counters_.gaps_detected;
            book_state_ = BookState::InvalidGap;
            return DeltaResult::GapDetected;
        }

        // Normal path: seq == seq_id_ + 1
        // Save previous BBO for feature computation
        if (__builtin_expect(bid_count_ > 0, 1))
            prev_best_bid_qty_ = bid_pool_[bid_head_].qty;
        if (__builtin_expect(ask_count_ > 0, 1))
            prev_best_ask_qty_ = ask_pool_[ask_head_].qty;
        prev_spread_ = spread();

        for (size_t i = 0; i < bid_n; ++i) {
            BYBIT_PREFETCH_R(bids + i + 1);
            update_bid(FixedPrice::from_double(bids[i].price), bids[i].qty);
        }
        for (size_t i = 0; i < ask_n; ++i) {
            BYBIT_PREFETCH_R(asks + i + 1);
            update_ask(FixedPrice::from_double(asks[i].price), asks[i].qty);
        }

        seq_id_ = seq;
        last_update_ns_ = TscClock::now_ns();
        ++update_count_;
        ++md_counters_.deltas_applied;
        return DeltaResult::Applied;
    }

    // ─── Accessors (hot path — inlined) ─────────────────────────────────────

    double best_bid() const noexcept {
        return bid_head_ != NULL_IDX ? bid_pool_[bid_head_].price.to_double() : 0.0;
    }
    double best_ask() const noexcept {
        return ask_head_ != NULL_IDX ? ask_pool_[ask_head_].price.to_double() : 0.0;
    }
    double best_bid_qty() const noexcept {
        return bid_head_ != NULL_IDX ? bid_pool_[bid_head_].qty : 0.0;
    }
    double best_ask_qty() const noexcept {
        return ask_head_ != NULL_IDX ? ask_pool_[ask_head_].qty : 0.0;
    }
    double spread() const noexcept {
        return best_ask() - best_bid();
    }
    double mid_price() const noexcept {
        return (best_bid() + best_ask()) * 0.5;
    }

    double microprice() const noexcept {
        double bb = best_bid(), ba = best_ask();
        double bq = best_bid_qty(), aq = best_ask_qty();
        double total = bq + aq;
        if (__builtin_expect(total < 1e-12, 0)) return mid_price();
        return (bb * aq + ba * bq) / total;
    }

    // ─── Imbalance (walk linked list up to depth) ───────────────────────────

    double imbalance(size_t depth) const noexcept {
        double bid_vol = walk_sum_qty(bid_pool_, bid_head_, depth);
        double ask_vol = walk_sum_qty(ask_pool_, ask_head_, depth);
        double total = bid_vol + ask_vol;
        if (__builtin_expect(total < 1e-12, 0)) return 0.0;
        return (bid_vol - ask_vol) / total;
    }

    // ─── VWAP ───────────────────────────────────────────────────────────────

    double vwap(size_t depth) const noexcept {
        double total_pv = 0.0, total_v = 0.0;

        uint32_t idx = bid_head_;
        for (size_t i = 0; i < depth && idx != NULL_IDX; ++i) {
            const auto& n = bid_pool_[idx];
            double p = n.price.to_double();
            total_pv += p * n.qty;
            total_v += n.qty;
            idx = n.next;
        }
        idx = ask_head_;
        for (size_t i = 0; i < depth && idx != NULL_IDX; ++i) {
            const auto& n = ask_pool_[idx];
            double p = n.price.to_double();
            total_pv += p * n.qty;
            total_v += n.qty;
            idx = n.next;
        }
        return total_v > 1e-12 ? total_pv / total_v : mid_price();
    }

    // ─── Liquidity slope ────────────────────────────────────────────────────

    double liquidity_slope(size_t depth) const noexcept {
        double mid = mid_price();
        if (mid < 1e-12) return 0.0;

        double weighted_dist = 0.0, total_qty = 0.0;

        uint32_t idx = bid_head_;
        for (size_t i = 0; i < depth && idx != NULL_IDX; ++i) {
            const auto& n = bid_pool_[idx];
            double dist = mid - n.price.to_double();
            weighted_dist += dist * n.qty;
            total_qty += n.qty;
            idx = n.next;
        }
        idx = ask_head_;
        for (size_t i = 0; i < depth && idx != NULL_IDX; ++i) {
            const auto& n = ask_pool_[idx];
            double dist = n.price.to_double() - mid;
            weighted_dist += dist * n.qty;
            total_qty += n.qty;
            idx = n.next;
        }
        return total_qty > 1e-12 ? weighted_dist / total_qty : 0.0;
    }

    // ─── Cancel spike / spread change ───────────────────────────────────────

    double cancel_spike() const noexcept {
        double bid_drop = prev_best_bid_qty_ > 1e-12
            ? (prev_best_bid_qty_ - best_bid_qty()) / prev_best_bid_qty_ : 0.0;
        double ask_drop = prev_best_ask_qty_ > 1e-12
            ? (prev_best_ask_qty_ - best_ask_qty()) / prev_best_ask_qty_ : 0.0;
        return std::max(bid_drop, ask_drop);
    }

    double spread_change_rate() const noexcept {
        double s = spread();
        if (std::abs(prev_spread_) < 1e-12) return 0.0;
        return (s - prev_spread_) / prev_spread_;
    }

    // ─── Depth analysis ─────────────────────────────────────────────────────

    double total_bid_qty(size_t depth) const noexcept {
        return walk_sum_qty(bid_pool_, bid_head_, depth);
    }

    double total_ask_qty(size_t depth) const noexcept {
        return walk_sum_qty(ask_pool_, ask_head_, depth);
    }

    // ─── State ──────────────────────────────────────────────────────────────

    // Backward-compat: returns true only when book_state_ == Valid
    bool valid() const noexcept { return book_state_ == BookState::Valid; }
    BookState book_state() const noexcept { return book_state_; }
    uint64_t seq_id() const noexcept { return seq_id_.raw(); }
    SequenceNumber seq_number() const noexcept { return seq_id_; }
    SequenceNumber expected_seq() const noexcept { return seq_id_ + 1; }
    uint64_t last_update_ns() const noexcept { return last_update_ns_; }
    uint64_t update_count() const noexcept { return update_count_; }
    size_t bid_count() const noexcept { return bid_count_; }
    size_t ask_count() const noexcept { return ask_count_; }

    // ─── Gap Diagnostics (Stage 3) ──────────────────────────────────────────
    const SequenceGapInfo& last_gap() const noexcept { return last_gap_; }
    const MDIngressCounters& md_counters() const noexcept { return md_counters_; }
    MDIngressCounters& md_counters() noexcept { return md_counters_; }

    // ─── Legacy accessors (bridge compatibility) ────────────────────────────

    const CompactLevel* compact_bids() const noexcept {
        fill_compact_cache(bid_pool_, bid_head_, bid_count_, compact_bids_cache_);
        return compact_bids_cache_.data();
    }

    const CompactLevel* compact_asks() const noexcept {
        fill_compact_cache(ask_pool_, ask_head_, ask_count_, compact_asks_cache_);
        return compact_asks_cache_.data();
    }

    size_t fill_bids(PriceLevel* out, size_t max_n) const noexcept {
        return fill_levels(bid_pool_, bid_head_, out, max_n);
    }

    size_t fill_asks(PriceLevel* out, size_t max_n) const noexcept {
        return fill_levels(ask_pool_, ask_head_, out, max_n);
    }

    const PriceLevel* bids() const noexcept {
        fill_bids(legacy_bids_.data(), bid_count_);
        return legacy_bids_.data();
    }

    const PriceLevel* asks() const noexcept {
        fill_asks(legacy_asks_.data(), ask_count_);
        return legacy_asks_.data();
    }

private:
    // ─── Bid delta update (O(1) lookup, sorted insert if new) ───────────────

    void update_bid(FixedPrice price, double qty) noexcept {
        uint32_t existing = bid_map_.find(price);

        if (existing != NULL_IDX) {
            // O(1) — found in hashmap
            if (__builtin_expect(qty < 1e-12, 0)) {
                // Remove
                unlink_bid(existing);
                bid_map_.remove(price);
                bid_pool_.free(existing);
                --bid_count_;
            } else {
                // Update in place — O(1)
                bid_pool_[existing].qty = qty;
            }
            return;
        }

        if (qty < 1e-12) return; // Remove non-existent — no-op

        // Insert new level: allocate node, find position in linked list
        if (bid_count_ >= MAX_LEVELS) {
            // Drop worst bid (tail of list) if new price is better
            uint32_t worst = find_tail(bid_pool_, bid_head_);
            if (worst != NULL_IDX && price <= bid_pool_[worst].price) return;
            // Remove worst
            unlink_bid(worst);
            bid_map_.remove(bid_pool_[worst].price);
            bid_pool_.free(worst);
            --bid_count_;
        }

        uint32_t new_idx = bid_pool_.alloc();
        if (__builtin_expect(new_idx == NULL_IDX, 0)) return;

        bid_pool_[new_idx].price = price;
        bid_pool_[new_idx].qty = qty;
        bid_pool_[new_idx].prev = NULL_IDX;
        bid_pool_[new_idx].next = NULL_IDX;

        // Insert into sorted linked list (bids: descending by price)
        insert_bid_sorted(new_idx);
        bid_map_.insert(price, new_idx);
        ++bid_count_;
    }

    // ─── Ask delta update ───────────────────────────────────────────────────

    void update_ask(FixedPrice price, double qty) noexcept {
        uint32_t existing = ask_map_.find(price);

        if (existing != NULL_IDX) {
            if (__builtin_expect(qty < 1e-12, 0)) {
                unlink_ask(existing);
                ask_map_.remove(price);
                ask_pool_.free(existing);
                --ask_count_;
            } else {
                ask_pool_[existing].qty = qty;
            }
            return;
        }

        if (qty < 1e-12) return;

        if (ask_count_ >= MAX_LEVELS) {
            uint32_t worst = find_tail(ask_pool_, ask_head_);
            if (worst != NULL_IDX && price >= ask_pool_[worst].price) return;
            unlink_ask(worst);
            ask_map_.remove(ask_pool_[worst].price);
            ask_pool_.free(worst);
            --ask_count_;
        }

        uint32_t new_idx = ask_pool_.alloc();
        if (__builtin_expect(new_idx == NULL_IDX, 0)) return;

        ask_pool_[new_idx].price = price;
        ask_pool_[new_idx].qty = qty;
        ask_pool_[new_idx].prev = NULL_IDX;
        ask_pool_[new_idx].next = NULL_IDX;

        insert_ask_sorted(new_idx);
        ask_map_.insert(price, new_idx);
        ++ask_count_;
    }

    // ─── Linked list operations ─────────────────────────────────────────────

    void unlink_bid(uint32_t idx) noexcept {
        auto& node = bid_pool_[idx];
        if (node.prev != NULL_IDX) bid_pool_[node.prev].next = node.next;
        else bid_head_ = node.next;
        if (node.next != NULL_IDX) bid_pool_[node.next].prev = node.prev;
    }

    void unlink_ask(uint32_t idx) noexcept {
        auto& node = ask_pool_[idx];
        if (node.prev != NULL_IDX) ask_pool_[node.prev].next = node.next;
        else ask_head_ = node.next;
        if (node.next != NULL_IDX) ask_pool_[node.next].prev = node.prev;
    }

    // Insert bid in descending order (head = highest price)
    void insert_bid_sorted(uint32_t new_idx) noexcept {
        FixedPrice price = bid_pool_[new_idx].price;

        if (bid_head_ == NULL_IDX) {
            bid_head_ = new_idx;
            return;
        }

        // Fast path: new best bid
        if (price > bid_pool_[bid_head_].price) {
            bid_pool_[new_idx].next = bid_head_;
            bid_pool_[bid_head_].prev = new_idx;
            bid_head_ = new_idx;
            return;
        }

        // Walk from head to find insertion point
        uint32_t cur = bid_head_;
        while (cur != NULL_IDX) {
            if (price > bid_pool_[cur].price) {
                // Insert before cur
                bid_pool_[new_idx].next = cur;
                bid_pool_[new_idx].prev = bid_pool_[cur].prev;
                if (bid_pool_[cur].prev != NULL_IDX)
                    bid_pool_[bid_pool_[cur].prev].next = new_idx;
                else
                    bid_head_ = new_idx;
                bid_pool_[cur].prev = new_idx;
                return;
            }
            if (bid_pool_[cur].next == NULL_IDX) {
                // Insert at tail
                bid_pool_[cur].next = new_idx;
                bid_pool_[new_idx].prev = cur;
                return;
            }
            cur = bid_pool_[cur].next;
        }
    }

    // Insert ask in ascending order (head = lowest price)
    void insert_ask_sorted(uint32_t new_idx) noexcept {
        FixedPrice price = ask_pool_[new_idx].price;

        if (ask_head_ == NULL_IDX) {
            ask_head_ = new_idx;
            return;
        }

        // Fast path: new best ask
        if (price < ask_pool_[ask_head_].price) {
            ask_pool_[new_idx].next = ask_head_;
            ask_pool_[ask_head_].prev = new_idx;
            ask_head_ = new_idx;
            return;
        }

        // Walk from head
        uint32_t cur = ask_head_;
        while (cur != NULL_IDX) {
            if (price < ask_pool_[cur].price) {
                ask_pool_[new_idx].next = cur;
                ask_pool_[new_idx].prev = ask_pool_[cur].prev;
                if (ask_pool_[cur].prev != NULL_IDX)
                    ask_pool_[ask_pool_[cur].prev].next = new_idx;
                else
                    ask_head_ = new_idx;
                ask_pool_[cur].prev = new_idx;
                return;
            }
            if (ask_pool_[cur].next == NULL_IDX) {
                ask_pool_[cur].next = new_idx;
                ask_pool_[new_idx].prev = cur;
                return;
            }
            cur = ask_pool_[cur].next;
        }
    }

    // Find tail of linked list
    static uint32_t find_tail(const NodePool<MAX_OB_LEVELS>& pool, uint32_t head) noexcept {
        if (head == NULL_IDX) return NULL_IDX;
        uint32_t cur = head;
        while (pool[cur].next != NULL_IDX) cur = pool[cur].next;
        return cur;
    }

    // Walk linked list summing qty
    static double walk_sum_qty(const NodePool<MAX_OB_LEVELS>& pool,
                               uint32_t head, size_t depth) noexcept {
        double sum = 0.0;
        uint32_t idx = head;
        for (size_t i = 0; i < depth && idx != NULL_IDX; ++i) {
            sum += pool[idx].qty;
            idx = pool[idx].next;
        }
        return sum;
    }

    // Fill PriceLevel array from linked list
    static size_t fill_levels(const NodePool<MAX_OB_LEVELS>& pool, uint32_t head,
                              PriceLevel* out, size_t max_n) noexcept {
        uint32_t idx = head;
        size_t i = 0;
        while (i < max_n && idx != NULL_IDX) {
            out[i].price = pool[idx].price.to_double();
            out[i].qty = pool[idx].qty;
            ++i;
            idx = pool[idx].next;
        }
        return i;
    }

    // Fill CompactLevel cache from linked list
    static void fill_compact_cache(const NodePool<MAX_OB_LEVELS>& pool, uint32_t head,
                                   size_t count,
                                   std::array<CompactLevel, MAX_OB_LEVELS>& cache) noexcept {
        uint32_t idx = head;
        for (size_t i = 0; i < count && idx != NULL_IDX; ++i) {
            cache[i].price = pool[idx].price;
            cache[i].qty = pool[idx].qty;
            idx = pool[idx].next;
        }
    }

    // ─── Data ───────────────────────────────────────────────────────────────

    // Node pools for bid and ask levels
    NodePool<MAX_OB_LEVELS> bid_pool_;
    NodePool<MAX_OB_LEVELS> ask_pool_;

    // Hash maps for O(1) price lookup
    PriceHashMap<HASH_CAP> bid_map_;
    PriceHashMap<HASH_CAP> ask_map_;

    // Linked list heads (best prices)
    uint32_t bid_head_ = NULL_IDX;
    uint32_t ask_head_ = NULL_IDX;

    // Counts and state
    size_t   bid_count_ = 0;
    size_t   ask_count_ = 0;
    SequenceNumber seq_id_{0};
    uint64_t last_update_ns_ = 0;
    uint64_t update_count_ = 0;
    double   prev_best_bid_qty_ = 0.0;
    double   prev_best_ask_qty_ = 0.0;
    double   prev_spread_ = 0.0;
    BookState book_state_ = BookState::Empty;

    // Stage 3 diagnostics
    SequenceGapInfo  last_gap_{};
    MDIngressCounters md_counters_{};

    // Legacy compatibility caches
    mutable std::array<CompactLevel, MAX_OB_LEVELS> compact_bids_cache_{};
    mutable std::array<CompactLevel, MAX_OB_LEVELS> compact_asks_cache_{};
    mutable std::array<PriceLevel, MAX_OB_LEVELS>   legacy_bids_{};
    mutable std::array<PriceLevel, MAX_OB_LEVELS>   legacy_asks_{};
};

// E1: Type alias so test files that used `OrderBook` from v3 still compile.
// Production code must NOT include this header — use orderbook.h instead.
using OrderBook = OrderBookV3;

} // namespace bybit
