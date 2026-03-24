#pragma once

#include "../config/types.h"
#include "../utils/clock.h"
#include "../utils/hmac.h"
#include "../metrics/latency_histogram.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <string>
#include <functional>
#include <chrono>
#include <atomic>
#include <deque>
#include <mutex>

namespace bybit {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

using RestCallback = std::function<void(bool success, const std::string& body)>;

// E8: Token bucket rate limiter for REST API calls.
// Prevents reconnection storms and reconciliation floods from hitting
// Bybit rate limits or causing IP bans.
//
// Thread-safe: protected by internal mutex.
// Non-blocking: try_acquire() returns immediately.
class TokenBucketLimiter {
public:
    // burst_capacity: max tokens (peak burst)
    // refill_rate: tokens added per second (sustained rate)
    TokenBucketLimiter(double burst_capacity = 10.0,
                       double refill_rate = 2.0) noexcept
        : capacity_(burst_capacity)
        , refill_rate_(refill_rate)
        , tokens_(burst_capacity)
        , last_refill_ns_(Clock::now_ns())
    {}

    // Try to consume one token. Returns true if allowed, false if throttled.
    bool try_acquire() noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        refill();
        if (tokens_ >= 1.0) {
            tokens_ -= 1.0;
            return true;
        }
        ++throttled_count_;
        return false;
    }

    // Time in milliseconds until next token is available
    uint64_t wait_ms() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        if (tokens_ >= 1.0) return 0;
        double deficit = 1.0 - tokens_;
        double wait_sec = deficit / refill_rate_;
        return static_cast<uint64_t>(wait_sec * 1000.0) + 1;
    }

    double tokens() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return tokens_;
    }

    uint64_t throttled_count() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return throttled_count_;
    }

private:
    void refill() noexcept {
        uint64_t now = Clock::now_ns();
        double elapsed_sec = static_cast<double>(now - last_refill_ns_) / 1e9;
        last_refill_ns_ = now;
        tokens_ = std::min(capacity_, tokens_ + elapsed_sec * refill_rate_);
    }

    double capacity_;
    double refill_rate_;
    double tokens_;
    uint64_t last_refill_ns_;
    uint64_t throttled_count_ = 0;
    mutable std::mutex mu_;
};

// #1: Persistent SSL connection wrapper for connection pooling
struct PersistentConnection {
    std::shared_ptr<beast::ssl_stream<beast::tcp_stream>> stream;
    bool in_use = false;
    uint64_t last_used_ns = 0;
    uint32_t requests_served = 0;
};

class RestClient {
public:
    RestClient(net::io_context& ioc, ssl::context& ssl_ctx, const AppConfig& cfg)
        : ioc_(ioc)
        , ssl_ctx_(ssl_ctx)
        , api_key_(cfg.api_key)
        , api_secret_(cfg.api_secret)
        , max_retries_(cfg.rest_max_retries)
        , timeout_ms_(cfg.rest_timeout_ms)
    {
        // Parse base URL
        std::string_view url(cfg.rest_base_url);
        if (url.starts_with("https://")) url.remove_prefix(8);
        host_ = std::string(url);
        port_ = "443";
    }

    // #7: Rate limit accessors
    int rate_limit_remaining() const { return rate_limit_remaining_.load(std::memory_order_relaxed); }
    int rate_limit_total() const { return rate_limit_total_.load(std::memory_order_relaxed); }

    // E8: Token bucket rate limiter accessors
    double limiter_tokens() const { return limiter_.tokens(); }
    uint64_t limiter_throttled() const { return limiter_.throttled_count(); }

    // ─── Order Operations ───────────────────────────────────────────────────

