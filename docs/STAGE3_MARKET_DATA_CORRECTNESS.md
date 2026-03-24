# Stage 3: Market-Data Correctness Slice — Implementation Plan

## 1. Executive Summary

Stage 3 hardens the websocket → parser → order-book boundary for correctness.
Today, the engine has:
- **Silent drop risk**: `PipelineConnector::send()` returns false on full queue and increments `drop_count_` — but `ws_manager.h` never uses a queue at all; it calls `ob_.apply_delta()` inline on the WS callback thread. This means there is no queue-drop problem per se, but there IS a thread-safety concern (WS callbacks run on a Boost.Asio strand, strategy_tick on the io_context — both in the same single-threaded io_context, so currently safe, but fragile).
- **Weak sequence checking**: `apply_delta()` only checks `seq < seq_id_` (stale reject). It does NOT check for gaps (`seq > seq_id_ + 1`). A gap means lost deltas → silently corrupted book.
- **No invalidation propagation**: `ob_.valid()` only becomes false on `reset()`. There is no "invalid due to gap" state. Downstream (`strategy_tick`) just skips when `!ob_.valid()`, which is correct behavior but the gap-to-invalid transition doesn't exist.
- **Resync is fire-and-forget**: `request_ob_resync()` calls `ob_.reset()` then issues an async REST call. Between reset() and the REST callback, the book is invalid. But new deltas arriving during this window are silently dropped (valid_ == false → apply_delta returns false). The resync snapshot's seq might be stale relative to deltas that arrived during the REST latency. There is no cross-validation.
- **No typed sequence numbers**: `seq` is raw `uint64_t` everywhere despite `SequenceNumber` existing in `strong_types.h`.
- **Fixed-point boundary is ad-hoc**: `FixedPrice::from_double()` uses `(int64_t)(p * 1e8 + 0.5)` rounding. The `PriceLevel` struct uses `double price` everywhere at the boundary. Conversion happens inside `apply_snapshot`/`apply_delta` — correct location but not documented or enforced.

**What this stage delivers:**
1. New `src/core/market_data.h` — typed MD event structs with `SequenceNumber`
2. Strict gap detection in `OrderBook::apply_delta()`
3. Formal book invalidation state with reason tracking
4. Resync state machine in `ws_manager.h` (pending resync → snapshot received → delta replay)
5. `PipelineConnector` drop-policy annotations (market-data = MUST_NOT_DROP)
6. Diagnostics counters for gap events, invalidations, resync attempts
7. BlackBoxRecorder event types for gap/invalidation/resync events
8. Fixed-point boundary documentation and `static_assert` enforcement

## 2. Stage 3 Integration Strategy

**Approach**: Bottom-up, type-first.

1. Create `market_data.h` with typed event structs (no existing file changes)
2. Refactor `OrderBook::apply_delta()` for strict gap detection + invalidation state
3. Refactor `ws_manager.h` to use typed events + resync state machine
4. Add diagnostic counters to `Metrics`
5. Add BlackBoxRecorder events for gap/invalidation
6. Update `strategy_tick` OB validity gate
7. Write tests, build, verify

**Rollback points**: Each file change is independently revertible. The new `market_data.h` header is additive. OrderBook changes are backward-compatible (same public API, stricter internal semantics). WsManager changes are the most impactful but contained to one file.

## 3. Dependency Graph and Migration Order

```
market_data.h (NEW, no deps beyond strong_types.h)
    ↓
orderbook_v3.h (add gap detection + invalidation state)
    ↓
latency_histogram.h (add gap/resync counters to Metrics)
    ↓
ws_manager.h (resync state machine, use market_data types)
    ↓
application.h (OB validity gate refinement)
    ↓
blackbox_recorder.h (add MD event types)
    ↓
tests + build verification
```

## 4. Current-State Audit of Stage 3 Targets

### ws_client.h (src/ws_client/ws_client.h)
- **Role**: Low-level Beast WS connection with reconnect, ping, stale detection
- **Fragilities**: None for Stage 3. It correctly delivers raw JSON to callbacks with recv_ns timestamp. No sequence handling here (correct — that belongs in ws_manager).
- **Changes needed**: None. Leave as-is.

### ws_manager.h (src/networking/ws_manager.h)
- **Role**: Dispatches parsed WS messages to OB/trades/execution. Contains the parser boundary.
- **Fragilities**:
  - `seq` parsed as raw `uint64_t`, no gap detection before passing to OB
  - `handle_orderbook_message` sends `PriceLevel` (double) to OB — correct boundary for conversion
  - `request_ob_resync()` has no state machine. Multiple resyncs can fire concurrently.
  - REST resync uses `nlohmann::json` (slow, allocating) in callback — acceptable since it's off hot path
  - No blackbox recording of gap/invalidation events
