// ─── OrderBook Parser Fuzzer ───────────────────────────────────────────────────
// LibFuzzer target for stress-testing orderbook delta parsing.
// Feeds random/mutated data into OrderBook v3's apply_delta and apply_snapshot.
//
// Goals:
//   - No UB (undefined behavior) on any input
//   - No crashes (SIGSEGV, SIGABRT, etc.)
//   - No infinite loops
//   - No memory corruption (ASAN/MSAN clean)
//
// Build:
//   cmake --build . --target fuzz_orderbook_parser
//
// Run:
//   ./fuzz_orderbook_parser -max_len=4096 -max_total_time=600
//   # or with corpus:
//   mkdir -p corpus/orderbook
//   ./fuzz_orderbook_parser corpus/orderbook -max_len=4096

#include "../src/orderbook/orderbook_v3.h"
#include "../src/config/types.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>

using namespace bybit;

// ─── Fuzzer Helpers ────────────────────────────────────────────────────────────

// Extract a value from fuzz data, advancing the pointer
template <typename T>
static bool consume(const uint8_t*& data, size_t& size, T& out) {
    if (size < sizeof(T)) return false;
    std::memcpy(&out, data, sizeof(T));
    data += sizeof(T);
    size -= sizeof(T);
    return true;
}

// Sanitize a double: replace NaN/Inf with 0.0
static double sanitize_double(double v) {
    if (std::isnan(v) || std::isinf(v)) return 0.0;
    // Clamp to reasonable range
    if (v > 1e12) return 1e12;
    if (v < -1e12) return -1e12;
    return v;
}