    void create_order(const std::string& symbol, Side side, OrderType type,
                      double qty, double price, TimeInForce tif,
                      bool reduce_only, RestCallback cb) {
        // S2: fmt::format instead of nlohmann::json — zero heap overhead
        const char* side_str = (side == Side::Buy) ? "Buy" : "Sell";
        const char* type_str = (type == OrderType::Limit) ? "Limit" : "Market";
        const char* tif_str = "GTC";
        switch (tif) {
            case TimeInForce::GTC: tif_str = "GTC"; break;
            case TimeInForce::IOC: tif_str = "IOC"; break;
            case TimeInForce::FOK: tif_str = "FOK"; break;
            case TimeInForce::PostOnly: tif_str = "PostOnly"; break;
        }

        std::string price_part = (type == OrderType::Limit)
            ? fmt::format(R"(,"price":"{:.1f}")", price) : "";
        std::string reduce_part = reduce_only ? R"(,"reduceOnly":true)" : "";

        std::string body = fmt::format(
            R"({{"category":"linear","symbol":"{}","side":"{}","orderType":"{}","qty":"{:.3f}","timeInForce":"{}"{}{}}})",
            symbol, side_str, type_str, qty, tif_str, price_part, reduce_part);

        post("/v5/order/create", body, std::move(cb));
    }

    void cancel_order(const std::string& symbol, const std::string& order_id, RestCallback cb) {
        std::string body = fmt::format(
            R"({{"category":"linear","symbol":"{}","orderId":"{}"}})",
            symbol, order_id);
        post("/v5/order/cancel", body, std::move(cb));
    }

    void amend_order(const std::string& symbol, const std::string& order_id,
                     double new_qty, double new_price, RestCallback cb) {
        std::string qty_part = new_qty > 0.0
            ? fmt::format(R"(,"qty":"{:.3f}")", new_qty) : "";
        std::string price_part = new_price > 0.0
            ? fmt::format(R"(,"price":"{:.1f}")", new_price) : "";

        std::string body = fmt::format(
            R"({{"category":"linear","symbol":"{}","orderId":"{}"{}{}}})",
            symbol, order_id, qty_part, price_part);
        post("/v5/order/amend", body, std::move(cb));
    }

    void cancel_all_orders(const std::string& symbol, RestCallback cb) {
        std::string body = fmt::format(
            R"({{"category":"linear","symbol":"{}"}})", symbol);
        post("/v5/order/cancel-all", body, std::move(cb));
    }

    // ─── Market Data (for OB resync) ────────────────────────────────────────

    void get_orderbook(const std::string& symbol, int depth, RestCallback cb) {
        std::string path = fmt::format("/v5/market/orderbook?category=linear&symbol={}&limit={}", symbol, depth);
        get(path, std::move(cb));
    }

    // E3: Fetch instrument info (tick_size, lot_size)
    void get_instrument_info(const std::string& symbol, RestCallback cb) {
        std::string path = fmt::format("/v5/market/instruments-info?category=linear&symbol={}", symbol);
        get(path, std::move(cb));
    }

    // #8: Get open orders for reconciliation
    void get_open_orders(const std::string& symbol, RestCallback cb) {
        std::string path = fmt::format(
            "/v5/order/realtime?category=linear&symbol={}&limit=50", symbol);
        get_signed(path, std::move(cb));
    }

    LatencyHistogram& submit_latency() { return submit_latency_; }

private:
    // #7: Extract rate limit headers from response
    void extract_rate_limits(const http::response<http::string_body>& res) {
        auto remaining = res.find("X-Bapi-Limit-Status");
        if (remaining != res.end()) {
            try {
                int val = std::stoi(std::string(remaining->value()));
                rate_limit_remaining_.store(val, std::memory_order_relaxed);
            } catch (...) {}
        }
        auto total = res.find("X-Bapi-Limit");
        if (total != res.end()) {
            try {
                int val = std::stoi(std::string(total->value()));
                rate_limit_total_.store(val, std::memory_order_relaxed);
            } catch (...) {}
        }
    }