- **Changes needed**: Resync state machine, gap pre-check, typed sequence, diagnostic counters

### orderbook_v3.h (src/orderbook/orderbook_v3.h)
- **Role**: O(1) L2 order book with Robin Hood hashmap
- **Fragilities**:
  - `apply_delta()` line 388: `if (seq < seq_id_) return false` — rejects stale but accepts gaps
  - Line 406: `seq_id_ = seq` — just overwrites, no gap detection
  - `valid_` is boolean only. No reason tracking. No "invalid_pending_resync" state.
  - `reset()` clears everything including seq_id_ to 0 — means post-reset deltas with any seq > 0 will be accepted, even if they're from before the snapshot
- **Changes needed**: Gap detection, invalidation reason enum, sequence continuity enforcement

### lockfree_pipeline.h (src/utils/lockfree_pipeline.h)
- **Role**: SPSC queue, TripleBuffer, SeqLock, PipelineConnector
- **Fragilities**: `PipelineConnector::send()` silently drops on full with only a counter. This is correct for some data classes (UI snapshots, telemetry) but WRONG for OB deltas.
- **Changes needed**: Drop policy annotation. Currently ws_manager doesn't use PipelineConnector for OB data (it's inline), so this is documentation + future-proofing, not a correctness fix.

### config/types.h
- **Role**: Shared types (PriceLevel, Trade, Features, etc.)
- **Changes needed**: None for Stage 3. `PriceLevel` stays as double — it's the exchange-boundary type. `FixedPrice` conversion happens inside OB.

### strong_types.h
- **Role**: Already has `SequenceNumber` type
- **Changes needed**: None. Already complete.

## 5-8. File-by-File Refactor Plans

### 5. market_data.h (NEW FILE)
Internal market-data event types for the WS→parser→OB boundary.

### 6. orderbook_v3.h
- Add `BookState` enum: `{Valid, InvalidGap, InvalidResync, Empty}`
- Add `invalidation_reason()` accessor
- Add `invalidate(BookState reason)` method
- Refactor `apply_delta()` to detect gap: `seq > seq_id_ + expected_delta`
- Add `expected_seq()` accessor for WsManager to pre-check
- Add diagnostic: `gap_count_`, `last_gap_seq_`

### 7. ws_manager.h
- Add `ResyncState` enum: `{Normal, PendingResync, AwaitingSnapshot}`
- Add resync state machine preventing duplicate resync requests
- Use `SequenceNumber` for parsed seq values
- Add pre-OB gap check with blackbox recording
- Add diagnostic counters

### 8. Metrics
- Add `seq_gaps_total`, `ob_invalidations_total` counters

## 9. Target Design for Sequence Handling and Gap Detection

**Bybit L2 orderbook contract:**
- Snapshot delivers initial state with `seq`
- Deltas deliver incremental updates with `seq`
- Bybit docs: "seq is continuously increasing by 1"
- If `delta.seq != book.seq + 1`, we have a gap

**Gap handling policy:**
```
on_delta(seq):
  if book.state != Valid:
    drop delta, increment counter
    return
  if seq == book.seq + 1:
    apply normally
  elif seq <= book.seq:
    stale, drop (already applied or older)
  else:
    GAP DETECTED: seq > book.seq + 1
    book.invalidate(InvalidGap)
    record gap event to blackbox
    trigger resync
```

## 10. Target Design for Queue / Backpressure Policy

| Data Class | Policy | Rationale |
|-----------|--------|-----------|
| OB deltas | MUST NOT DROP | Gaps corrupt book. Currently inline (no queue). |
| OB snapshots | MUST NOT DROP | Required for recovery. Currently inline. |
| Trades | MAY COALESCE | Missing trades = imprecise features, not corruption. |
| Private WS (orders/fills) | MUST NOT DROP | Execution state depends on it. Currently inline. |
| UI snapshots | MAY DROP (latest-wins) | TripleBuffer already provides this. |
| Telemetry/diagnostics | MAY DROP with counter | BlackBoxRecorder ring buffer already does this. |
| Analytics/RL | MAY SAMPLE | Not time-critical. |

Since the current architecture is single-threaded io_context (WS callbacks + strategy_tick on same thread), there is no actual queue between WS and OB. The "queue" concern is about the PipelineConnector if the architecture ever moves to multi-threaded. For now, we add policy annotations as documentation.

