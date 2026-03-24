#pragma once

// ─── Market-Data Correctness Types (Stage 3) ────────────────────────────────
// Typed event structs for the websocket → parser → order-book boundary.
// All types are trivially copyable, cache-line-aware, and carry SequenceNumber.
//
// Design invariant:
//   sequence gap detected
//   → order book invalidated
//   → snapshot resync required
//   → downstream consumers must not operate on silently corrupted book state

#include "strong_types.h"
#include "memory_policy.h"
#include "../config/types.h"

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace bybit {

// ─── Book State ─────────────────────────────────────────────────────────────
// Formal order-book validity state. Downstream code must check this
// before consuming book data.

enum class BookState : uint8_t {
    Empty              = 0,  // No snapshot received yet
    Valid              = 1,  // Book is consistent, safe to use
    InvalidGap         = 2,  // Sequence gap detected — book may be corrupted
    InvalidDisconnect  = 3,  // WS disconnected — book is stale
    PendingResync      = 4,  // Resync requested, waiting for snapshot
};

inline const char* book_state_name(BookState s) noexcept {
    switch (s) {
        case BookState::Empty:             return "Empty";
        case BookState::Valid:             return "Valid";
        case BookState::InvalidGap:        return "InvalidGap";
        case BookState::InvalidDisconnect: return "InvalidDisconnect";
        case BookState::PendingResync:     return "PendingResync";
        default:                           return "Unknown";
    }
}

// ─── Sequence Gap Info ──────────────────────────────────────────────────────
// Diagnostic record emitted when a gap is detected.

struct SequenceGapInfo {
    SequenceNumber expected;    // what we expected (book.seq + 1)
    SequenceNumber received;    // what we got
    uint64_t       gap_size;    // received - expected
    uint64_t       timestamp_ns;
};

static_assert(std::is_trivially_copyable_v<SequenceGapInfo>);

// ─── OB Delta Result ────────────────────────────────────────────────────────
// Returned by OrderBook::apply_delta() to provide richer feedback than bool.

enum class DeltaResult : uint8_t {
    Applied        = 0,  // Normal: delta applied, book remains valid
    StaleRejected  = 1,  // Delta seq <= book seq — harmless, ignored
    GapDetected    = 2,  // Delta seq > book seq + 1 — book invalidated
    BookNotValid   = 3,  // Book was already invalid — delta dropped
    DuplicateSeq   = 4,  // Exact same seq — harmless, ignored
};

inline const char* delta_result_name(DeltaResult r) noexcept {
    switch (r) {
        case DeltaResult::Applied:       return "Applied";
        case DeltaResult::StaleRejected: return "StaleRejected";
        case DeltaResult::GapDetected:   return "GapDetected";
        case DeltaResult::BookNotValid:  return "BookNotValid";
        case DeltaResult::DuplicateSeq:  return "DuplicateSeq";
        default:                         return "Unknown";
    }
}

// ─── Resync State (for WsManager) ──────────────────────────────────────────
// Prevents duplicate resync requests and tracks recovery.

enum class ResyncState : uint8_t {
    Normal         = 0,  // Operating normally, no resync in progress
    PendingResync  = 1,  // Resync REST request issued, awaiting response
};

// ─── Market-Data Drop Policy ────────────────────────────────────────────────
// Annotation for queue/connector semantics. Documents which data classes
// may be dropped and which must be preserved.

enum class DropPolicy : uint8_t {
    MustNotDrop   = 0,  // OB deltas, OB snapshots, private execution messages
    MayCoalesce   = 1,  // Trades (missing = imprecise features, not corruption)
    MayDrop       = 2,  // UI snapshots, telemetry, analytics
};

// ─── Market-Data Ingress Counters ───────────────────────────────────────────
// Diagnostic counters for the WS → OB boundary. All atomics for cross-thread
// visibility (even though currently single-threaded, this is defensive).

struct MDIngressCounters {
    uint64_t deltas_applied      = 0;
    uint64_t deltas_stale        = 0;
    uint64_t deltas_dropped_invalid = 0; // dropped because book was invalid
    uint64_t gaps_detected       = 0;
    uint64_t resyncs_triggered   = 0;
    uint64_t resyncs_succeeded   = 0;
    uint64_t resyncs_failed      = 0;
    uint64_t snapshots_applied   = 0;
    uint64_t checksum_mismatches  = 0;  // E2: CRC32 mismatch count
    uint64_t backpressure_drops   = 0;  // E6: deltas dropped under backpressure

    void reset() noexcept {
        deltas_applied = 0;
        deltas_stale = 0;
        deltas_dropped_invalid = 0;
        gaps_detected = 0;
        resyncs_triggered = 0;
        resyncs_succeeded = 0;
        resyncs_failed = 0;
        snapshots_applied = 0;
        checksum_mismatches = 0;
        backpressure_drops = 0;
    }
};

static_assert(std::is_trivially_copyable_v<MDIngressCounters>);

} // namespace bybit
