#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

namespace bybit {

// Fixed-size memory pool for zero-allocation hot-path usage.
// Thread-safe via lock-free free-list.
// T must be trivially destructible for best performance.
template <typename T, size_t Capacity>
class MemoryPool {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be power of 2");
public:
    MemoryPool() noexcept {
        // Initialize free list: each slot points to the next
        for (size_t i = 0; i < Capacity - 1; ++i) {
            nodes_[i].next.store(i + 1, std::memory_order_relaxed);
        }
        nodes_[Capacity - 1].next.store(NONE, std::memory_order_relaxed);
        free_head_.store(0, std::memory_order_release);
        alloc_count_.store(0, std::memory_order_relaxed);
    }

    // Allocate one T from the pool. Returns nullptr if exhausted.
    T* allocate() noexcept {
        size_t head = free_head_.load(std::memory_order_acquire);
        while (head != NONE) {
            size_t next = nodes_[head].next.load(std::memory_order_relaxed);
            if (free_head_.compare_exchange_weak(head, next,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                alloc_count_.fetch_add(1, std::memory_order_relaxed);
                return reinterpret_cast<T*>(&nodes_[head].storage);
            }
        }
        return nullptr; // pool exhausted
    }

    // Return a previously allocated T back to the pool.
    void deallocate(T* ptr) noexcept {
        if (!ptr) return;
        auto* node = reinterpret_cast<Node*>(ptr);
        size_t idx = static_cast<size_t>(node - nodes_.data());
        if (idx >= Capacity) return; // invalid pointer

        size_t head = free_head_.load(std::memory_order_acquire);
        do {
            nodes_[idx].next.store(head, std::memory_order_relaxed);
        } while (!free_head_.compare_exchange_weak(head, idx,
                    std::memory_order_acq_rel, std::memory_order_acquire));
        alloc_count_.fetch_sub(1, std::memory_order_relaxed);
    }

    size_t allocated() const noexcept {
        return alloc_count_.load(std::memory_order_relaxed);
    }

    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    static constexpr size_t NONE = ~size_t(0);

    union Node {
        alignas(T) char storage[sizeof(T)];
        std::atomic<size_t> next;
    };

    std::array<Node, Capacity> nodes_{};
    alignas(64) std::atomic<size_t> free_head_{0};
    alignas(64) std::atomic<size_t> alloc_count_{0};
};

// ─── Scoped pool allocation helper ──────────────────────────────────────────

template <typename T, size_t Cap>
class PoolPtr {
public:
    PoolPtr(MemoryPool<T, Cap>& pool) noexcept : pool_(pool), ptr_(pool.allocate()) {}
    ~PoolPtr() { if (ptr_) pool_.deallocate(ptr_); }

    PoolPtr(const PoolPtr&) = delete;
    PoolPtr& operator=(const PoolPtr&) = delete;
    PoolPtr(PoolPtr&& o) noexcept : pool_(o.pool_), ptr_(o.ptr_) { o.ptr_ = nullptr; }

    T* get() noexcept { return ptr_; }
    const T* get() const noexcept { return ptr_; }
    T* operator->() noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    T* release() noexcept { auto* p = ptr_; ptr_ = nullptr; return p; }

private:
    MemoryPool<T, Cap>& pool_;
    T* ptr_;
};

} // namespace bybit
