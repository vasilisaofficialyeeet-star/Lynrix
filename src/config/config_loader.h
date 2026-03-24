#pragma once

#include "types.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <stdexcept>

namespace bybit {

class ConfigLoader {
public:
    static AppConfig load(const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            throw std::runtime_error("Cannot open config file: " + path);
        }

        nlohmann::json j;
        ifs >> j;

        AppConfig cfg;

        // API keys from environment (never from config file)
        const char* key = std::getenv("BYBIT_API_KEY");
        const char* secret = std::getenv("BYBIT_API_SECRET");
        if (key) cfg.api_key = key;
        if (secret) cfg.api_secret = secret;

        // Exchange
        if (j.contains("symbol")) cfg.symbol = j["symbol"].get<std::string>();
        if (j.contains("paper_trading")) cfg.paper_trading = j["paper_trading"].get<bool>();
        if (j.contains("paper_fill_rate")) cfg.paper_fill_rate = j["paper_fill_rate"].get<double>();

        // WebSocket
        if (j.contains("ws")) {
            auto& ws = j["ws"];
            if (ws.contains("public_url")) cfg.ws_public_url = ws["public_url"].get<std::string>();
            if (ws.contains("private_url")) cfg.ws_private_url = ws["private_url"].get<std::string>();
            if (ws.contains("ping_interval_sec")) cfg.ws_ping_interval_sec = ws["ping_interval_sec"].get<int>();
            if (ws.contains("stale_timeout_sec")) cfg.ws_stale_timeout_sec = ws["stale_timeout_sec"].get<int>();
            if (ws.contains("reconnect_base_ms")) cfg.ws_reconnect_base_ms = ws["reconnect_base_ms"].get<int>();
            if (ws.contains("reconnect_max_ms")) cfg.ws_reconnect_max_ms = ws["reconnect_max_ms"].get<int>();
        }

        // REST
        if (j.contains("rest")) {
            auto& rest = j["rest"];
            if (rest.contains("base_url")) cfg.rest_base_url = rest["base_url"].get<std::string>();
            if (rest.contains("timeout_ms")) cfg.rest_timeout_ms = rest["timeout_ms"].get<int>();
            if (rest.contains("max_retries")) cfg.rest_max_retries = rest["max_retries"].get<int>();
        }

        // Trading
        if (j.contains("trading")) {
            auto& t = j["trading"];
            if (t.contains("order_qty")) cfg.order_qty = t["order_qty"].get<double>();
            if (t.contains("signal_threshold")) cfg.signal_threshold = t["signal_threshold"].get<double>();
            if (t.contains("signal_ttl_ms")) cfg.signal_ttl_ms = t["signal_ttl_ms"].get<int>();
            if (t.contains("entry_offset_bps")) cfg.entry_offset_bps = t["entry_offset_bps"].get<double>();
        }

        // Risk
        if (j.contains("risk")) {
            auto& r = j["risk"];
            if (r.contains("max_position_size")) cfg.risk.max_position_size = Qty(r["max_position_size"].get<double>());
            if (r.contains("max_leverage")) cfg.risk.max_leverage = r["max_leverage"].get<double>();
            if (r.contains("max_daily_loss")) cfg.risk.max_daily_loss = Notional(r["max_daily_loss"].get<double>());
            if (r.contains("max_drawdown")) cfg.risk.max_drawdown = r["max_drawdown"].get<double>();
            if (r.contains("max_orders_per_sec")) cfg.risk.max_orders_per_sec = r["max_orders_per_sec"].get<int>();
        }

        // Model
        if (j.contains("model")) {
            auto& m = j["model"];
            if (m.contains("bias")) cfg.model_bias = m["bias"].get<double>();
            if (m.contains("weights")) {
                auto& w = m["weights"];
                for (size_t i = 0; i < std::min(w.size(), cfg.model_weights.size()); ++i) {
                    cfg.model_weights[i] = w[i].get<double>();
                }
            }
        }

        // Persistence
        if (j.contains("persistence")) {
            auto& p = j["persistence"];
            if (p.contains("log_dir")) cfg.log_dir = p["log_dir"].get<std::string>();
            if (p.contains("batch_flush_ms")) cfg.batch_flush_ms = p["batch_flush_ms"].get<int>();
        }

        // Performance
        if (j.contains("performance")) {
            auto& p = j["performance"];
            if (p.contains("ob_levels")) cfg.ob_levels = p["ob_levels"].get<int>();
            if (p.contains("io_threads")) cfg.io_threads = p["io_threads"].get<int>();
        }

        // Instrument metadata (overrides BTC defaults for other symbols)
        if (j.contains("instrument")) {
            auto& inst = j["instrument"];
            if (inst.contains("tick_size")) cfg.tick_size = inst["tick_size"].get<double>();
            if (inst.contains("lot_size"))  cfg.lot_size  = inst["lot_size"].get<double>();
        }

        return cfg;
    }
};

} // namespace bybit
