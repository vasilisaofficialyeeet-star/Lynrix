#pragma once
// ---- Memory Model and Cache Discipline Policy --------------------------------
//
// Formal rules for hot-path data layout:
//
// 1. Every cross-thread atomic variable gets its own 128-byte aligned slot.
// 2. No two independently-written fields share a cache line.
// 3. Every hot-path struct is trivially copyable (for SeqLock/memcpy).
// 4. Every hot-path struct has static_assert on sizeof, alignof, trivially_copyable.
// 5. seq_cst is FORBIDDEN in hot path. Every atomic op has explicit ordering.
// 6. Single-writer rule: every hot-path object has exactly ONE writer thread.
//
// Ownership table (see STAGE1_V2.md Section 5.1 for full specification):
//   OrderBook       -> strategy thread writes, UI reads via SeqLock
//   Features        -> strategy thread only (single-writer)
//   Metrics.ws      -> WS thread writes (relaxed)
//   Metrics.strat   -> strategy thread writes (relaxed)
//   DeferredWork Q  -> strategy pushes (SPSC producer), drain thread pops (consumer)
//   UISnapshot      -> strategy writes (TripleBuffer), UI reads

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <atomic>

namespace bybit {

// ---- Cache Line Constants ---------------------------------------------------

#if defined(__APPLE__) && defined(__aarch64__)
inline constexpr size_t BYBIT_CACHELINE = 128;  // Apple Silicon L1 line
#else
inline constexpr size_t BYBIT_CACHELINE = 64;   // x86-64 L1 line
#endif

inline constexpr size_t BYBIT_PAGE_SIZE = 16384; // Apple Silicon page

// ---- Padding Helpers --------------------------------------------------------

// Pad a struct to fill a complete cache line. Use for cross-thread atomics.
template <typename T>
struct alignas(BYBIT_CACHELINE) CacheLinePadded {
    T value;
    // Padding is implicit from alignas

    CacheLinePadded() noexcept = default;
    explicit CacheLinePadded(T v) noexcept : value(v) {}

    T& operator*() noexcept { return value; }
    const T& operator*() const noexcept { return value; }
    T* operator->() noexcept { return &value; }
    const T* operator->() const noexcept { return &value; }
};

static_assert(sizeof(CacheLinePadded<uint64_t>) == BYBIT_CACHELINE);
static_assert(alignof(CacheLinePadded<uint64_t>) == BYBIT_CACHELINE);

// Explicit padding array for insertion between member groups.
template <size_t N>
struct Padding {
    char _pad[N];
};
using CacheLinePad = Padding<BYBIT_CACHELINE>;

// ---- Layout Verification Macros ---------------------------------------------

// Verify struct size and alignment at compile time.
#define BYBIT_VERIFY_LAYOUT(Type, expected_size, expected_align) \
    static_assert(sizeof(Type) == (expected_size), \
        #Type " size mismatch: expected " #expected_size); \
    static_assert(alignof(Type) >= (expected_align), \
        #Type " alignment too weak: expected >= " #expected_align)

// Verify struct is trivially copyable (required for SeqLock, memcpy, ring buffers).
#define BYBIT_VERIFY_TRIVIAL(Type) \
    static_assert(std::is_trivially_copyable_v<Type>, \
        #Type " must be trivially copyable for lock-free transport")

// Verify struct is cache-line aligned (required for cross-thread structs).
#define BYBIT_VERIFY_CACHELINE(Type) \
    static_assert(alignof(Type) >= BYBIT_CACHELINE, \
        #Type " must be cache-line aligned for cross-thread access"); \
    static_assert(sizeof(Type) % BYBIT_CACHELINE == 0, \
        #Type " size must be a multiple of cache line")

// ---- Atomic Counter with Cache-Line Isolation --------------------------------
// Use this for cross-thread counters (Metrics, etc.).
// Each counter lives on its own cache line. No false sharing.

struct alignas(BYBIT_CACHELINE) IsolatedCounter {
    std::atomic<uint64_t> value{0};

    uint64_t load(std::memory_order order = std::memory_order_relaxed) const noexcept {
        return value.load(order);
    }

    void store(uint64_t v, std::memory_order order = std::memory_order_relaxed) noexcept {
        value.store(v, order);
    }

    uint64_t fetch_add(uint64_t delta, std::memory_order order = std::memory_order_relaxed) noexcept {
        return value.fetch_add(delta, order);
    }

    void increment() noexcept {
        value.fetch_add(1, std::memory_order_relaxed);
    }

    // operator++ returns old value (like fetch_add)
    uint64_t operator++() noexcept {
        return value.fetch_add(1, std::memory_order_relaxed);
    }
};

static_assert(sizeof(IsolatedCounter) == BYBIT_CACHELINE);
static_assert(alignof(IsolatedCounter) == BYBIT_CACHELINE);

// ---- Atomic Double with Cache-Line Isolation --------------------------------
// For cross-thread floating-point metrics (latency percentiles, etc.).

struct alignas(BYBIT_CACHELINE) IsolatedAtomicDouble {
    std::atomic<double> value{0.0};

    double load(std::memory_order order = std::memory_order_relaxed) const noexcept {
        return value.load(order);
    }

    void store(double v, std::memory_order order = std::memory_order_relaxed) noexcept {
        value.store(v, order);
    }
};

static_assert(sizeof(IsolatedAtomicDouble) == BYBIT_CACHELINE);

// ---- Ownership Annotations --------------------------------------------------
// Documentation markers. No runtime cost. Useful for code review and static analysis.

// Marks a member as written by exactly one thread.
#define BYBIT_SINGLE_WRITER /* thread: <name> */

// Marks a member as read-only after initialization.
#define BYBIT_IMMUTABLE

// Marks a member as accessed cross-thread (requires atomic or SeqLock).
#define BYBIT_CROSS_THREAD

} // namespace bybit
