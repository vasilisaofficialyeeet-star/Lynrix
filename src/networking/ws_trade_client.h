#pragma once

// #15: Bybit WebSocket Trade API for order placement
// Sends orders via private WebSocket instead of REST for ~10x lower latency.
// Bybit V5 WebSocket Trade: wss://stream.bybit.com/v5/trade

#include "../config/types.h"
#include "../utils/clock.h"
#include "../utils/hmac.h"
#include "../ws_client/ws_client.h"
#include <boost/asio/steady_timer.hpp>

#include <simdjson.h>
#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <string>
#include <functional>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace bybit {

namespace net = boost::asio;
namespace ssl = net::ssl;

// Callback for WS trade responses
using WsTradeCallback = std::function<void(bool success, std::string_view order_id, std::string_view err_msg)>;

class WsTradeClient {
public:
    explicit WsTradeClient(net::io_context& ioc, ssl::context& ssl_ctx, const AppConfig& cfg)
        : ioc_(ioc)
        , ssl_ctx_(ssl_ctx)
        , cfg_(cfg)
        , timeout_timer_(ioc)
    {}

    void start() {
        ws_ = std::make_shared<WsClient>(ioc_, ssl_ctx_, "ws-trade");
        ws_->set_url(cfg_.ws_trade_url.empty()
            ? "wss://stream.bybit.com/v5/trade"
            : cfg_.ws_trade_url);
        ws_->set_ping_interval(cfg_.ws_ping_interval_sec);
        ws_->set_stale_timeout(cfg_.ws_stale_timeout_sec);
        ws_->set_reconnect_params(cfg_.ws_reconnect_base_ms, cfg_.ws_reconnect_max_ms);

        ws_->set_on_connect([this]() {
            // Authenticate
            uint64_t expires = Clock::wall_ms() + 10000;
            std::string sign_str = "GET/realtime" + std::to_string(expires);
            std::string sign = hmac_sha256(cfg_.api_secret, sign_str);

            std::string auth = fmt::format(
                R"({{"op":"auth","args":["{}",{},"{}"]}})",
                cfg_.api_key, expires, sign);
            ws_->send(auth);
            spdlog::info("[WS-TRADE] Connected, auth sent (awaiting confirmation)");
        });

        ws_->set_on_disconnect([this]() {
            authenticated_.store(false, std::memory_order_release);
            spdlog::warn("[WS-TRADE] Disconnected");
        });

        ws_->set_on_message([this](simdjson::ondemand::document& doc, uint64_t recv_ns) {
            handle_response(doc, recv_ns);
        });

        ws_->connect();
    }

    void stop() {
        timeout_timer_.cancel();
        if (ws_) ws_->disconnect();
    }

    bool is_connected() const { return ws_ && ws_->is_connected() && authenticated_.load(std::memory_order_acquire); }

    // ─── Order Operations via WebSocket ─────────────────────────────────────

    void create_order(const std::string& symbol, Side side, OrderType type,
                      double qty, double price, TimeInForce tif,
                      bool reduce_only, WsTradeCallback cb) {
        std::string req_id = generate_req_id();

        std::string tif_str;
        switch (tif) {
            case TimeInForce::GTC: tif_str = "GTC"; break;
            case TimeInForce::IOC: tif_str = "IOC"; break;
            case TimeInForce::FOK: tif_str = "FOK"; break;
            case TimeInForce::PostOnly: tif_str = "PostOnly"; break;
        }

        std::string msg;
        if (type == OrderType::Limit) {
            msg = fmt::format(
                R"({{"reqId":"{}","header":{{"X-BAPI-TIMESTAMP":"{}","X-BAPI-RECV-WINDOW":"5000"}},"op":"order.create","args":[{{"category":"linear","symbol":"{}","side":"{}","orderType":"Limit","qty":"{}","price":"{}","timeInForce":"{}"{}}}]}})",
                req_id, Clock::wall_ms(), symbol,
                (side == Side::Buy) ? "Buy" : "Sell",
                fmt::format("{:.3f}", qty),
                fmt::format("{:.1f}", price),
                tif_str,
                reduce_only ? R"(,"reduceOnly":true)" : "");
        } else {
            msg = fmt::format(
                R"({{"reqId":"{}","header":{{"X-BAPI-TIMESTAMP":"{}","X-BAPI-RECV-WINDOW":"5000"}},"op":"order.create","args":[{{"category":"linear","symbol":"{}","side":"{}","orderType":"Market","qty":"{}"{}}}]}})",
                req_id, Clock::wall_ms(), symbol,
                (side == Side::Buy) ? "Buy" : "Sell",
                fmt::format("{:.3f}", qty),
                reduce_only ? R"(,"reduceOnly":true)" : "");
        }

        {
            std::lock_guard<std::mutex> lk(pending_mu_);
            pending_callbacks_[req_id] = {std::move(cb), Clock::now_ns()};
        }

        ws_->send(msg);
    }

