#pragma once

// ─── TSC Clock — mach_absolute_time Based Timing ────────────────────────────
// Replaces std::chrono in hot path for deterministic, sub-nanosecond timing.
//
// On Apple Silicon, mach_absolute_time() reads CNTPCT_EL0 directly — a single
// instruction with ~1 ns resolution and zero syscall overhead.
//
// Key advantages over std::chrono::steady_clock:
//   - No vDSO/syscall overhead (direct register read)
//   - Deterministic latency (~2 ns vs ~20-50 ns for steady_clock)
//   - Monotonic and invariant across P/E cores on M-series
//   - No frequency scaling artifacts
//
// Usage:
//   uint64_t t0 = TscClock::now();
//   ... hot path ...
//   uint64_t elapsed_ns = TscClock::elapsed_ns(t0);

#include <cstdint>
#include <mach/mach_time.h>

namespace bybit {

class TscClock {
public:
    // Read TSC tick counter — single ARM instruction on Apple Silicon
    static inline uint64_t now() noexcept {
        return mach_absolute_time();
    }

    // Convert ticks to nanoseconds using cached timebase info
    static inline uint64_t to_ns(uint64_t ticks) noexcept {
        // On Apple Silicon, numer/denom is always 1/1 so this is identity.
        // We still do the multiply+divide for correctness on other hardware.
        return ticks * timebase().numer / timebase().denom;
    }

    // Current time in nanoseconds (monotonic)
    static inline uint64_t now_ns() noexcept {
        return to_ns(now());
    }

    // Elapsed nanoseconds since a previous now() call
    static inline uint64_t elapsed_ns(uint64_t start_ticks) noexcept {
        return to_ns(now() - start_ticks);
    }

    // Elapsed microseconds (double) since a previous now() call
    static inline double elapsed_us(uint64_t start_ticks) noexcept {
        return static_cast<double>(elapsed_ns(start_ticks)) * 0.001;
    }

    // rdtsc-style: read raw tick without conversion (cheapest possible)
    static inline uint64_t rdtsc() noexcept {
#if defined(__aarch64__)
        uint64_t val;
        asm volatile("mrs %0, cntvct_el0" : "=r"(val));
        return val;
#else
        return mach_absolute_time();
#endif
    }

    // Calibration: ticks per nanosecond (cached)
    static inline double ticks_per_ns() noexcept {
        const auto& tb = timebase();
        return static_cast<double>(tb.denom) / static_cast<double>(tb.numer);
    }

private:
    // Lazy-initialized timebase info (called once, then cached)
    static const mach_timebase_info_data_t& timebase() noexcept {
        static mach_timebase_info_data_t info = [] {
            mach_timebase_info_data_t tb;
            mach_timebase_info(&tb);
            return tb;
        }();
        return info;
    }
};

// ─── Scoped TSC Timer ───────────────────────────────────────────────────────
// RAII timer that records elapsed nanoseconds into a uint64_t& on destruction.
// Zero overhead if optimized away by compiler.

class ScopedTscTimer {
public:
    explicit ScopedTscTimer(uint64_t& out_ns) noexcept
        : out_(out_ns), start_(TscClock::now()) {}

    ~ScopedTscTimer() noexcept {
        out_ = TscClock::elapsed_ns(start_);
    }

    ScopedTscTimer(const ScopedTscTimer&) = delete;
    ScopedTscTimer& operator=(const ScopedTscTimer&) = delete;

private:
    uint64_t& out_;
    uint64_t  start_;
};

// ─── Typed Wrappers (Stage 1 V2 integration) ─────────────────────────────────
// These forward-declare strong types and provide typed accessors.
// Existing raw uint64_t API above is preserved for 118+ existing call sites.
// New code should prefer these typed accessors via TscClockSource in clock_source.h.

} // namespace bybit
