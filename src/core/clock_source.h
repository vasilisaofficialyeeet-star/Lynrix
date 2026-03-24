#pragma once
// ---- Clock Sources for Dual-Mode Determinism --------------------------------
//
// Primary design: STATIC DISPATCH via template parameter.
// TscClockSource is inlined in production. Zero overhead.
// ReplayClockSource is injected for replay. One TU boundary, no vtable.
//
// For legacy callers that cannot be templatized, ClockFn provides a
// type-erased function-pointer wrapper (8 bytes, one indirection, no vtable).
//
// No IClock virtual base class in the primary design.
// No global singleton. No g_clock. Pass clock explicitly.
//
// Usage:
//   TscClockSource clock;                     // production
//   auto now = clock.now();                   // inlined TSC read
//
//   ReplayClockSource replay;                 // replay mode
//   replay.advance_to(TimestampNs{12345});
//   auto now = replay.now();                  // returns stored value
//
//   ClockFn fn = ClockFn::from<TscClockSource>(); // type-erased
//   auto now = fn.now();                           // one indirection

#include "strong_types.h"
#include "../utils/tsc_clock.h"

#include <atomic>
#include <cstdint>

namespace bybit {

// ---- TscClockSource (Production) --------------------------------------------
// Inlineable. Zero overhead after LTO. This is the default for all hot paths.

struct TscClockSource {
    TimestampNs now() const noexcept {
        return TimestampNs{TscClock::now_ns()};
    }

    TscTicks now_ticks() const noexcept {
        return TscTicks{TscClock::now()};
    }

    DurationNs elapsed(TscTicks start) const noexcept {
        return DurationNs{static_cast<int64_t>(TscClock::elapsed_ns(start.raw()))};
    }

    // Convenience: milliseconds for cold-path / wall-clock needs
    uint64_t wall_ms() const noexcept {
        return TscClock::now_ns() / 1'000'000ULL;
    }
};

// ---- ReplayClockSource ------------------------------------------------------
// Externally driven. Thread-safe (atomic). Used by ReplayEngine.
// advance_to() is called before each event injection.

struct ReplayClockSource {
    ReplayClockSource() noexcept = default;
    explicit ReplayClockSource(TimestampNs initial) noexcept : current_ns_(initial.raw()) {}

    TimestampNs now() const noexcept {
        return TimestampNs{current_ns_.load(std::memory_order_acquire)};
    }

    TscTicks now_ticks() const noexcept {
        return TscTicks{current_ns_.load(std::memory_order_acquire)};
    }

    DurationNs elapsed(TscTicks start) const noexcept {
        uint64_t cur = current_ns_.load(std::memory_order_acquire);
        return DurationNs{static_cast<int64_t>(cur) - static_cast<int64_t>(start.raw())};
    }

    uint64_t wall_ms() const noexcept {
        return current_ns_.load(std::memory_order_acquire) / 1'000'000ULL;
    }

    // Replay control
    void advance_to(TimestampNs ns) noexcept {
        current_ns_.store(ns.raw(), std::memory_order_release);
    }

    void advance_by(DurationNs delta) noexcept {
        current_ns_.fetch_add(static_cast<uint64_t>(delta.raw()), std::memory_order_acq_rel);
    }

    void reset(TimestampNs ns = TimestampNs{0}) noexcept {
        current_ns_.store(ns.raw(), std::memory_order_release);
    }

    TimestampNs current() const noexcept {
        return TimestampNs{current_ns_.load(std::memory_order_relaxed)};
    }

private:
    std::atomic<uint64_t> current_ns_{0};
};

// ---- MockClockSource (Testing) ----------------------------------------------
// Non-atomic. Single-threaded tests only. Simpler and faster than ReplayClockSource.

struct MockClockSource {
    MockClockSource() noexcept = default;
    explicit MockClockSource(TimestampNs initial) noexcept : current_ns_(initial.raw()) {}

    TimestampNs now() const noexcept { return TimestampNs{current_ns_}; }

    TscTicks now_ticks() const noexcept { return TscTicks{current_ns_}; }

    DurationNs elapsed(TscTicks start) const noexcept {
        return DurationNs{static_cast<int64_t>(current_ns_) - static_cast<int64_t>(start.raw())};
    }

    uint64_t wall_ms() const noexcept { return current_ns_ / 1'000'000ULL; }

    // Test control
    void set(TimestampNs ns) noexcept { current_ns_ = ns.raw(); }

    void advance(DurationNs delta) noexcept {
        current_ns_ = static_cast<uint64_t>(static_cast<int64_t>(current_ns_) + delta.raw());
    }

private:
    uint64_t current_ns_ = 0;
};

// ---- ClockFn (Type-Erased Function Pointer) ---------------------------------
// For legacy callers that cannot be templatized.
// 16 bytes (fn ptr + context). One indirect call (~2ns). No vtable.
//
// Usage:
//   ClockFn fn = ClockFn::from_tsc();
//   TimestampNs t = fn.now();

struct ClockFn {
    using NowFn = uint64_t(*)(void*) noexcept;

    NowFn  now_fn;
    void*  ctx;

    TimestampNs now() const noexcept {
        return TimestampNs{now_fn(ctx)};
    }

    // Factory: production TSC clock
    static ClockFn from_tsc() noexcept {
        return ClockFn{
            [](void*) noexcept -> uint64_t { return TscClock::now_ns(); },
            nullptr
        };
    }

    // Factory: from any clock source with a now() returning TimestampNs.
    // Caller must ensure Source outlives ClockFn.
    template <typename Source>
    static ClockFn from(Source& src) noexcept {
        return ClockFn{
            [](void* p) noexcept -> uint64_t {
                return static_cast<Source*>(p)->now().raw();
            },
            static_cast<void*>(&src)
        };
    }
};

static_assert(sizeof(ClockFn) == 2 * sizeof(void*), "ClockFn: fn pointer + context");

// ---- Clock concept (C++20) --------------------------------------------------
// Constrains template parameters. Any clock source must satisfy this.

template <typename T>
concept ClockSourceConcept = requires(const T& c, TscTicks t) {
    { c.now() } noexcept -> std::same_as<TimestampNs>;
    { c.now_ticks() } noexcept -> std::same_as<TscTicks>;
    { c.elapsed(t) } noexcept -> std::same_as<DurationNs>;
};

static_assert(ClockSourceConcept<TscClockSource>);
static_assert(ClockSourceConcept<ReplayClockSource>);
static_assert(ClockSourceConcept<MockClockSource>);

} // namespace bybit