    // #1: Acquire a persistent connection from pool, or create new one
    // C5: Enforce MAX_POOL_SIZE — evict stale connections when pool is full
    std::shared_ptr<beast::ssl_stream<beast::tcp_stream>> acquire_connection() {
        std::lock_guard<std::mutex> lk(pool_mu_);
        for (auto& conn : pool_) {
            if (!conn.in_use) {
                conn.in_use = true;
                conn.last_used_ns = Clock::now_ns();
                ++conn.requests_served;
                return conn.stream;
            }
        }
        // C5: If pool is at max capacity, evict the oldest idle or exhausted connection
        if (pool_.size() >= MAX_POOL_SIZE) {
            // Find and evict the least recently used non-in-use connection
            auto oldest_it = pool_.end();
            uint64_t oldest_time = UINT64_MAX;
            for (auto it = pool_.begin(); it != pool_.end(); ++it) {
                if (!it->in_use && it->last_used_ns < oldest_time) {
                    oldest_time = it->last_used_ns;
                    oldest_it = it;
                }
            }
            if (oldest_it != pool_.end()) {
                if (oldest_it->stream) {
                    oldest_it->stream->async_shutdown([s = oldest_it->stream](beast::error_code) {});
                }
                pool_.erase(oldest_it);
            } else {
                // All connections in use and pool full — create one anyway but log warning
                spdlog::warn("[REST] Connection pool full ({} active), creating overflow connection", pool_.size());
            }
        }
        // Create new connection
        auto stream = std::make_shared<beast::ssl_stream<beast::tcp_stream>>(
            net::make_strand(ioc_), ssl_ctx_);
        SSL_set_tlsext_host_name(stream->native_handle(), host_.c_str());
        pool_.push_back({stream, true, Clock::now_ns(), 1});
        return stream;
    }

    // #1: Return connection to pool for reuse
    void release_connection(const std::shared_ptr<beast::ssl_stream<beast::tcp_stream>>& stream, bool reusable) {
        std::lock_guard<std::mutex> lk(pool_mu_);
        for (auto& conn : pool_) {
            if (conn.stream == stream) {
                if (reusable && conn.requests_served < MAX_CONN_REUSE) {
                    conn.in_use = false; // available for reuse
                } else {
                    // Evict: shutdown and remove
                    stream->async_shutdown([stream](beast::error_code) {});
                    conn.stream = nullptr;
                    conn.in_use = false;
                }
                return;
            }
        }
        // Not found in pool — just shutdown
        stream->async_shutdown([stream](beast::error_code) {});
    }

    // C6: HTTP method enum for correct retry dispatch
    enum class HttpMethod { GET, GET_SIGNED, POST };

    // #1: Try to send request on an existing connected stream
    // C6: Added method parameter for correct retry dispatch
    void send_on_stream(
            const std::shared_ptr<beast::ssl_stream<beast::tcp_stream>>& stream,
            const std::shared_ptr<http::request<http::string_body>>& req,
            RestCallback cb, uint64_t start_ns,
            const std::string& path, const std::string& body, int attempt,
            HttpMethod method = HttpMethod::POST) {
        auto res = std::make_shared<http::response<http::string_body>>();
        auto buffer = std::make_shared<beast::flat_buffer>();

        beast::get_lowest_layer(*stream).expires_after(std::chrono::milliseconds(timeout_ms_));
        http::async_write(*stream, *req,
            [this, stream, res, buffer, cb, start_ns, path, body, attempt, method]
            (beast::error_code ec, std::size_t) {
                if (ec) {
                    release_connection(stream, false);
                    handle_error("write", ec, path, body, std::move(cb), attempt, method);
                    return;
                }
                http::async_read(*stream, *buffer, *res,
                    [this, stream, res, cb, start_ns]
                    (beast::error_code ec, std::size_t) {
                        uint64_t end_ns = Clock::now_ns();
                        submit_latency_.record(end_ns - start_ns);

                        if (ec) {
                            release_connection(stream, false);
                            if (cb) cb(false, ec.message());
                            return;
                        }

                        // #7: Track rate limits from response headers
                        extract_rate_limits(*res);

                        bool ok = (res->result() == http::status::ok);
                        // #1: Return to pool for reuse (keep-alive)
                        release_connection(stream, ok);
                        if (cb) cb(ok, res->body());
                    });
            });
    }

