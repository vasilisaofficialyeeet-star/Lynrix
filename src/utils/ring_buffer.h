#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>

namespace bybit {

// Single-producer single-consumer lock-free ring buffer.
// Size must be power of 2.
// Buffer is heap-allocated to avoid stack overflow with large T.
template <typename T, size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "N must be power of 2");

public:
    RingBuffer() : head_(0), tail_(0), data_(new T[N]{}) {}

    bool push(const T& item) noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) & MASK;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        data_[h] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) noexcept {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        item = data_[t];
        tail_.store((t + 1) & MASK, std::memory_order_release);
        return true;
    }

    size_t size() const noexcept {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & MASK;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    void clear() noexcept {
        head_.store(0, std::memory_order_release);
        tail_.store(0, std::memory_order_release);
    }

private:
    static constexpr size_t MASK = N - 1;
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    std::unique_ptr<T[]> data_;
};

} // namespace bybit
