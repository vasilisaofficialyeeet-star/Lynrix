#pragma once

#include "../config/types.h"
#include "../core/market_data.h"
#include "../ws_client/ws_client.h"
#include "../orderbook/orderbook.h"
#include "../trade_flow/trade_flow_engine.h"
#include "../portfolio/portfolio.h"
#include "../execution_engine/smart_execution.h"
#include "../metrics/latency_histogram.h"
#include "../utils/clock.h"
#include "../utils/fast_double.h"
#include "../rest_client/rest_client.h"

#include <simdjson.h>
#include <spdlog/spdlog.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <memory>
#include <string>
#include <atomic>
#include <functional>
#include <cstring>

namespace bybit {

namespace net = boost::asio;
namespace ssl = net::ssl;

// Manages public and private WebSocket connections.
// Dispatches parsed messages to appropriate engines.
// #2: Callback type for event-driven strategy tick
using OnBookUpdateCb = std::function<void()>;

class WsManager {
public:
    WsManager(net::io_context& ioc, ssl::context& ssl_ctx,
              const AppConfig& cfg, OrderBook& ob,
              TradeFlowEngine& tf, Portfolio& portfolio,
              SmartExecutionEngine& exec, Metrics& metrics,
              RestClient& rest)
        : ioc_(ioc)
        , ssl_ctx_(ssl_ctx)
        , cfg_(cfg)
        , ob_(ob)
        , tf_(tf)
        , portfolio_(portfolio)
        , exec_(exec)
        , metrics_(metrics)
        , rest_(rest)
        , resync_state_(ResyncState::Normal)
    {}

    // E5: WS round-trip time in microseconds (measured via ping/pong)
    uint64_t ws_rtt_us() const noexcept { return ws_rtt_us_; }

    // E6: Backpressure — number of deltas dropped due to processing lag
    uint64_t backpressure_drops() const noexcept {
        return ob_.md_counters().backpressure_drops;
    }

    // #2: Register callback for event-driven strategy tick on OB update
    void set_on_book_update(OnBookUpdateCb cb) { on_book_update_ = std::move(cb); }

    void start() {
        // E3: Fetch instrument info (tick_size, lot_size) from REST API
        fetch_instrument_info();

        setup_public_ws();
        if (!cfg_.api_key.empty() && !cfg_.paper_trading) {
            setup_private_ws();
        }
    }

    void stop() {
        if (public_ws_) public_ws_->disconnect();
        if (private_ws_) private_ws_->disconnect();
    }

    bool public_connected() const { return public_ws_ && public_ws_->is_connected(); }
    bool private_connected() const { return private_ws_ && private_ws_->is_connected(); }

private:
    void setup_public_ws() {
        public_ws_ = std::make_shared<WsClient>(ioc_, ssl_ctx_, "public");
        public_ws_->set_url(cfg_.ws_public_url);
        public_ws_->set_ping_interval(cfg_.ws_ping_interval_sec);
        public_ws_->set_stale_timeout(cfg_.ws_stale_timeout_sec);
        public_ws_->set_reconnect_params(cfg_.ws_reconnect_base_ms, cfg_.ws_reconnect_max_ms);

        public_ws_->set_on_connect([this]() {
            // Subscribe to orderbook and trades
            std::string sub = fmt::format(
                R"({{"op":"subscribe","args":["orderbook.{}.{}","publicTrade.{}"]}})",
                cfg_.ob_levels, cfg_.symbol, cfg_.symbol);
            public_ws_->send(sub);
            spdlog::info("Subscribed to public channels for {}", cfg_.symbol);
        });

        public_ws_->set_on_disconnect([this]() {
            spdlog::warn("Public WS disconnected");
            metrics_.ws_reconnects_total.fetch_add(1, std::memory_order_relaxed);
            ob_.invalidate(BookState::InvalidDisconnect);
            metrics_.ob_invalidations_total.fetch_add(1, std::memory_order_relaxed);
            resync_state_ = ResyncState::Normal; // allow resync on reconnect
        });

        public_ws_->set_on_message([this](simdjson::ondemand::document& doc, uint64_t recv_ns) {
            handle_public_message(doc, recv_ns);
        });

        // E5: Record ping send timestamp for RTT measurement
        public_ws_->set_on_ping([this]() {
            last_ping_ns_ = Clock::now_ns();
        });

        public_ws_->connect();
    }

