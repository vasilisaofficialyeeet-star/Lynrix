#pragma once

// #13: Multi-instrument support
// Manages multiple trading instruments with independent orderbooks,
// feature engines, and execution contexts.

#include "../config/types.h"
#include "../orderbook/orderbook.h"
#include "../trade_flow/trade_flow_engine.h"
#include "../feature_engine/advanced_feature_engine.h"
#include "../regime/regime_detector.h"
#include "../utils/clock.h"
#include "../core/strong_types.h"

#include <spdlog/spdlog.h>

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <functional>

namespace bybit {

// ─── Instrument Specification ───────────────────────────────────────────────

struct InstrumentSpec {
    std::string symbol;
    double      tick_size   = 0.1;     // Min price increment
    double      lot_size    = 0.001;   // Min qty increment
    int         ob_levels   = 50;      // OB depth to subscribe
    double      max_position = 0.1;    // Max position size
    bool        enabled     = true;
};

// ─── Per-Instrument Context ─────────────────────────────────────────────────

struct InstrumentContext {
    InstrumentSpec          spec;
    OrderBook               orderbook;
    TradeFlowEngine         trade_flow;
    AdvancedFeatureEngine   feature_engine;
    RegimeDetector          regime_detector;
    Features                last_features{};
    ModelOutput             last_prediction{};
    uint64_t                last_update_ns = 0;
    uint64_t                tick_count = 0;

    explicit InstrumentContext(const InstrumentSpec& s) : spec(s) {}
};

// ─── Instrument Manager ─────────────────────────────────────────────────────

class InstrumentManager {
public:
    InstrumentManager() = default;

    // Add an instrument to manage
    void add_instrument(const InstrumentSpec& spec) {
        if (instruments_.count(spec.symbol)) {
            spdlog::warn("[INSTRUMENTS] {} already registered", spec.symbol);
            return;
        }
        instruments_.emplace(spec.symbol, std::make_unique<InstrumentContext>(spec));
        symbol_list_.push_back(spec.symbol);
        spdlog::info("[INSTRUMENTS] Registered {} (tick={} lot={} levels={})",
                     spec.symbol, spec.tick_size, spec.lot_size, spec.ob_levels);
    }

    // Remove an instrument
    void remove_instrument(const std::string& symbol) {
        instruments_.erase(symbol);
        symbol_list_.erase(
            std::remove(symbol_list_.begin(), symbol_list_.end(), symbol),
            symbol_list_.end());
    }

    // Get instrument context (nullptr if not found)
    InstrumentContext* get(const std::string& symbol) {
        auto it = instruments_.find(symbol);
        return it != instruments_.end() ? it->second.get() : nullptr;
    }

    const InstrumentContext* get(const std::string& symbol) const {
        auto it = instruments_.find(symbol);
        return it != instruments_.end() ? it->second.get() : nullptr;
    }

    // Iterate over all instruments
    template<typename Fn>
    void for_each(Fn&& fn) {
        for (auto& [symbol, ctx] : instruments_) {
            if (ctx->spec.enabled) {
                fn(symbol, *ctx);
            }
        }
    }

    template<typename Fn>
    void for_each(Fn&& fn) const {
        for (const auto& [symbol, ctx] : instruments_) {
            if (ctx->spec.enabled) {
                fn(symbol, *ctx);
            }
        }
    }

    // Get list of active symbols
    const std::vector<std::string>& symbols() const { return symbol_list_; }

    // Count
    size_t count() const { return instruments_.size(); }
    size_t active_count() const {
        size_t n = 0;
        for (const auto& [_, ctx] : instruments_) {
            if (ctx->spec.enabled) ++n;
        }
        return n;
    }

    // Generate WebSocket subscription message for all instruments
    std::string generate_subscription() const {
        std::string args;
        bool first = true;
        for (const auto& [symbol, ctx] : instruments_) {
            if (!ctx->spec.enabled) continue;
            if (!first) args += ",";
            args += fmt::format("\"orderbook.{}.{}\",\"publicTrade.{}\"",
                                ctx->spec.ob_levels, symbol, symbol);
            first = false;
        }
        return fmt::format(R"({{"op":"subscribe","args":[{}]}})", args);
    }

    // Route orderbook update to correct instrument
    bool route_ob_update(const std::string& symbol,
                         const PriceLevel* bids, size_t bid_count,
                         const PriceLevel* asks, size_t ask_count,
                         uint64_t seq, bool is_snapshot) {
        auto* ctx = get(symbol);
        if (!ctx) return false;

        if (is_snapshot) {
            ctx->orderbook.apply_snapshot(bids, bid_count, asks, ask_count, seq);
        } else {
            SequenceNumber seq_typed{seq};
            ctx->orderbook.apply_delta_typed(bids, bid_count, asks, ask_count, seq_typed);
        }
        ctx->last_update_ns = Clock::now_ns();
        return true;
    }

    // Route trade to correct instrument
    bool route_trade(const std::string& symbol, const Trade& trade) {
        auto* ctx = get(symbol);
        if (!ctx) return false;
        ctx->trade_flow.on_trade(trade);
        return true;
    }

    // Compute features for an instrument
    bool compute_features(const std::string& symbol) {
        auto* ctx = get(symbol);
        if (!ctx || !ctx->orderbook.valid()) return false;

        ctx->last_features = ctx->feature_engine.compute(ctx->orderbook, ctx->trade_flow);
        double mid = ctx->orderbook.mid_price();
        ctx->regime_detector.update(ctx->last_features, mid);
        ++ctx->tick_count;
        return true;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<InstrumentContext>> instruments_;
    std::vector<std::string> symbol_list_;
};

} // namespace bybit