    void post(const std::string& path, const std::string& body, RestCallback cb, int attempt = 0) {
        // E8: Token bucket rate limiting — defer if throttled
        if (!limiter_.try_acquire()) {
            uint64_t wait = limiter_.wait_ms();
            spdlog::warn("[RATE_LIMIT] POST {} throttled (tokens={:.1f}, wait={}ms, throttled={})",
                         path, limiter_.tokens(), wait, limiter_.throttled_count());
            auto timer = std::make_shared<net::steady_timer>(ioc_);
            timer->expires_after(std::chrono::milliseconds(wait));
            timer->async_wait([this, timer, path, body, cb, attempt](beast::error_code ec) {
                if (!ec) post(path, body, std::move(cb), attempt);
                else if (cb) cb(false, "Rate limit timer cancelled");
            });
            return;
        }
        auto start_ns = Clock::now_ns();

        // #7: Check rate limit before sending
        if (rate_limit_remaining_.load(std::memory_order_relaxed) <= 1) {
            spdlog::warn("[RATE_LIMIT] Near exchange limit ({}/{})",
                         rate_limit_remaining_.load(), rate_limit_total_.load());
        }

        uint64_t timestamp = Clock::wall_ms();
        int recv_window = 5000;

        std::string sign_payload = fmt::format("{}{}{}{}", timestamp, api_key_, recv_window, body);
        std::string signature = hmac_sha256(api_secret_, sign_payload);

        auto req = std::make_shared<http::request<http::string_body>>(http::verb::post, path, 11);
        req->set(http::field::host, host_);
        req->set(http::field::content_type, "application/json");
        req->set(http::field::connection, "keep-alive");  // #1: Request keep-alive
        req->set("X-BAPI-API-KEY", api_key_);
        req->set("X-BAPI-TIMESTAMP", std::to_string(timestamp));
        req->set("X-BAPI-SIGN", signature);
        req->set("X-BAPI-RECV-WINDOW", std::to_string(recv_window));
        req->body() = body;
        req->prepare_payload();

        // #1: Try to reuse a pooled connection
        auto stream = acquire_connection();
        if (beast::get_lowest_layer(*stream).socket().is_open()) {
            // Connection already established — send directly
            send_on_stream(stream, req, std::move(cb), start_ns, path, body, attempt, HttpMethod::POST);
            return;
        }

        // New connection — resolve + connect + SSL handshake
        auto resolver = std::make_shared<tcp::resolver>(net::make_strand(ioc_));
        resolver->async_resolve(host_, port_,
            [this, stream, req, resolver, cb, attempt, start_ns, path, body]
            (beast::error_code ec, tcp::resolver::results_type results) {
                if (ec) {
                    release_connection(stream, false);
                    handle_error("resolve", ec, path, body, std::move(cb), attempt, HttpMethod::POST);
                    return;
                }
                beast::get_lowest_layer(*stream).expires_after(std::chrono::milliseconds(timeout_ms_));
                beast::get_lowest_layer(*stream).async_connect(results,
                    [this, stream, req, cb, attempt, start_ns, path, body]
                    (beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
                        if (ec) {
                            release_connection(stream, false);
                            handle_error("connect", ec, path, body, std::move(cb), attempt, HttpMethod::POST);
                            return;
                        }
                        beast::get_lowest_layer(*stream).socket().set_option(
                            tcp::no_delay(true));
                        stream->async_handshake(ssl::stream_base::client,
                            [this, stream, req, cb, attempt, start_ns, path, body]
                            (beast::error_code ec) {
                                if (ec) {
                                    release_connection(stream, false);
                                    handle_error("ssl", ec, path, body, std::move(cb), attempt, HttpMethod::POST);
                                    return;
                                }
                                send_on_stream(stream, req, std::move(cb), start_ns, path, body, attempt, HttpMethod::POST);
                            });
                    });
            });
    }

