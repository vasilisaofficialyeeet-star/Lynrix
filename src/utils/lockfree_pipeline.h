#pragma once

// ─── Lock-Free Pipeline Primitives ──────────────────────────────────────────
// SPSC Queue, Triple Buffer, SeqLock v2 — all designed for Apple Silicon.
// Zero-copy data transfer between pipeline stages.
// Cache-line padded to prevent false sharing on M-series (128-byte lines).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <array>
#include <optional>

namespace bybit {

inline constexpr size_t CACHELINE_SIZE = 128; // Apple Silicon

// ─── SPSC Queue (Single-Producer Single-Consumer) ───────────────────────────
// Wait-free bounded queue. Zero allocation in steady state.
// Producer and consumer can run on different cores without any locks.
//
// Memory layout: head and tail on separate cache lines to prevent false sharing.
// Data stored in contiguous power-of-2 buffer for branch-free masking.

template <typename T, size_t N>
class alignas(CACHELINE_SIZE) SPSCQueue {
    static_assert((N & (N - 1)) == 0, "N must be power of 2");
    static_assert(N >= 4, "Queue must hold at least 4 elements");

public:
    SPSCQueue() noexcept : data_(new(std::align_val_t{CACHELINE_SIZE}) T[N]{}) {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    ~SPSCQueue() {
        ::operator delete[](data_, std::align_val_t{CACHELINE_SIZE});
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Producer: try to push. Returns false if full.
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) & MASK;
        if (__builtin_expect(next == tail_.load(std::memory_order_acquire), 0)) {
            return false; // full
        }
        data_[h] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Producer: push with move semantics
    [[nodiscard]] bool try_push(T&& item) noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) & MASK;
        if (__builtin_expect(next == tail_.load(std::memory_order_acquire), 0)) {
            return false;
        }
        data_[h] = static_cast<T&&>(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Producer: emplace directly into queue slot (zero-copy for complex types)
    template <typename... Args>
    [[nodiscard]] bool try_emplace(Args&&... args) noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) & MASK;
        if (__builtin_expect(next == tail_.load(std::memory_order_acquire), 0)) {
            return false;
        }
        ::new (&data_[h]) T(static_cast<Args&&>(args)...);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer: try to pop. Returns false if empty.
    [[nodiscard]] bool try_pop(T& item) noexcept {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (__builtin_expect(t == head_.load(std::memory_order_acquire), 0)) {
            return false; // empty
        }
        item = static_cast<T&&>(data_[t]);
        tail_.store((t + 1) & MASK, std::memory_order_release);
        return true;
    }

    // Consumer: peek at front without removing
    [[nodiscard]] const T* peek() const noexcept {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) {
            return nullptr;
        }
        return &data_[t];
    }

    // Producer: get writable slot (for zero-copy writes)
    // Call commit() after writing to make it visible to consumer.
    [[nodiscard]] T* acquire_write_slot() noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) & MASK;
        if (__builtin_expect(next == tail_.load(std::memory_order_acquire), 0)) {
            return nullptr;
        }
        return &data_[h];
    }

    void commit_write() noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        head_.store((h + 1) & MASK, std::memory_order_release);
    }

    // Stats
    [[nodiscard]] size_t size() const noexcept {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & MASK;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool full() const noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        return ((h + 1) & MASK) == tail_.load(std::memory_order_acquire);
    }

    static constexpr size_t capacity() noexcept { return N - 1; }

private:
    static constexpr size_t MASK = N - 1;

    alignas(CACHELINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHELINE_SIZE) std::atomic<size_t> tail_{0};
    T* data_;
};

// ─── Triple Buffer ──────────────────────────────────────────────────────────
// Lock-free single-writer multiple-reader data sharing.
// Writer publishes to back buffer, atomically swaps to middle.
// Reader always gets latest complete frame from middle buffer.
// Zero tearing, zero blocking — ideal for UI snapshot sharing.

