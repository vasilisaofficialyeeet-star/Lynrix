#pragma once

#include "../config/types.h"
#include "../utils/clock.h"
#include "../utils/hmac.h"
#include "../metrics/latency_histogram.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <simdjson.h>
#include <spdlog/spdlog.h>

#include <string>
#include <functional>
#include <atomic>
#include <memory>
#include <thread>
#include <chrono>

namespace bybit {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

// Callback types
using OnMessageCb = std::function<void(simdjson::ondemand::document&, uint64_t recv_ns)>;
using OnConnectCb = std::function<void()>;
using OnDisconnectCb = std::function<void()>;

class WsClient : public std::enable_shared_from_this<WsClient> {
public:
    using ws_stream_type = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

    WsClient(net::io_context& ioc, ssl::context& ssl_ctx, const std::string& tag)
        : ioc_(ioc)
        , ssl_ctx_(ssl_ctx)
        , resolver_(net::make_strand(ioc))
        , ws_(std::make_unique<ws_stream_type>(net::make_strand(ioc), ssl_ctx))
        , tag_(tag)
        , parser_()
    {}

    void set_url(const std::string& url) {
        // Parse wss://host/path
        // Simple parser for wss:// URLs
        std::string_view sv(url);
        if (sv.starts_with("wss://")) sv.remove_prefix(6);

        auto slash_pos = sv.find('/');
        if (slash_pos != std::string_view::npos) {
            host_ = std::string(sv.substr(0, slash_pos));
            path_ = std::string(sv.substr(slash_pos));
        } else {
            host_ = std::string(sv);
            path_ = "/";
        }
        port_ = "443";
    }

    void set_on_message(OnMessageCb cb) { on_message_ = std::move(cb); }
    void set_on_connect(OnConnectCb cb) { on_connect_ = std::move(cb); }
    void set_on_disconnect(OnDisconnectCb cb) { on_disconnect_ = std::move(cb); }
    // E5: Called just before sending a ping — allows caller to record timestamp
    void set_on_ping(std::function<void()> cb) { on_ping_ = std::move(cb); }
    void set_ping_interval(int seconds) { ping_interval_sec_ = seconds; }
    void set_stale_timeout(int seconds) { stale_timeout_sec_ = seconds; }
    void set_reconnect_params(int base_ms, int max_ms) {
        reconnect_base_ms_ = base_ms;
        reconnect_max_ms_ = max_ms;
    }

    void connect() {
        reconnect_attempts_ = 0;
        do_resolve();
    }

    void disconnect() {
        closing_.store(true, std::memory_order_release);
        beast::error_code ec;
        if (ws_ && ws_->is_open()) {
            ws_->close(websocket::close_code::normal, ec);
        }
    }

    // M1: Accept by value + move to avoid unnecessary string copies
    void send(std::string msg) {
        auto self = shared_from_this();
        net::post(ws_->get_executor(), [self, msg = std::move(msg)]() {
            if (!self->ws_) return;
            auto buf = std::make_shared<std::string>(std::move(msg));
            self->ws_->async_write(
                net::buffer(*buf),
                [self, buf](beast::error_code ec, std::size_t) {
                    if (ec) {
                        spdlog::error("[{}] write error: {}", self->tag_, ec.message());
                    }
                });
        });
    }

    bool is_connected() const { return connected_.load(std::memory_order_acquire); }
    const std::string& tag() const { return tag_; }

    // Stale stream detection
    bool is_stale() const {
        if (!connected_.load(std::memory_order_acquire)) return false;
        uint64_t now = Clock::now_ns();
        uint64_t last = last_message_ns_.load(std::memory_order_acquire);
        uint64_t timeout_ns = static_cast<uint64_t>(stale_timeout_sec_) * 1'000'000'000ULL;
        return (now - last) > timeout_ns;
    }

    LatencyHistogram& message_latency() { return message_latency_; }

private:
    void do_resolve() {
        auto self = shared_from_this();
        resolver_.async_resolve(host_, port_,
            [self](beast::error_code ec, tcp::resolver::results_type results) {
                if (ec) {
                    spdlog::error("[{}] resolve error: {}", self->tag_, ec.message());
                    self->schedule_reconnect();
                    return;
                }
                self->do_connect(results);
            });
    }

    void do_connect(tcp::resolver::results_type results) {
        auto self = shared_from_this();
        beast::get_lowest_layer(*ws_).expires_after(std::chrono::seconds(10));
        beast::get_lowest_layer(*ws_).async_connect(results,
            [self](beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
                if (ec) {
                    spdlog::error("[{}] connect error: {}", self->tag_, ec.message());
                    self->schedule_reconnect();
                    return;
                }
                // #16: Disable Nagle's algorithm for minimal send latency
                beast::get_lowest_layer(*self->ws_).socket().set_option(
                    tcp::no_delay(true));
                self->do_ssl_handshake();
            });
    }