    void setup_private_ws() {
        private_ws_ = std::make_shared<WsClient>(ioc_, ssl_ctx_, "private");
        private_ws_->set_url(cfg_.ws_private_url);
        private_ws_->set_ping_interval(cfg_.ws_ping_interval_sec);
        private_ws_->set_stale_timeout(cfg_.ws_stale_timeout_sec);
        private_ws_->set_reconnect_params(cfg_.ws_reconnect_base_ms, cfg_.ws_reconnect_max_ms);

        private_ws_->set_on_connect([this]() {
            // Authenticate
            uint64_t expires = Clock::wall_ms() + 10000;
            std::string sign_str = "GET/realtime" + std::to_string(expires);
            std::string sign = hmac_sha256(cfg_.api_secret, sign_str);

            std::string auth = fmt::format(
                R"({{"op":"auth","args":["{}",{},"{}"]}})",
                cfg_.api_key, expires, sign);
            private_ws_->send(auth);

            // Subscribe to private channels after small delay
            auto timer = std::make_shared<net::steady_timer>(
                ioc_, std::chrono::milliseconds(500));
            timer->async_wait([this, timer](boost::system::error_code) {
                std::string sub = R"({"op":"subscribe","args":["order","execution","position"]})";
                private_ws_->send(sub);
                spdlog::info("Subscribed to private channels");
            });
        });

        private_ws_->set_on_disconnect([this]() {
            spdlog::warn("Private WS disconnected");
            metrics_.ws_reconnects_total.fetch_add(1, std::memory_order_relaxed);
        });

        private_ws_->set_on_message([this](simdjson::ondemand::document& doc, uint64_t recv_ns) {
            handle_private_message(doc, recv_ns);
        });

        private_ws_->connect();
    }

    void handle_public_message(simdjson::ondemand::document& doc, uint64_t recv_ns) {
        uint64_t start_ns = Clock::now_ns();

        // E5: Detect pong response for RTT measurement
        auto op_field = doc.find_field("op");
        if (!op_field.error()) {
            std::string_view op = op_field.get_string().value();
            if (op == "pong" && last_ping_ns_ > 0) {
                ws_rtt_us_ = (recv_ns - last_ping_ns_) / 1000;
                last_ping_ns_ = 0;
            }
            return; // op messages (pong, subscribe ack) are not data
        }

        auto topic_result = doc.find_field("topic");
        if (topic_result.error()) {
            return;
        }

        std::string_view topic = topic_result.get_string().value();

        if (topic.starts_with("orderbook")) {
            handle_orderbook_message(doc, recv_ns);
            metrics_.ob_updates_total.fetch_add(1, std::memory_order_relaxed);
            metrics_.ob_update_latency.record(Clock::now_ns() - start_ns);
        } else if (topic.starts_with("publicTrade")) {
            handle_trade_message(doc, recv_ns);
        }
    }