    void get(const std::string& path, RestCallback cb) {
        // E8: Token bucket rate limiting
        if (!limiter_.try_acquire()) {
            uint64_t wait = limiter_.wait_ms();
            spdlog::warn("[RATE_LIMIT] GET {} throttled (wait={}ms)", path, wait);
            auto timer = std::make_shared<net::steady_timer>(ioc_);
            timer->expires_after(std::chrono::milliseconds(wait));
            timer->async_wait([this, timer, path, cb](beast::error_code ec) {
                if (!ec) get(path, std::move(cb));
                else if (cb) cb(false, "Rate limit timer cancelled");
            });
            return;
        }
        auto start_ns = Clock::now_ns();

        auto req = std::make_shared<http::request<http::string_body>>(http::verb::get, path, 11);
        req->set(http::field::host, host_);
        req->set(http::field::connection, "keep-alive");  // #1

        auto stream = acquire_connection();
        if (beast::get_lowest_layer(*stream).socket().is_open()) {
            send_on_stream(stream, req, std::move(cb), start_ns, path, "", 0, HttpMethod::GET);
            return;
        }

        auto resolver = std::make_shared<tcp::resolver>(net::make_strand(ioc_));
        resolver->async_resolve(host_, port_,
            [this, stream, req, resolver, cb, start_ns, path]
            (beast::error_code ec, tcp::resolver::results_type results) {
                if (ec) { release_connection(stream, false); handle_error("resolve", ec, path, "", std::move(cb), 0, HttpMethod::GET); return; }
                beast::get_lowest_layer(*stream).expires_after(std::chrono::milliseconds(timeout_ms_));
                beast::get_lowest_layer(*stream).async_connect(results,
                    [this, stream, req, cb, start_ns, path]
                    (beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
                        if (ec) { release_connection(stream, false); handle_error("connect", ec, path, "", std::move(cb), 0, HttpMethod::GET); return; }
                        beast::get_lowest_layer(*stream).socket().set_option(
                            tcp::no_delay(true));
                        stream->async_handshake(ssl::stream_base::client,
                            [this, stream, req, cb, start_ns, path]
                            (beast::error_code ec) {
                                if (ec) { release_connection(stream, false); handle_error("ssl", ec, path, "", std::move(cb), 0, HttpMethod::GET); return; }
                                send_on_stream(stream, req, std::move(cb), start_ns, path, "", 0, HttpMethod::GET);
                            });
                    });
            });
    }