    void do_ssl_handshake() {
        auto self = shared_from_this();
        beast::get_lowest_layer(*ws_).expires_after(std::chrono::seconds(10));

        if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), host_.c_str())) {
            spdlog::error("[{}] SSL SNI error", tag_);
            schedule_reconnect();
            return;
        }

        ws_->next_layer().async_handshake(ssl::stream_base::client,
            [self](beast::error_code ec) {
                if (ec) {
                    spdlog::error("[{}] SSL handshake error: {}", self->tag_, ec.message());
                    self->schedule_reconnect();
                    return;
                }
                self->do_ws_handshake();
            });
    }

    void do_ws_handshake() {
        auto self = shared_from_this();
        beast::get_lowest_layer(*ws_).expires_never();

        ws_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(beast::http::field::user_agent, "bybit-hft/1.0");
            }));

        ws_->async_handshake(host_, path_,
            [self](beast::error_code ec) {
                if (ec) {
                    spdlog::error("[{}] WS handshake error: {}", self->tag_, ec.message());
                    self->schedule_reconnect();
                    return;
                }
                spdlog::info("[{}] connected", self->tag_);
                self->connected_.store(true, std::memory_order_release);
                self->reconnect_attempts_ = 0;
                self->last_message_ns_.store(Clock::now_ns(), std::memory_order_release);

                if (self->on_connect_) self->on_connect_();

                self->do_read();
                self->schedule_ping();
            });
    }

    void do_read() {
        auto self = shared_from_this();
        ws_->async_read(buffer_,
            [self](beast::error_code ec, std::size_t bytes_transferred) {
                if (ec) {
                    if (ec != websocket::error::closed) {
                        spdlog::warn("[{}] read error: {}", self->tag_, ec.message());
                    }
                    self->handle_disconnect();
                    return;
                }

                uint64_t recv_ns = Clock::now_ns();
                self->last_message_ns_.store(recv_ns, std::memory_order_release);

                // Parse with simdjson
                auto data = static_cast<const char*>(self->buffer_.data().data());
                auto len = self->buffer_.size();

                auto err = self->parser_.iterate(data, len, len + simdjson::SIMDJSON_PADDING);
                if (!err.error()) {
                    simdjson::ondemand::document doc = std::move(err.value_unsafe());

                    uint64_t parse_ns = Clock::now_ns();
                    self->message_latency_.record(parse_ns - recv_ns);

                    if (self->on_message_) {
                        self->on_message_(doc, recv_ns);
                    }
                } else {
                    spdlog::warn("[{}] JSON parse error", self->tag_);
                }

                self->buffer_.consume(self->buffer_.size());
                self->do_read();
            });
    }

    void schedule_ping() {
        if (closing_.load(std::memory_order_acquire)) return;

        auto self = shared_from_this();
        auto timer = std::make_shared<net::steady_timer>(
            ws_->get_executor(), std::chrono::seconds(ping_interval_sec_));

        timer->async_wait([self, timer](beast::error_code ec) {
            if (ec || self->closing_.load(std::memory_order_acquire)) return;
            if (!self->connected_.load(std::memory_order_acquire)) return;

            // Bybit expects JSON ping
            if (self->on_ping_) self->on_ping_();  // E5: notify before ping
            self->send(R"({"op":"ping"})");

            // Check stale
            if (self->is_stale()) {
                spdlog::warn("[{}] stale stream detected, reconnecting", self->tag_);
                self->handle_disconnect();
                return;
            }

            self->schedule_ping();
        });
    }

    void handle_disconnect() {
        bool was_connected = connected_.exchange(false, std::memory_order_acq_rel);
        if (was_connected && on_disconnect_) on_disconnect_();
        if (!closing_.load(std::memory_order_acquire)) {
            schedule_reconnect();
        }
    }

    void schedule_reconnect() {
        if (closing_.load(std::memory_order_acquire)) return;

        int delay_ms = std::min(
            reconnect_base_ms_ * (1 << std::min(reconnect_attempts_, 10)),
            reconnect_max_ms_);
        ++reconnect_attempts_;

        spdlog::info("[{}] reconnecting in {}ms (attempt {})", tag_, delay_ms, reconnect_attempts_);

        auto self = shared_from_this();
        auto timer = std::make_shared<net::steady_timer>(
            net::make_strand(ioc_), std::chrono::milliseconds(delay_ms));

        timer->async_wait([self, timer](beast::error_code ec) {
            if (ec || self->closing_.load(std::memory_order_acquire)) return;

            // Create a fresh SSL stream for reconnection.
            // unique_ptr reset safely destructs the old stream; any stale
            // async handlers referencing it will see errors, not UB.
            self->ws_ = std::make_unique<ws_stream_type>(
                net::make_strand(self->ioc_), self->ssl_ctx_);
            self->buffer_.clear();
            self->do_resolve();
        });
    }

    net::io_context& ioc_;
    ssl::context& ssl_ctx_;
    tcp::resolver resolver_;
    std::unique_ptr<ws_stream_type> ws_;
    beast::flat_buffer buffer_;

    std::string tag_;
    std::string host_;
    std::string port_;
    std::string path_;

    OnMessageCb on_message_;
    OnConnectCb on_connect_;
    OnDisconnectCb on_disconnect_;
    std::function<void()> on_ping_;  // E5: pre-ping callback

    int ping_interval_sec_ = 20;
    int stale_timeout_sec_ = 30;
    int reconnect_base_ms_ = 1000;
    int reconnect_max_ms_ = 30000;
    int reconnect_attempts_ = 0;

    std::atomic<bool> connected_{false};
    std::atomic<bool> closing_{false};
    std::atomic<uint64_t> last_message_ns_{0};

    simdjson::ondemand::parser parser_;
    LatencyHistogram message_latency_;
};

} // namespace bybit