## 11. Target Design for Order-Book Invalidation and Resync

**Book states:**
```
Empty → (snapshot) → Valid → (gap detected) → InvalidGap
                     Valid → (ws disconnect) → InvalidDisconnect
                     InvalidGap → (resync triggered) → PendingResync
                     InvalidDisconnect → (reconnect) → PendingResync
                     PendingResync → (snapshot applied) → Valid
```

**Downstream behavior when book is not Valid:**
- `strategy_tick()`: Already gates on `ob_.valid()` — returns early. No change needed.
- `feature_engine_`: Not called when book invalid. Correct.
- `watchdog_check()`: Already checks `ob_.valid()`. Correct.
- `UI snapshot`: Already publishes `ob_valid` flag. GUI dims display. Correct.

**Resync state machine in WsManager:**
```
Normal:
  delta arrives → check seq → apply or detect gap → InvalidGap → trigger resync
  
PendingResync:
  new deltas arrive → buffer seq numbers for post-resync validation
  REST callback arrives with snapshot → apply snapshot → transition to Normal
  timeout (5s) → retry resync

AwaitingSnapshot (not needed — REST is async callback, PendingResync covers it)
```

## 12. Target Design for Fixed-Point Boundary Discipline

**Current boundary** (correct, needs documentation):
```
Exchange (string "98234.5") 
  → fast_atof() → double 
  → PriceLevel{double price, double qty}
  → OrderBook::apply_snapshot/apply_delta()
  → FixedPrice::from_double() [int64_t, *1e8, round-to-nearest]
  → internal processing (all fixed-point)
  → output: best_bid()/best_ask()/mid_price() → FixedPrice::to_double() → double
```

**Rules:**
1. `FixedPrice::from_double()` is the ONLY entry point into fixed-point. Called inside OB.
2. `FixedPrice::to_double()` is the ONLY exit point from fixed-point. Called in OB accessors.
3. Features, signals, execution all use `double`. They never see `FixedPrice`.
4. Rounding: round-to-nearest (`+ 0.5` truncation). This is acceptable for price levels.
5. Quantity stays as `double` everywhere — no fixed-point for qty.

**No changes needed** — the boundary is already correct. We add `static_assert` documentation.

## 13-14. Code Skeletons and Compatibility

See implementation below.

## 15. Test Plan

1. **Sequence gap detection**: Feed OB snapshot(seq=100), delta(seq=102) → book becomes invalid
2. **Stale delta rejection**: Feed delta(seq=99) after snapshot(seq=100) → ignored, book stays valid
3. **Normal delta**: Feed delta(seq=101) after snapshot(seq=100) → applied, book valid
4. **Resync recovery**: Invalidate book → apply new snapshot → book valid again
5. **No operation on invalid book**: Verify `strategy_tick` returns early when book invalid
6. **Concurrent resync prevention**: Trigger two gaps rapidly → only one resync request
7. **Fixed-point round-trip**: Verify `from_double(to_double(x)) == x` for representative prices
8. **Gap counter diagnostics**: Verify gap events increment counters
9. **BlackBox gap event**: Verify gap detection emits recorder event
10. **Backward compat**: All existing OB tests pass unchanged

## 16. Benchmark Plan

1. `apply_delta()` with gap check overhead: measure ns/delta (expect <1ns overhead — single branch)
2. `SequenceNumber` vs raw `uint64_t` in gap check: verify zero overhead (same codegen)
3. Invalidation check in `strategy_tick`: verify zero overhead (same boolean check)

## 17. CI / Verification Plan

- `static_assert(sizeof(SequenceNumber) == 8)` — already in strong_types.h
- `static_assert(std::is_trivially_copyable_v<OBDeltaEvent>)` — new events
- All 12 existing test suites pass + new Stage 3 test suite
- New ctest target: `test_stage3_md_correctness`

## 18. Risks and Trade-offs

1. **Bybit seq semantics**: Assumed `seq` increments by 1. If Bybit uses non-unit increments, the gap check needs adjustment. Mitigation: log first few seq deltas at startup.
2. **Resync latency window**: Between gap detection and REST snapshot, the book is invalid (50-500ms). This is correct behavior — better to skip ticks than trade on corrupted state.
3. **REST snapshot seq vs buffered deltas**: After resync, deltas with seq <= snapshot.seq should be dropped. Deltas with seq > snapshot.seq + 1 trigger another resync. This is handled by the existing `seq < seq_id_` check.
