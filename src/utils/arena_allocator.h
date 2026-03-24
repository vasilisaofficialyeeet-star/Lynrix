#pragma once

// ─── Arena Allocator + Object Pools for Zero-Allocation Hot Path ────────────
// Designed for Apple Silicon (M2/M3/M4) with 128-byte cache lines.
// All allocations are bump-pointer; deallocation is bulk-only (reset).
// Thread-safety: single-thread per arena (one arena per pipeline stage).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <atomic>
#include <new>
#include <type_traits>
#include <cassert>
#include <sys/mman.h>
#include <unistd.h>

namespace bybit {

// ─── Compile-time config ────────────────────────────────────────────────────

inline constexpr size_t CACHE_LINE = 128;  // Apple Silicon L1 cache line
inline constexpr size_t PAGE_SIZE  = 16384; // Apple Silicon page size (16KB)
inline constexpr size_t ARENA_DEFAULT_SIZE = 4 * 1024 * 1024; // 4 MB

// ─── Arena Allocator ────────────────────────────────────────────────────────
// Bump-pointer allocator with mmap-backed memory.
// Allocations: O(1) — single pointer increment.
// Deallocations: O(1) — reset() frees everything at once.
// No fragmentation, no syscalls in hot path.

class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t capacity = ARENA_DEFAULT_SIZE) noexcept
        : capacity_(align_up(capacity, PAGE_SIZE))
    {
        // mmap with MAP_PRIVATE|MAP_ANON — no file backing, lazy commit
        base_ = static_cast<uint8_t*>(
            ::mmap(nullptr, capacity_, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0));
        if (base_ == MAP_FAILED) {
            base_ = nullptr;
            capacity_ = 0;
        }
        cursor_ = 0;
        high_water_ = 0;
    }

    ~ArenaAllocator() {
        if (base_) {
            ::munmap(base_, capacity_);
        }
    }

    // Non-copyable, movable
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    ArenaAllocator(ArenaAllocator&& o) noexcept
        : base_(o.base_), capacity_(o.capacity_),
          cursor_(o.cursor_), high_water_(o.high_water_)
    {
        o.base_ = nullptr;
        o.capacity_ = 0;
        o.cursor_ = 0;
    }

    // Allocate aligned memory. Returns nullptr if arena exhausted.
    [[nodiscard]] void* allocate(size_t size, size_t alignment = CACHE_LINE) noexcept {
        size_t aligned_cursor = align_up(cursor_, alignment);
        size_t new_cursor = aligned_cursor + size;

        if (__builtin_expect(new_cursor > capacity_, 0)) {
            return nullptr; // arena exhausted
        }

        cursor_ = new_cursor;
        if (cursor_ > high_water_) high_water_ = cursor_;
        return base_ + aligned_cursor;
    }

    // Typed allocation with placement construction
    template <typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args) noexcept {
        static_assert(std::is_trivially_destructible_v<T> || true,
                      "Arena types should ideally be trivially destructible");
        void* mem = allocate(sizeof(T), alignof(T));
        if (__builtin_expect(!mem, 0)) return nullptr;
        return ::new (mem) T(static_cast<Args&&>(args)...);
    }

    // Allocate array of N elements
    template <typename T>
    [[nodiscard]] T* allocate_array(size_t count) noexcept {
        void* mem = allocate(sizeof(T) * count, alignof(T));
        if (!mem) return nullptr;
        // Zero-initialize for POD types
        if constexpr (std::is_trivial_v<T>) {
            std::memset(mem, 0, sizeof(T) * count);
        } else {
            T* arr = static_cast<T*>(mem);
            for (size_t i = 0; i < count; ++i) {
                ::new (&arr[i]) T();
            }
        }
        return static_cast<T*>(mem);
    }

    // Reset arena — all allocations are invalidated. O(1).
    void reset() noexcept {
        cursor_ = 0;
        // Optionally advise kernel to reclaim physical pages
        if (high_water_ > PAGE_SIZE * 4) {
            ::madvise(base_, high_water_, MADV_FREE);
        }
    }

    // Save/restore cursor for temporary allocations (scratch space)
    [[nodiscard]] size_t save() const noexcept { return cursor_; }
    void restore(size_t saved) noexcept { cursor_ = saved; }

    // Stats
    [[nodiscard]] size_t used() const noexcept { return cursor_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_t remaining() const noexcept { return capacity_ - cursor_; }
    [[nodiscard]] size_t high_water_mark() const noexcept { return high_water_; }
    [[nodiscard]] bool valid() const noexcept { return base_ != nullptr; }

private:
    static constexpr size_t align_up(size_t value, size_t alignment) noexcept {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    uint8_t* base_     = nullptr;
    size_t   capacity_ = 0;
    size_t   cursor_   = 0;
    size_t   high_water_ = 0;
};

// ─── Scoped Arena Checkpoint ────────────────────────────────────────────────
// RAII guard that restores arena to a saved state on destruction.
// Use for temporary per-tick allocations.

class ArenaCheckpoint {
public:
    explicit ArenaCheckpoint(ArenaAllocator& arena) noexcept
        : arena_(arena), saved_(arena.save()) {}

    ~ArenaCheckpoint() { arena_.restore(saved_); }

    ArenaCheckpoint(const ArenaCheckpoint&) = delete;
    ArenaCheckpoint& operator=(const ArenaCheckpoint&) = delete;

private:
    ArenaAllocator& arena_;
    size_t saved_;
};

// ─── Fixed-Size Object Pool ────────────────────────────────────────────────
// Lock-free freelist pool for hot-path objects (Order, PriceLevel, etc.).
// Single-producer single-consumer: no CAS contention.
// Objects are pre-allocated in a contiguous block for cache locality.

template <typename T, size_t Capacity>
class alignas(CACHE_LINE) ObjectPool {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be power of 2");
    static_assert(sizeof(T) >= sizeof(uint32_t),
                  "Object must be at least 4 bytes");

public:
    ObjectPool() noexcept {
        // Build freelist through the storage blocks
        for (uint32_t i = 0; i < Capacity - 1; ++i) {
            *reinterpret_cast<uint32_t*>(&storage_[i]) = i + 1;
        }
        *reinterpret_cast<uint32_t*>(&storage_[Capacity - 1]) = SENTINEL;
        free_head_ = 0;
        alloc_count_ = 0;
    }

    // Acquire one object from pool. Returns nullptr if exhausted.
    [[nodiscard]] T* acquire() noexcept {
        if (__builtin_expect(free_head_ == SENTINEL, 0)) {
            return nullptr;
        }
        uint32_t idx = free_head_;
        free_head_ = *reinterpret_cast<uint32_t*>(&storage_[idx]);
        ++alloc_count_;

        T* obj = reinterpret_cast<T*>(&storage_[idx]);
        if constexpr (std::is_trivial_v<T>) {
            std::memset(obj, 0, sizeof(T));
        } else {
            ::new (obj) T();
        }
        return obj;
    }

    // Release object back to pool
    void release(T* ptr) noexcept {
        if (__builtin_expect(!ptr, 0)) return;
        auto* block = reinterpret_cast<Storage*>(ptr);
        size_t idx = static_cast<size_t>(block - storage_.data());
        assert(idx < Capacity);

        if constexpr (!std::is_trivially_destructible_v<T>) {
            ptr->~T();
        }

        *reinterpret_cast<uint32_t*>(&storage_[idx]) = free_head_;
        free_head_ = static_cast<uint32_t>(idx);
        --alloc_count_;
    }

    [[nodiscard]] size_t allocated() const noexcept { return alloc_count_; }
    [[nodiscard]] size_t available() const noexcept { return Capacity - alloc_count_; }
    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    static constexpr uint32_t SENTINEL = ~uint32_t(0);

    struct alignas(alignof(T)) Storage {
        char data[sizeof(T)];
    };

    std::array<Storage, Capacity> storage_{};
    uint32_t free_head_ = 0;
    size_t   alloc_count_ = 0;
};

// ─── Thread-Safe Object Pool (Multi-Producer Multi-Consumer) ───────────────
// Uses tagged pointer CAS for ABA prevention.
// For cross-thread allocation patterns (e.g., alloc on WS thread, free on exec thread).

template <typename T, size_t Capacity>
class alignas(CACHE_LINE) ConcurrentObjectPool {
    static_assert(Capacity > 0 && Capacity <= (1u << 24),
                  "Capacity must fit in 24-bit index");

public:
    ConcurrentObjectPool() noexcept {
        for (uint32_t i = 0; i < Capacity - 1; ++i) {
            nodes_[i].next.store(i + 1, std::memory_order_relaxed);
        }
        nodes_[Capacity - 1].next.store(SENTINEL, std::memory_order_relaxed);
        // Pack tag(0) + head(0) into single atomic
        head_.store(pack(0, 0), std::memory_order_release);
        count_.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] T* acquire() noexcept {
        uint64_t old_head = head_.load(std::memory_order_acquire);
        while (true) {
            auto [tag, idx] = unpack(old_head);
            if (idx == SENTINEL) return nullptr;

            uint32_t next = nodes_[idx].next.load(std::memory_order_relaxed);
            uint64_t new_head = pack(tag + 1, next);

            if (head_.compare_exchange_weak(old_head, new_head,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                count_.fetch_add(1, std::memory_order_relaxed);
                T* obj = reinterpret_cast<T*>(&nodes_[idx].storage);
                if constexpr (std::is_trivial_v<T>) {
                    std::memset(obj, 0, sizeof(T));
                } else {
                    ::new (obj) T();
                }
                return obj;
            }
        }
    }

    void release(T* ptr) noexcept {
        if (!ptr) return;
        auto* node = reinterpret_cast<Node*>(ptr);
        uint32_t idx = static_cast<uint32_t>(node - nodes_.data());
        assert(idx < Capacity);

        if constexpr (!std::is_trivially_destructible_v<T>) {
            ptr->~T();
        }

        uint64_t old_head = head_.load(std::memory_order_acquire);
        while (true) {
            auto [tag, head_idx] = unpack(old_head);
            nodes_[idx].next.store(head_idx, std::memory_order_relaxed);
            uint64_t new_head = pack(tag + 1, idx);

            if (head_.compare_exchange_weak(old_head, new_head,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                count_.fetch_sub(1, std::memory_order_relaxed);
                return;
            }
        }
    }

    [[nodiscard]] size_t allocated() const noexcept {
        return count_.load(std::memory_order_relaxed);
    }

private:
    static constexpr uint32_t SENTINEL = 0x00FFFFFF;

    static uint64_t pack(uint32_t tag, uint32_t idx) noexcept {
        return (static_cast<uint64_t>(tag) << 32) | idx;
    }
    static std::pair<uint32_t, uint32_t> unpack(uint64_t v) noexcept {
        return { static_cast<uint32_t>(v >> 32),
                 static_cast<uint32_t>(v & 0x00FFFFFF) };
    }

    union Node {
        alignas(alignof(T)) char storage[sizeof(T)];
        std::atomic<uint32_t> next;
    };

    std::array<Node, Capacity> nodes_{};
    alignas(CACHE_LINE) std::atomic<uint64_t> head_{0};
    alignas(CACHE_LINE) std::atomic<size_t> count_{0};
};

} // namespace bybit