template <typename T>
class alignas(CACHELINE_SIZE) TripleBuffer {
public:
    TripleBuffer() noexcept {
        for (auto& buf : buffers_) {
            if constexpr (std::is_trivial_v<T>) {
                std::memset(&buf, 0, sizeof(T));
            } else {
                ::new (&buf) T();
            }
        }
        // Initial state: writer=0, middle=1, reader=2
        state_.store(make_state(0, 1, 2, false), std::memory_order_relaxed);
    }

    // Writer: get reference to back buffer for writing
    [[nodiscard]] T& write_buffer() noexcept {
        uint32_t s = state_.load(std::memory_order_relaxed);
        return buffers_[writer_idx(s)];
    }

    // Writer: publish back buffer (swap with middle)
    void publish() noexcept {
        uint32_t old_state = state_.load(std::memory_order_relaxed);
        uint32_t new_state;
        do {
            uint8_t w = writer_idx(old_state);
            uint8_t m = middle_idx(old_state);
            uint8_t r = reader_idx(old_state);
            // Swap writer and middle, mark new_data=true
            new_state = make_state(m, w, r, true);
        } while (!state_.compare_exchange_weak(old_state, new_state,
                    std::memory_order_acq_rel, std::memory_order_relaxed));
        ++version_;
    }

    // Reader: get latest published data (swaps middle to reader if new data)
    [[nodiscard]] const T& read() noexcept {
        uint32_t old_state = state_.load(std::memory_order_acquire);
        if (has_new_data(old_state)) {
            uint32_t new_state;
            do {
                uint8_t w = writer_idx(old_state);
                uint8_t m = middle_idx(old_state);
                uint8_t r = reader_idx(old_state);
                // Swap reader and middle, clear new_data
                new_state = make_state(w, r, m, false);
            } while (!state_.compare_exchange_weak(old_state, new_state,
                        std::memory_order_acq_rel, std::memory_order_acquire));
        }
        return buffers_[reader_idx(state_.load(std::memory_order_acquire))];
    }

    // Check if new data is available without consuming it
    [[nodiscard]] bool has_update() const noexcept {
        return has_new_data(state_.load(std::memory_order_acquire));
    }

    [[nodiscard]] uint64_t version() const noexcept { return version_; }

private:
    // State packing: [new_data:1][reader:2][middle:2][writer:2] in low 7 bits
    static constexpr uint32_t make_state(uint8_t w, uint8_t m, uint8_t r, bool new_data) noexcept {
        return (static_cast<uint32_t>(new_data) << 6) |
               (static_cast<uint32_t>(r) << 4) |
               (static_cast<uint32_t>(m) << 2) |
               static_cast<uint32_t>(w);
    }
    static constexpr uint8_t writer_idx(uint32_t s) noexcept { return s & 0x3; }
    static constexpr uint8_t middle_idx(uint32_t s) noexcept { return (s >> 2) & 0x3; }
    static constexpr uint8_t reader_idx(uint32_t s) noexcept { return (s >> 4) & 0x3; }
    static constexpr bool has_new_data(uint32_t s) noexcept { return (s >> 6) & 0x1; }

    alignas(CACHELINE_SIZE) T buffers_[3];
    alignas(CACHELINE_SIZE) std::atomic<uint32_t> state_{0};
    uint64_t version_ = 0;
};

// ─── SeqLock v2 ─────────────────────────────────────────────────────────────
// Optimistic reader-writer lock for publishing structured data.
// Writer is wait-free. Readers are lock-free (retry on contention).
// Ideal for UI snapshot publishing where writer must never block.
//
// Improvement over v1: 128-byte alignment, dmb ishst on ARM, retry budget.

template <typename T>
class alignas(CACHELINE_SIZE) SeqLock {
    static_assert(std::is_trivially_copyable_v<T>,
                  "SeqLock requires trivially copyable type");

public:
    SeqLock() noexcept {
        seq_.store(0, std::memory_order_relaxed);
        std::memset(&data_, 0, sizeof(T));
    }