// ─── Fuzz Target: OrderBook Delta Parsing ──────────────────────────────────────

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Need at least a mode byte
    if (size < 1) return 0;

    // First byte selects operation mode
    uint8_t mode = data[0];
    data++; size--;

    // Persistent orderbook across iterations for stateful fuzzing
    static OrderBook ob;
    static bool initialized = false;

    if (!initialized || (mode & 0x80)) {
        // Initialize with a valid snapshot
        ob.reset();
        PriceLevel bids[5] = {
            {50000.0, 1.0}, {49999.9, 2.0}, {49999.8, 3.0},
            {49999.7, 4.0}, {49999.6, 5.0}
        };
        PriceLevel asks[5] = {
            {50000.1, 1.0}, {50000.2, 2.0}, {50000.3, 3.0},
            {50000.4, 4.0}, {50000.5, 5.0}
        };
        ob.apply_snapshot(bids, 5, asks, 5, 1);
        initialized = true;
    }

    switch (mode & 0x0F) {
        case 0:
        case 1:
        case 2:
        case 3: {
            // ── Mode 0-3: Random delta with raw doubles from fuzz data ──
            size_t max_levels = std::min(size / 16, static_cast<size_t>(50));
            if (max_levels == 0) break;

            PriceLevel bids[50], asks[50];
            size_t bid_n = 0, ask_n = 0;

            // Split levels between bids and asks
            for (size_t i = 0; i < max_levels; ++i) {
                double price, qty;
                if (!consume(data, size, price)) break;
                if (!consume(data, size, qty)) break;

                // Use raw fuzz doubles — OB must handle gracefully
                PriceLevel lvl{price, qty};
                if (i % 2 == 0 && bid_n < 50) {
                    bids[bid_n++] = lvl;
                } else if (ask_n < 50) {
                    asks[ask_n++] = lvl;
                }
            }

            uint64_t seq = ob.seq_id() + 1;
            ob.apply_delta(bids, bid_n, asks, ask_n, seq);
            break;
        }

        case 4:
        case 5: {
            // ── Mode 4-5: Snapshot with raw fuzz data ──
            size_t max_levels = std::min(size / 16, static_cast<size_t>(50));
            if (max_levels == 0) break;

            PriceLevel bids[50], asks[50];
            size_t bid_n = 0, ask_n = 0;

            for (size_t i = 0; i < max_levels; ++i) {
                double price, qty;
                if (!consume(data, size, price)) break;
                if (!consume(data, size, qty)) break;

                PriceLevel lvl{price, qty};
                if (i % 2 == 0 && bid_n < 50) {
                    bids[bid_n++] = lvl;
                } else if (ask_n < 50) {
                    asks[ask_n++] = lvl;
                }
            }

            ob.apply_snapshot(bids, bid_n, asks, ask_n, ob.seq_id() + 1);
            break;
        }

        case 6: {
            // ── Mode 6: Extreme values ──
            PriceLevel bids[4] = {
                {0.0, 0.0},
                {-1.0, 1.0},
                {1e18, 1e18},
                {std::nan(""), std::nan("")}
            };
            PriceLevel asks[4] = {
                {0.0, 0.0},
                {-1.0, -1.0},
                {1e18, 1e18},
                {std::numeric_limits<double>::infinity(), 0.0}
            };
            ob.apply_delta(bids, 4, asks, 4, ob.seq_id() + 1);
            break;
        }

        case 7: {
            // ── Mode 7: Crossed book (bid > ask) ──
            double base = 50000.0;
            double offset = 0.0;
            consume(data, size, offset);
            offset = sanitize_double(offset);

            PriceLevel bids[1] = {{base + std::abs(offset) + 100.0, 1.0}};
            PriceLevel asks[1] = {{base - std::abs(offset), 1.0}};
            ob.apply_delta(bids, 1, asks, 1, ob.seq_id() + 1);
            break;
        }

        case 8: {
            // ── Mode 8: Many duplicate prices ──
            double price = 50000.0;
            consume(data, size, price);
            price = sanitize_double(price);
            if (price <= 0.0) price = 50000.0;

            PriceLevel bids[20], asks[20];
            for (int i = 0; i < 20; ++i) {
                double qty = 0.0;
                consume(data, size, qty);
                bids[i] = {price, sanitize_double(qty)};
                asks[i] = {price + 0.1, sanitize_double(qty)};
            }
            ob.apply_delta(bids, 20, asks, 20, ob.seq_id() + 1);
            break;
        }

        case 9: {
            // ── Mode 9: Zero-quantity removals ──
            PriceLevel bids[10], asks[10];
            for (int i = 0; i < 10; ++i) {
                double price = 0.0;
                consume(data, size, price);
                price = sanitize_double(price);
                bids[i] = {price, 0.0};  // remove
                asks[i] = {price + 0.1, 0.0}; // remove
            }
            ob.apply_delta(bids, 10, asks, 10, ob.seq_id() + 1);
            break;
        }

        case 10: {
            // ── Mode 10: Accessor stress ──
            // Just call all read accessors — must not crash
            volatile double v;
            v = ob.best_bid();
            v = ob.best_ask();
            v = ob.best_bid_qty();
            v = ob.best_ask_qty();
            v = ob.spread();
            v = ob.mid_price();
            v = ob.microprice();
            v = ob.imbalance(5);
            v = ob.imbalance(50);
            v = ob.imbalance(500);
            v = ob.vwap(5);
            v = ob.liquidity_slope(10);
            v = ob.cancel_spike();
            v = ob.spread_change_rate();
            v = ob.total_bid_qty(20);
            v = ob.total_ask_qty(20);
            (void)v;
            (void)ob.valid();
            (void)ob.seq_id();
            (void)ob.bid_count();
            (void)ob.ask_count();
            break;
        }

        case 11: {
            // ── Mode 11: Rapid reset + snapshot ──
            ob.reset();
            size_t levels = std::min(size / 16, static_cast<size_t>(20));
            PriceLevel bids[20], asks[20];
            size_t n = 0;
            for (size_t i = 0; i < levels; ++i) {
                double p, q;
                if (!consume(data, size, p)) break;
                if (!consume(data, size, q)) break;
                p = sanitize_double(p);
                q = sanitize_double(q);
                if (p > 0 && q > 0 && n < 20) {
                    bids[n] = {p, q};
                    asks[n] = {p + 0.1, q};
                    ++n;
                }
            }
            if (n > 0) {
                ob.apply_snapshot(bids, n, asks, n, 1);
            }
            initialized = n > 0;
            break;
        }

        case 12: {
            // ── Mode 12: set_bbo with fuzz data ──
            double bp = 0, bq = 0, ap = 0, aq = 0;
            consume(data, size, bp);
            consume(data, size, bq);
            consume(data, size, ap);
            consume(data, size, aq);
            PriceLevel bid{sanitize_double(bp), sanitize_double(bq)};
            PriceLevel ask{sanitize_double(ap), sanitize_double(aq)};
            uint64_t ts = 0;
            consume(data, size, ts);
            ob.set_bbo(bid, ask, ts);
            break;
        }

        default:
            break;
    }

    return 0;
}