    void cancel_order(const std::string& symbol, const std::string& order_id, WsTradeCallback cb) {
        std::string req_id = generate_req_id();

        std::string msg = fmt::format(
            R"({{"reqId":"{}","header":{{"X-BAPI-TIMESTAMP":"{}","X-BAPI-RECV-WINDOW":"5000"}},"op":"order.cancel","args":[{{"category":"linear","symbol":"{}","orderId":"{}"}}]}})",
            req_id, Clock::wall_ms(), symbol, order_id);

        {
            std::lock_guard<std::mutex> lk(pending_mu_);
            pending_callbacks_[req_id] = {std::move(cb), Clock::now_ns()};
        }

        ws_->send(msg);
    }

    void amend_order(const std::string& symbol, const std::string& order_id,
                     double new_qty, double new_price, WsTradeCallback cb) {
        std::string req_id = generate_req_id();

        std::string qty_part = new_qty > 0.0
            ? fmt::format(R"(,"qty":"{:.3f}")", new_qty) : "";
        std::string price_part = new_price > 0.0
            ? fmt::format(R"(,"price":"{:.1f}")", new_price) : "";

        std::string msg = fmt::format(
            R"({{"reqId":"{}","header":{{"X-BAPI-TIMESTAMP":"{}","X-BAPI-RECV-WINDOW":"5000"}},"op":"order.amend","args":[{{"category":"linear","symbol":"{}","orderId":"{}"{}{}}}]}})",
            req_id, Clock::wall_ms(), symbol, order_id, qty_part, price_part);

        {
            std::lock_guard<std::mutex> lk(pending_mu_);
            pending_callbacks_[req_id] = {std::move(cb), Clock::now_ns()};
        }

        ws_->send(msg);
    }

private:
    // M5: Periodic timeout checker for pending callbacks
    void start_timeout_checker() {
        timeout_timer_.expires_after(std::chrono::seconds(5));
        timeout_timer_.async_wait([this](boost::system::error_code ec) {
            if (ec) return;
            cleanup_expired_callbacks();
            start_timeout_checker();
        });
    }

    void cleanup_expired_callbacks() {
        constexpr uint64_t TIMEOUT_NS = 30'000'000'000ULL; // 30 seconds
        uint64_t now = Clock::now_ns();
        std::lock_guard<std::mutex> lk(pending_mu_);
        for (auto it = pending_callbacks_.begin(); it != pending_callbacks_.end();) {
            if (now - it->second.timestamp_ns > TIMEOUT_NS) {
                spdlog::warn("[WS-TRADE] Callback timeout for reqId={}", it->first);
                if (it->second.callback) {
                    it->second.callback(false, "", "timeout");
                }
                it = pending_callbacks_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void handle_response(simdjson::ondemand::document& doc, uint64_t recv_ns) {
        // Check for auth response (no reqId, has "op":"auth")
        auto op_field = doc.find_field("op");
        if (!op_field.error()) {
            std::string_view op_sv = op_field.get_string().value();
            if (op_sv == "auth") {
                auto success_field = doc.find_field("success");
                if (!success_field.error() && success_field.get_bool().value()) {
                    authenticated_.store(true, std::memory_order_release);
                    spdlog::info("[WS-TRADE] Auth confirmed by exchange");
                    // Start timeout checker once after successful auth
                    if (!timeout_checker_started_) {
                        timeout_checker_started_ = true;
                        start_timeout_checker();
                    }
                } else {
                    spdlog::error("[WS-TRADE] Auth REJECTED by exchange");
                }
                return;
            }
        }

        auto req_id_field = doc.find_field("reqId");
        if (req_id_field.error()) return; // Not a trade response

        std::string_view req_id = req_id_field.get_string().value();

        auto ret_code = doc.find_field("retCode");
        int64_t rc = ret_code.error() ? -1 : ret_code.get_int64().value();

        std::string_view order_id = "";
        std::string_view err_msg = "";

        if (rc == 0) {
            auto data = doc.find_field("data");
            if (!data.error()) {
                auto oid = data.find_field("orderId");
                if (!oid.error()) order_id = oid.get_string().value();
            }
        } else {
            auto msg = doc.find_field("retMsg");
            if (!msg.error()) err_msg = msg.get_string().value();
        }

        // Find and invoke callback
        WsTradeCallback cb;
        {
            std::lock_guard<std::mutex> lk(pending_mu_);
            auto it = pending_callbacks_.find(std::string(req_id));
            if (it != pending_callbacks_.end()) {
                cb = std::move(it->second.callback);
                pending_callbacks_.erase(it);
            }
        }

        if (cb) {
            cb(rc == 0, order_id, err_msg);
        }
    }

    std::string generate_req_id() {
        uint64_t id = req_counter_.fetch_add(1, std::memory_order_relaxed);
        return fmt::format("wst-{}", id);
    }

    net::io_context& ioc_;
    ssl::context& ssl_ctx_;
    const AppConfig& cfg_;

    std::shared_ptr<WsClient> ws_;
    std::atomic<bool> authenticated_{false};
    std::atomic<uint64_t> req_counter_{1};
    bool timeout_checker_started_ = false;

    // M5: Pending callbacks with timestamp for timeout detection
    struct PendingCallback {
        WsTradeCallback callback;
        uint64_t timestamp_ns;
    };
    std::unordered_map<std::string, PendingCallback> pending_callbacks_;
    std::mutex pending_mu_;
    net::steady_timer timeout_timer_;
};

} // namespace bybit
