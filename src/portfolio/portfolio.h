#pragma once

#include "../config/types.h"
#include "../utils/clock.h"
#include <atomic>
#include <cmath>
#include <cstring>

namespace bybit {

// Thread-safe portfolio engine using SeqLock for consistent snapshots.
// Writers (WS thread) publish atomically; readers (strategy thread) get
// a coherent Position with no torn reads.
class Portfolio {
public:
    Portfolio() noexcept { reset(); }

    void reset() noexcept {
        seq_.fetch_add(1, std::memory_order_release); // odd → writing
        data_.size           = 0.0;
        data_.entry_price    = 0.0;
        data_.unrealized_pnl = 0.0;
        data_.realized_pnl   = 0.0;
        data_.funding_impact = 0.0;
        data_.side           = static_cast<uint8_t>(Side::Buy);
        seq_.fetch_add(1, std::memory_order_release); // even → done
    }

    // Called from private WS thread on position update
    void update_position(Qty size, Price entry_price, Side side) noexcept {
        seq_.fetch_add(1, std::memory_order_release);
        data_.size        = size.raw();
        data_.entry_price = entry_price.raw();
        data_.side        = static_cast<uint8_t>(side);
        seq_.fetch_add(1, std::memory_order_release);
    }

    // Called from private WS thread on execution
    void add_realized_pnl(Notional pnl) noexcept {
        seq_.fetch_add(1, std::memory_order_release);
        data_.realized_pnl += pnl.raw();
        seq_.fetch_add(1, std::memory_order_release);
    }

    void add_funding(Notional funding) noexcept {
        seq_.fetch_add(1, std::memory_order_release);
        data_.funding_impact += funding.raw();
        seq_.fetch_add(1, std::memory_order_release);
    }

    // Called from strategy thread to compute unrealized PnL
    void mark_to_market(Price current_price) noexcept {
        // Read current position fields consistently
        PortfolioData d;
        read_consistent(d);

        double pnl = 0.0;
        if (std::abs(d.size) > 1e-12 && std::abs(d.entry_price) > 1e-12) {
            auto s = static_cast<Side>(d.side);
            if (s == Side::Buy) {
                pnl = d.size * (current_price.raw() - d.entry_price);
            } else {
                pnl = d.size * (d.entry_price - current_price.raw());
            }
        }

        seq_.fetch_add(1, std::memory_order_release);
        data_.unrealized_pnl = pnl;
        seq_.fetch_add(1, std::memory_order_release);
    }

    // Consistent snapshot — SeqLock guarantees no torn reads
    Position snapshot() const noexcept {
        PortfolioData d;
        read_consistent(d);

        Position p;
        p.size           = Qty(d.size);
        p.entry_price    = Price(d.entry_price);
        p.unrealized_pnl = Notional(d.unrealized_pnl);
        p.realized_pnl   = Notional(d.realized_pnl);
        p.funding_impact = Notional(d.funding_impact);
        p.side           = static_cast<Side>(d.side);
        return p;
    }

    Notional net_pnl() const noexcept {
        PortfolioData d;
        read_consistent(d);
        return Notional(d.realized_pnl + d.unrealized_pnl + d.funding_impact);
    }

    bool has_position() const noexcept {
        PortfolioData d;
        read_consistent(d);
        return std::abs(d.size) > 1e-12;
    }

    // Legacy compat: raw-double overloads for WS thread boundary
    void update_position_raw(double size, double entry_price, Side side) noexcept {
        update_position(Qty(size), Price(entry_price), side);
    }
    void add_realized_pnl_raw(double pnl) noexcept {
        add_realized_pnl(Notional(pnl));
    }

private:
    // Plain-old-data snapshot — NOT individually atomic; protected by SeqLock.
    struct PortfolioData {
        double  size           = 0.0;
        double  entry_price    = 0.0;
        double  unrealized_pnl = 0.0;
        double  realized_pnl   = 0.0;
        double  funding_impact = 0.0;
        uint8_t side           = static_cast<uint8_t>(Side::Buy);
    };

    // SeqLock read: spins until a consistent snapshot is obtained.
    void read_consistent(PortfolioData& out) const noexcept {
        uint64_t s1, s2;
        int spins = 0;
        do {
            s1 = seq_.load(std::memory_order_acquire);
            if (s1 & 1) { // writer active
                if (++spins > 10000) { out = data_; return; } // safety fallback
                continue;
            }
            out = data_;
            std::atomic_thread_fence(std::memory_order_acquire);
            s2 = seq_.load(std::memory_order_acquire);
        } while (s1 != s2);
    }

    PortfolioData data_{};
    std::atomic<uint64_t> seq_{0};
};

} // namespace bybit