    // #8: GET with authentication for private endpoints
    void get_signed(const std::string& path, RestCallback cb) {
        // E8: Token bucket rate limiting
        if (!limiter_.try_acquire()) {
            uint64_t wait = limiter_.wait_ms();
            spdlog::warn("[RATE_LIMIT] GET_SIGNED {} throttled (wait={}ms)", path, wait);
            auto timer = std::make_shared<net::steady_timer>(ioc_);
            timer->expires_after(std::chrono::milliseconds(wait));
            timer->async_wait([this, timer, path, cb](beast::error_code ec) {
                if (!ec) get_signed(path, std::move(cb));
                else if (cb) cb(false, "Rate limit timer cancelled");
            });
            return;
        }
        auto start_ns = Clock::now_ns();
        uint64_t timestamp = Clock::wall_ms();
        int recv_window = 5000;

        std::string sign_payload = fmt::format("{}{}{}", timestamp, api_key_, recv_window);
        std::string signature = hmac_sha256(api_secret_, sign_payload);

        auto req = std::make_shared<http::request<http::string_body>>(http::verb::get, path, 11);
        req->set(http::field::host, host_);
        req->set(http::field::connection, "keep-alive");
        req->set("X-BAPI-API-KEY", api_key_);
        req->set("X-BAPI-TIMESTAMP", std::to_string(timestamp));
        req->set("X-BAPI-SIGN", signature);
        req->set("X-BAPI-RECV-WINDOW", std::to_string(recv_window));

        auto stream = acquire_connection();
        if (beast::get_lowest_layer(*stream).socket().is_open()) {
            send_on_stream(stream, req, std::move(cb), start_ns, path, "", 0, HttpMethod::GET_SIGNED);
            return;
        }

        auto resolver = std::make_shared<tcp::resolver>(net::make_strand(ioc_));
        resolver->async_resolve(host_, port_,
            [this, stream, req, resolver, cb, start_ns, path]
            (beast::error_code ec, tcp::resolver::results_type results) {
                if (ec) { release_connection(stream, false); handle_error("resolve", ec, path, "", std::move(cb), 0, HttpMethod::GET_SIGNED); return; }
                beast::get_lowest_layer(*stream).expires_after(std::chrono::milliseconds(timeout_ms_));
                beast::get_lowest_layer(*stream).async_connect(results,
                    [this, stream, req, cb, start_ns, path]
                    (beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
                        if (ec) { release_connection(stream, false); handle_error("connect", ec, path, "", std::move(cb), 0, HttpMethod::GET_SIGNED); return; }
                        beast::get_lowest_layer(*stream).socket().set_option(
                            tcp::no_delay(true));
                        stream->async_handshake(ssl::stream_base::client,
                            [this, stream, req, cb, start_ns, path]
                            (beast::error_code ec) {
                                if (ec) { release_connection(stream, false); handle_error("ssl", ec, path, "", std::move(cb), 0, HttpMethod::GET_SIGNED); return; }
                                send_on_stream(stream, req, std::move(cb), start_ns, path, "", 0, HttpMethod::GET_SIGNED);
                            });
                    });
            });
    }

    void handle_error(const char* stage, beast::error_code ec,
                      const std::string& path, const std::string& body,
                      RestCallback cb, int attempt,
                      HttpMethod method = HttpMethod::POST) {
        spdlog::warn("REST {} error on {}: {} (attempt {}/{})", stage, path, ec.message(), attempt + 1, max_retries_);
        if (attempt + 1 < max_retries_) {
            // Retry with exponential backoff using the CORRECT HTTP method
            auto timer = std::make_shared<net::steady_timer>(
                ioc_, std::chrono::milliseconds(100 * (1 << attempt)));
            timer->async_wait([this, timer, path, body, cb, attempt, method](beast::error_code ec) {
                if (ec) return;
                switch (method) {
                    case HttpMethod::POST:
                        post(path, body, std::move(cb), attempt + 1);
                        break;
                    case HttpMethod::GET:
                        get(path, std::move(cb));
                        break;
                    case HttpMethod::GET_SIGNED:
                        get_signed(path, std::move(cb));
                        break;
                }
            });
        } else {
            if (cb) cb(false, ec.message());
        }
    }

    static constexpr uint32_t MAX_CONN_REUSE = 100;  // #1: Max requests per connection
    static constexpr size_t MAX_POOL_SIZE = 4;         // #1: Max connections in pool

    net::io_context& ioc_;
    ssl::context& ssl_ctx_;

    std::string host_;
    std::string port_;
    std::string api_key_;
    std::string api_secret_;
    int max_retries_;
    int timeout_ms_;

    LatencyHistogram submit_latency_;

    // #1: Connection pool
    std::deque<PersistentConnection> pool_;
    std::mutex pool_mu_;

    // #7: Rate limit tracking
    std::atomic<int> rate_limit_remaining_{20};
    std::atomic<int> rate_limit_total_{20};

    // E8: Token bucket rate limiter (burst=10, sustained=2 req/s)
    mutable TokenBucketLimiter limiter_{10.0, 2.0};
};

} // namespace bybit