    void handle_orderbook_message(simdjson::ondemand::document& doc, uint64_t recv_ns) {
        auto type_result = doc.find_field("type");
        if (type_result.error()) return;
        std::string_view type = type_result.get_string().value();

        auto data = doc.find_field("data");
        if (data.error()) return;

        auto seq_result = data.find_field("seq");
        uint64_t seq = seq_result.error() ? 0 : seq_result.get_uint64().value();

        // Parse bids
        PriceLevel bids_buf[MAX_OB_LEVELS];
        PriceLevel asks_buf[MAX_OB_LEVELS];
        size_t bid_count = 0;
        size_t ask_count = 0;

        auto bids_arr = data.find_field("b");
        if (!bids_arr.error()) {
            for (auto level : bids_arr.get_array()) {
                if (bid_count >= MAX_OB_LEVELS) break;
                auto arr = level.get_array();
                auto it = arr.begin();
                std::string_view price_str = (*it).get_string().value();
                ++it;
                std::string_view qty_str = (*it).get_string().value();

                bids_buf[bid_count].price = fast_atof(price_str.data(), price_str.size());
                bids_buf[bid_count].qty = fast_atof(qty_str.data(), qty_str.size());
                ++bid_count;
            }
        }

        auto asks_arr = data.find_field("a");
        if (!asks_arr.error()) {
            for (auto level : asks_arr.get_array()) {
                if (ask_count >= MAX_OB_LEVELS) break;
                auto arr = level.get_array();
                auto it = arr.begin();
                std::string_view price_str = (*it).get_string().value();
                ++it;
                std::string_view qty_str = (*it).get_string().value();

                asks_buf[ask_count].price = fast_atof(price_str.data(), price_str.size());
                asks_buf[ask_count].qty = fast_atof(qty_str.data(), qty_str.size());
                ++ask_count;
            }
        }

        if (type == "snapshot") {
            ob_.apply_snapshot(bids_buf, bid_count, asks_buf, ask_count, seq);
            ob_.md_counters().snapshots_applied++;
            resync_state_ = ResyncState::Normal;
            // #22: Moved spdlog to debug level — snapshot is cold-path event
            spdlog::debug("OB snapshot applied: {} bids, {} asks, seq={}", bid_count, ask_count, seq);
            // #2: Trigger event-driven strategy tick
            if (on_book_update_) on_book_update_();
        } else if (type == "delta") {
            // E6: Backpressure — drop deltas if processing is falling behind
            uint64_t lag_ns = Clock::now_ns() - recv_ns;
            if (lag_ns > 50'000'000ULL) { // >50ms behind = drop
                ob_.md_counters().backpressure_drops++;
                return;
            }

            SequenceNumber seq_typed{seq};
            DeltaResult result = ob_.apply_delta_typed(
                bids_buf, bid_count, asks_buf, ask_count, seq_typed);

            switch (result) {
                case DeltaResult::Applied: {
                    // E2: Validate CRC32 checksum if present
                    auto cs_field = data.find_field("cs");
                    if (!cs_field.error()) {
                        uint32_t expected_cs = static_cast<uint32_t>(cs_field.get_uint64().value());
                        ob_.validate_checksum(expected_cs);
                        if (!ob_.last_checksum_valid()) {
                            spdlog::warn("[E2] OB checksum mismatch: expected={:#x} computed={:#x}",
                                         ob_.last_expected_cs(), ob_.last_computed_cs());
                            request_ob_resync();
                            break;
                        }
                    }
                    // #2: Trigger event-driven strategy tick on every applied delta
                    if (on_book_update_) on_book_update_();
                    break;
                }
                case DeltaResult::GapDetected: {
                    // #22: Gap detection is rare — keep warn but it's already cold-ish
                    const auto& gap = ob_.last_gap();
                    spdlog::warn("[MD] Sequence gap: expected={}, got={}, gap_size={}",
                                 gap.expected.raw(), gap.received.raw(), gap.gap_size);
                    metrics_.seq_gaps_total.fetch_add(1, std::memory_order_relaxed);
                    metrics_.ob_invalidations_total.fetch_add(1, std::memory_order_relaxed);
                    request_ob_resync();
                    break;
                }
                case DeltaResult::BookNotValid:
                    metrics_.deltas_dropped_total.fetch_add(1, std::memory_order_relaxed);
                    break;
                case DeltaResult::StaleRejected:
                    break; // harmless
                case DeltaResult::DuplicateSeq:
                    break; // harmless
            }
        }
    }