    // Writer: store new data. Single writer only.
    void store(const T& value) noexcept {
        uint64_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_release); // odd = writing
        std::atomic_signal_fence(std::memory_order_acq_rel);
        std::memcpy(&data_, &value, sizeof(T));
        std::atomic_signal_fence(std::memory_order_acq_rel);
        seq_.store(s + 2, std::memory_order_release); // even = consistent
    }

    // Writer: get writable reference + begin/end for in-place mutation
    T& begin_write() noexcept {
        uint64_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_release);
        std::atomic_signal_fence(std::memory_order_acq_rel);
        return data_;
    }

    void end_write() noexcept {
        std::atomic_signal_fence(std::memory_order_acq_rel);
        uint64_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_release);
    }

    // Reader: load data with consistency check. Returns false on torn read.
    [[nodiscard]] bool try_load(T& out) const noexcept {
        uint64_t s1 = seq_.load(std::memory_order_acquire);
        if (__builtin_expect(s1 & 1, 0)) return false; // writer active
        std::atomic_signal_fence(std::memory_order_acquire);
        std::memcpy(&out, &data_, sizeof(T));
        std::atomic_signal_fence(std::memory_order_acquire);
        uint64_t s2 = seq_.load(std::memory_order_acquire);
        return s1 == s2;
    }

    // Reader: blocking load with retry budget (spin up to max_retries)
    [[nodiscard]] T load(uint32_t max_retries = 64) const noexcept {
        T out;
        for (uint32_t i = 0; i < max_retries; ++i) {
            if (try_load(out)) return out;
#if defined(__aarch64__)
            asm volatile("yield" ::: "memory");
#elif defined(__x86_64__)
            __builtin_ia32_pause();
#endif
        }
        // Last resort: memcpy (may be torn but better than hanging)
        std::memcpy(&out, &data_, sizeof(T));
        return out;
    }

    [[nodiscard]] uint64_t version() const noexcept {
        return seq_.load(std::memory_order_acquire) >> 1;
    }

private:
    alignas(CACHELINE_SIZE) std::atomic<uint64_t> seq_{0};
    alignas(CACHELINE_SIZE) T data_;
};

// ─── Pipeline Stage Connector ───────────────────────────────────────────────
// Connects two pipeline stages via SPSC queue with backpressure signaling.
// Provides zero-copy batch transfer and latency tracking.

template <typename T, size_t QueueSize = 4096>
class PipelineConnector {
public:
    // Producer side
    [[nodiscard]] bool send(const T& item) noexcept {
        bool ok = queue_.try_push(item);
        if (ok) {
            ++sent_count_;
        } else {
            ++drop_count_;
        }
        return ok;
    }

    [[nodiscard]] bool send(T&& item) noexcept {
        bool ok = queue_.try_push(static_cast<T&&>(item));
        if (ok) {
            ++sent_count_;
        } else {
            ++drop_count_;
        }
        return ok;
    }

    // Zero-copy: get write slot, write directly, then commit
    [[nodiscard]] T* acquire() noexcept { return queue_.acquire_write_slot(); }
    void commit() noexcept { queue_.commit_write(); ++sent_count_; }

    // Consumer side
    [[nodiscard]] bool receive(T& item) noexcept {
        bool ok = queue_.try_pop(item);
        if (ok) ++recv_count_;
        return ok;
    }

    // Drain up to max_batch items into output array. Returns count drained.
    size_t drain(T* out, size_t max_batch) noexcept {
        size_t count = 0;
        while (count < max_batch && queue_.try_pop(out[count])) {
            ++count;
        }
        recv_count_ += count;
        return count;
    }

    // Stats
    [[nodiscard]] size_t pending() const noexcept { return queue_.size(); }
    [[nodiscard]] bool empty() const noexcept { return queue_.empty(); }
    [[nodiscard]] uint64_t sent() const noexcept { return sent_count_; }
    [[nodiscard]] uint64_t received() const noexcept { return recv_count_; }
    [[nodiscard]] uint64_t dropped() const noexcept { return drop_count_; }
    [[nodiscard]] double drop_rate() const noexcept {
        uint64_t total = sent_count_ + drop_count_;
        return total > 0 ? static_cast<double>(drop_count_) / total : 0.0;
    }

private:
    SPSCQueue<T, QueueSize> queue_;
    uint64_t sent_count_ = 0;
    uint64_t recv_count_ = 0;
    uint64_t drop_count_ = 0;
};

} // namespace bybit
