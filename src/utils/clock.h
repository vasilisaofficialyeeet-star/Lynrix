#pragma once

// ─── Clock — Unified Timing Facade (Compatibility Layer) ─────────────────────
//
// MIGRATION STATUS: This file is a backward-compatible shim.
//
// All 118+ existing call sites (Clock::now_ns(), Clock::now_ms(), etc.)
// continue to work unchanged via the raw uint64_t API below.
//
// NEW CODE should use:
//   - TscClockSource   (static dispatch, zero overhead)    — src/core/clock_source.h
//   - ReplayClockSource (atomic, for replay)               — src/core/clock_source.h
//   - ClockFn           (type-erased, 16 bytes, 1 indirection) — src/core/clock_source.h
//
// Typed overloads (now_typed(), elapsed_typed()) are provided for incremental
// migration of individual call sites without touching unrelated code.
//
// TODO(stage3): Remove raw uint64_t API once all call sites are migrated.

#include "tsc_clock.h"
#include "../core/strong_types.h"
#include <chrono>
#include <cstdint>

namespace bybit {

struct Clock {
    // ── Hot path: TSC-based monotonic time (LEGACY raw API) ──────────────
    // Preserved for backward compatibility. 118+ call sites depend on these.

    static uint64_t now_ns() noexcept {
        return TscClock::now_ns();
    }

    static uint64_t now_ms() noexcept {
        return TscClock::now_ns() / 1'000'000ULL;
    }

    static uint64_t now_ticks() noexcept {
        return TscClock::now();
    }

    static uint64_t elapsed_ns(uint64_t start_ticks) noexcept {
        return TscClock::elapsed_ns(start_ticks);
    }

    // ── Hot path: typed API (Stage 1 V2) ─────────────────────────────────
    // Same cost. Returns strong types instead of raw uint64_t.
    // Use these in new code or when migrating call sites incrementally.

    static TimestampNs now_typed() noexcept {
        return TimestampNs{TscClock::now_ns()};
    }

    static TscTicks ticks_typed() noexcept {
        return TscTicks{TscClock::now()};
    }

    static DurationNs elapsed_typed(TscTicks start) noexcept {
        return DurationNs{static_cast<int64_t>(TscClock::elapsed_ns(start.raw()))};
    }

    static DurationNs elapsed_typed(uint64_t start_ticks) noexcept {
        return DurationNs{static_cast<int64_t>(TscClock::elapsed_ns(start_ticks))};
    }

    // ── Cold path: wall-clock for logging / persistence ──────────────────
    // Uses std::chrono — NOT for latency measurement in hot path.

    static uint64_t wall_ms() noexcept {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
    }

    static uint64_t wall_ns() noexcept {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
    }
};

} // namespace bybit