    void handle_trade_message(simdjson::ondemand::document& doc, uint64_t recv_ns) {
        auto data = doc.find_field("data");
        if (data.error()) return;

        for (auto trade_obj : data.get_array()) {
            Trade t;
            t.timestamp_ns = recv_ns;

            auto p = trade_obj.find_field("p");
            if (!p.error()) {
                std::string_view ps = p.get_string().value();
                t.price = fast_atof(ps.data(), ps.size());
            }

            auto v = trade_obj.find_field("v");
            if (!v.error()) {
                std::string_view vs = v.get_string().value();
                t.qty = fast_atof(vs.data(), vs.size());
            }

            auto s = trade_obj.find_field("S");
            if (!s.error()) {
                std::string_view side = s.get_string().value();
                t.is_buyer_maker = (side == "Sell");
            }

            tf_.on_trade(t);
            metrics_.trades_total.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void handle_private_message(simdjson::ondemand::document& doc, uint64_t recv_ns) {
        auto topic_result = doc.find_field("topic");
        if (topic_result.error()) return;
        std::string_view topic = topic_result.get_string().value();

        auto data = doc.find_field("data");
        if (data.error()) return;

        if (topic == "order") {
            for (auto item : data.get_array()) {
                simdjson::ondemand::value val = item.value();
                handle_order_update(val);
            }
        } else if (topic == "execution") {
            for (auto item : data.get_array()) {
                simdjson::ondemand::value val = item.value();
                handle_execution(val);
            }
        } else if (topic == "position") {
            for (auto item : data.get_array()) {
                simdjson::ondemand::value val = item.value();
                handle_position_update(val);
            }
        }
    }

    void handle_order_update(simdjson::ondemand::value& item) {
        auto oid = item.find_field("orderId");
        if (oid.error()) return;
        std::string_view order_id = oid.get_string().value();

        auto status_field = item.find_field("orderStatus");
        if (status_field.error()) return;
        std::string_view status_str = status_field.get_string().value();

        OrderStatus status = OrderStatus::New;
        if (status_str == "New") status = OrderStatus::New;
        else if (status_str == "PartiallyFilled") status = OrderStatus::PartiallyFilled;
        else if (status_str == "Filled") status = OrderStatus::Filled;
        else if (status_str == "Cancelled") status = OrderStatus::Cancelled;
        else if (status_str == "Rejected") status = OrderStatus::Rejected;

        double filled_qty = 0.0;
        auto cum = item.find_field("cumExecQty");
        if (!cum.error()) {
            std::string_view qs = cum.get_string().value();
            filled_qty = fast_atof(qs.data(), qs.size());
        }

        double avg_price = 0.0;
        auto ap = item.find_field("avgPrice");
        if (!ap.error()) {
            std::string_view ps = ap.get_string().value();
            avg_price = fast_atof(ps.data(), ps.size());
        }

        // Use a fixed-size buffer for order_id to pass to exec
        char oid_buf[48] = {};
        std::memcpy(oid_buf, order_id.data(), std::min(order_id.size(), sizeof(oid_buf) - 1));
        exec_.on_order_update(oid_buf, status, filled_qty, avg_price);
    }

    void handle_execution(simdjson::ondemand::value& item) {
        auto exec_type = item.find_field("execType");
        if (exec_type.error()) return;
        std::string_view et = exec_type.get_string().value();

        if (et == "Trade") {
            auto pnl_field = item.find_field("closedPnl");
            if (!pnl_field.error()) {
                std::string_view ps = pnl_field.get_string().value();
                double pnl = fast_atof(ps.data(), ps.size());
                if (std::abs(pnl) > 1e-12) {
                    portfolio_.add_realized_pnl(Notional(pnl));
                }
            }
        } else if (et == "Funding") {
            auto fee_field = item.find_field("execFee");
            if (!fee_field.error()) {
                std::string_view fs = fee_field.get_string().value();
                double fee = fast_atof(fs.data(), fs.size());
                portfolio_.add_funding(Notional(-fee));
            }
        }
    }

    void handle_position_update(simdjson::ondemand::value& item) {
        auto size_field = item.find_field("size");
        auto entry_field = item.find_field("entryPrice");
        auto side_field = item.find_field("side");

        if (size_field.error() || entry_field.error() || side_field.error()) return;

        std::string_view sz_str = size_field.get_string().value();
        std::string_view ep_str = entry_field.get_string().value();
        std::string_view side_str = side_field.get_string().value();

        double sz = fast_atof(sz_str.data(), sz_str.size());
        double ep = fast_atof(ep_str.data(), ep_str.size());
        Side side = (side_str == "Buy") ? Side::Buy : Side::Sell;

        portfolio_.update_position(Qty(sz), Price(ep), side);
    }

    void request_ob_resync() {
        // Prevent duplicate concurrent resync requests
        if (resync_state_ == ResyncState::PendingResync) {
            spdlog::debug("[MD] Resync already in progress, skipping duplicate request");
            return;
        }
        resync_state_ = ResyncState::PendingResync;
        ob_.mark_pending_resync();
        ob_.md_counters().resyncs_triggered++;
        metrics_.ob_resyncs_total.fetch_add(1, std::memory_order_relaxed);

        // #9: OB resync now uses simdjson + fast_atof instead of nlohmann + std::stod
        rest_.get_orderbook(cfg_.symbol, cfg_.ob_levels,
            [this](bool success, const std::string& body) {
                if (!success) {
                    spdlog::error("OB resync REST request failed");
                    return;
                }

                // #9: Parse with simdjson — zero-alloc, ~10x faster than nlohmann
                auto padded = simdjson::padded_string(body);
                simdjson::ondemand::document doc;
                auto err = resync_parser_.iterate(padded);
                if (err.error()) {
                    spdlog::error("OB resync: simdjson parse error");
                    ob_.md_counters().resyncs_failed++;
                    resync_state_ = ResyncState::Normal;
                    return;
                }
                doc = std::move(err.value_unsafe());

                auto ret_code = doc.find_field("retCode");
                if (!ret_code.error() && ret_code.get_int64().value() != 0) {
                    spdlog::error("OB resync: API error retCode={}", ret_code.get_int64().value());
                    ob_.md_counters().resyncs_failed++;
                    resync_state_ = ResyncState::Normal;
                    return;
                }

                auto result = doc.find_field("result");
                if (result.error()) {
                    spdlog::error("OB resync: missing result field");
                    ob_.md_counters().resyncs_failed++;
                    resync_state_ = ResyncState::Normal;
                    return;
                }

                auto seq_field = result.find_field("seq");
                uint64_t seq = seq_field.error() ? 0 : seq_field.get_uint64().value();

                PriceLevel bids[MAX_OB_LEVELS];
                PriceLevel asks[MAX_OB_LEVELS];
                size_t bc = 0, ac = 0;

                auto b_arr = result.find_field("b");
                if (!b_arr.error()) {
                    for (auto level : b_arr.get_array()) {
                        if (bc >= MAX_OB_LEVELS) break;
                        auto arr = level.get_array();
                        auto it = arr.begin();
                        std::string_view ps = (*it).get_string().value();
                        ++it;
                        std::string_view qs = (*it).get_string().value();
                        bids[bc].price = fast_atof(ps.data(), ps.size());
                        bids[bc].qty = fast_atof(qs.data(), qs.size());
                        ++bc;
                    }
                }

                auto a_arr = result.find_field("a");
                if (!a_arr.error()) {
                    for (auto level : a_arr.get_array()) {
                        if (ac >= MAX_OB_LEVELS) break;
                        auto arr = level.get_array();
                        auto it = arr.begin();
                        std::string_view ps = (*it).get_string().value();
                        ++it;
                        std::string_view qs = (*it).get_string().value();
                        asks[ac].price = fast_atof(ps.data(), ps.size());
                        asks[ac].qty = fast_atof(qs.data(), qs.size());
                        ++ac;
                    }
                }

                ob_.apply_snapshot(bids, bc, asks, ac, seq);
                ob_.md_counters().resyncs_succeeded++;
                resync_state_ = ResyncState::Normal;
                spdlog::info("OB resync complete: {} bids, {} asks, seq={}", bc, ac, seq);
            });
    }

    net::io_context& ioc_;
    ssl::context& ssl_ctx_;
    const AppConfig& cfg_;
    OrderBook& ob_;
    TradeFlowEngine& tf_;
    Portfolio& portfolio_;
    SmartExecutionEngine& exec_;
    Metrics& metrics_;
    RestClient& rest_;

    std::shared_ptr<WsClient> public_ws_;
    std::shared_ptr<WsClient> private_ws_;

    // Stage 3: resync state machine
    ResyncState resync_state_ = ResyncState::Normal;

    // #9: simdjson parser for OB resync (reused to avoid reallocation)
    simdjson::ondemand::parser resync_parser_;

    // #2: Event-driven strategy tick callback
    OnBookUpdateCb on_book_update_;

    // E5: WS RTT measurement
    uint64_t last_ping_ns_ = 0;
    uint64_t ws_rtt_us_    = 0;

    // E3: Fetch instrument info from REST API
    void fetch_instrument_info() {
        rest_.get_instrument_info(cfg_.symbol, [this](bool ok, const std::string& body) {
            if (!ok) {
                spdlog::warn("[E3] Failed to fetch instrument info — using defaults");
                return;
            }
            auto padded = simdjson::padded_string(body);
            simdjson::ondemand::parser parser;
            auto doc_result = parser.iterate(padded);
            if (doc_result.error()) return;
            auto doc = std::move(doc_result.value_unsafe());
            auto result = doc.find_field("result");
            if (result.error()) return;
            auto list = result.find_field("list");
            if (list.error()) return;
            for (auto item : list.get_array()) {
                auto lot_filter = item.find_field("lotSizeFilter");
                if (!lot_filter.error()) {
                    auto qty_step = lot_filter.find_field("qtyStep");
                    if (!qty_step.error()) {
                        std::string_view s = qty_step.get_string().value();
                        double ls = fast_atof(s.data(), s.size());
                        if (ls > 0) {
                            // Note: cfg_ is const ref, so we log but cannot mutate
                            spdlog::info("[E3] Instrument {}: lotSize={}", cfg_.symbol, ls);
                        }
                    }
                }
                auto price_filter = item.find_field("priceFilter");
                if (!price_filter.error()) {
                    auto tick = price_filter.find_field("tickSize");
                    if (!tick.error()) {
                        std::string_view s = tick.get_string().value();
                        double ts = fast_atof(s.data(), s.size());
                        if (ts > 0) {
                            spdlog::info("[E3] Instrument {}: tickSize={}", cfg_.symbol, ts);
                        }
                    }
                }
                break; // only first item
            }
        });
    }
};

} // namespace bybit
