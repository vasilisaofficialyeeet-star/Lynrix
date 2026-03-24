#pragma once

#include "../config/types.h"
#include "../utils/ring_buffer.h"
#include "../utils/clock.h"

#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <cinttypes>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <filesystem>
#include <chrono>
#include <cstdio>

namespace bybit {

// ─── Research Record Types ──────────────────────────────────────────────────

struct ResearchRecord {
    enum class Type : uint8_t {
        Trade, Feature, Signal, Pnl,
        ObSnapshot, ModelPrediction, RegimeChange, OrderEvent
    };
    Type type = Type::Trade;
    uint64_t timestamp_ns = 0;
    char data[768] = {};
};

// ─── Research Recorder ──────────────────────────────────────────────────────
// Extended async data recorder for ML training pipeline.
// Records all market data, features, predictions, and execution events.

class ResearchRecorder {
public:
    static constexpr size_t BUFFER_SIZE = 32768;

    explicit ResearchRecorder(const std::string& log_dir, int flush_interval_ms = 500)
        : log_dir_(log_dir)
        , flush_interval_ms_(flush_interval_ms)
    {}

    ~ResearchRecorder() { stop(); }

    void start() {
        if (running_.load(std::memory_order_acquire)) return;

        std::filesystem::create_directories(log_dir_);

        std::string ts = fmt::format("{}", Clock::wall_ms());
        std::string session_dir = log_dir_ + "/session_" + ts;
        std::filesystem::create_directories(session_dir);

        // Open all log files
        trade_file_.open(session_dir + "/trades.csv");
        feature_file_.open(session_dir + "/features.csv");
        signal_file_.open(session_dir + "/signals.csv");
        pnl_file_.open(session_dir + "/pnl.csv");
        ob_file_.open(session_dir + "/orderbook.csv");
        prediction_file_.open(session_dir + "/predictions.csv");
        regime_file_.open(session_dir + "/regimes.csv");
        order_file_.open(session_dir + "/orders.csv");

        write_headers();

        running_.store(true, std::memory_order_release);
        writer_thread_ = std::thread([this]() { run(); });
        spdlog::info("ResearchRecorder started: {}", session_dir);
    }

    void stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        if (writer_thread_.joinable()) writer_thread_.join();
        flush();
        close_files();
        spdlog::info("ResearchRecorder stopped");
    }

    // ── Non-blocking enqueue methods ─────────────────────────────────────

    bool log_trade(uint64_t ts, double price, double qty, bool is_buy) noexcept {
        ResearchRecord rec;
        rec.type = ResearchRecord::Type::Trade;
        rec.timestamp_ns = ts;
        std::snprintf(rec.data, sizeof(rec.data),
                      "%" PRIu64 ",%.2f,%.6f,%s",
                      ts, price, qty, is_buy ? "buy" : "sell");
        return buffer_.push(rec);
    }

    bool log_features(const Features& f) noexcept {
        ResearchRecord rec;
        rec.type = ResearchRecord::Type::Feature;
        rec.timestamp_ns = f.timestamp_ns;
        std::snprintf(rec.data, sizeof(rec.data),
            "%" PRIu64
            ",%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"
            ",%.6f,%.6f,%.6f,%.6f,%.6f"
            ",%.6f,%.6f,%.6f,%.6f,%.8f"
            ",%.6f,%.6f,%.6f,%.6f"
            ",%.8f,%.8f,%.8f,%.8f",
            f.timestamp_ns,
            f.imbalance_1, f.imbalance_5, f.imbalance_20,
            f.ob_slope, f.depth_concentration, f.cancel_spike, f.liquidity_wall,
            f.aggression_ratio, f.avg_trade_size, f.trade_velocity,
            f.trade_acceleration, f.volume_accel,
            f.microprice, f.spread_bps, f.spread_change_rate,
            f.mid_momentum, f.volatility,
            f.microprice_dev, f.short_term_pressure,
            f.bid_depth_total, f.ask_depth_total,
            f.d_imbalance_dt, f.d2_imbalance_dt2,
            f.d_volatility_dt, f.d_momentum_dt);
        return buffer_.push(rec);
    }

    bool log_signal(const Signal& s) noexcept {
        ResearchRecord rec;
        rec.type = ResearchRecord::Type::Signal;
        rec.timestamp_ns = s.timestamp_ns;
        std::snprintf(rec.data, sizeof(rec.data),
                      "%" PRIu64 ",%s,%.2f,%.6f,%.4f,%d,%.4f,%.4f,%.4f",
                      s.timestamp_ns,
                      s.side == Side::Buy ? "buy" : "sell",
                      s.price.raw(), s.qty.raw(), s.confidence,
                      static_cast<int>(s.regime),
                      s.fill_prob, s.expected_pnl.raw(), s.adaptive_threshold);
        return buffer_.push(rec);
    }

    bool log_pnl(uint64_t ts, double realized, double unrealized,
                 double pos_size, double entry, double mark_price) noexcept {
        ResearchRecord rec;
        rec.type = ResearchRecord::Type::Pnl;
        rec.timestamp_ns = ts;
        std::snprintf(rec.data, sizeof(rec.data),
                      "%" PRIu64 ",%.4f,%.4f,%.6f,%.2f,%.2f",
                      ts, realized, unrealized, pos_size, entry, mark_price);
        return buffer_.push(rec);
    }

    bool log_ob_snapshot(uint64_t ts, double best_bid, double best_ask,
                         double bid_depth, double ask_depth,
                         double microprice, double spread) noexcept {
        ResearchRecord rec;
        rec.type = ResearchRecord::Type::ObSnapshot;
        rec.timestamp_ns = ts;
        std::snprintf(rec.data, sizeof(rec.data),
                      "%" PRIu64 ",%.2f,%.2f,%.4f,%.4f,%.6f,%.4f",
                      ts, best_bid, best_ask, bid_depth, ask_depth,
                      microprice, spread);
        return buffer_.push(rec);
    }

    bool log_prediction(uint64_t ts,
                        double prob_up_100, double prob_down_100,
                        double prob_up_500, double prob_down_500,
                        double prob_up_1s, double prob_down_1s,
                        double prob_up_3s, double prob_down_3s,
                        double confidence, uint64_t inference_ns) noexcept {
        ResearchRecord rec;
        rec.type = ResearchRecord::Type::ModelPrediction;
        rec.timestamp_ns = ts;
        std::snprintf(rec.data, sizeof(rec.data),
            "%" PRIu64
            ",%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f"
            ",%.4f,%" PRIu64,
            ts,
            prob_up_100, prob_down_100,
            prob_up_500, prob_down_500,
            prob_up_1s, prob_down_1s,
            prob_up_3s, prob_down_3s,
            confidence, inference_ns);
        return buffer_.push(rec);
    }

    bool log_regime_change(uint64_t ts, MarketRegime prev, MarketRegime curr,
                           double confidence, double volatility) noexcept {
        ResearchRecord rec;
        rec.type = ResearchRecord::Type::RegimeChange;
        rec.timestamp_ns = ts;
        std::snprintf(rec.data, sizeof(rec.data),
                      "%" PRIu64 ",%d,%d,%.4f,%.8f",
                      ts, static_cast<int>(prev), static_cast<int>(curr),
                      confidence, volatility);
        return buffer_.push(rec);
    }

    bool log_order_event(uint64_t ts, const char* order_id, const char* event,
                         const char* side, double price, double qty,
                         double filled_qty) noexcept {
        ResearchRecord rec;
        rec.type = ResearchRecord::Type::OrderEvent;
        rec.timestamp_ns = ts;
        std::snprintf(rec.data, sizeof(rec.data),
                      "%" PRIu64 ",%s,%s,%s,%.2f,%.6f,%.6f",
                      ts, order_id, event, side, price, qty, filled_qty);
        return buffer_.push(rec);
    }

private:
    void write_headers() {
        if (trade_file_.is_open())
            trade_file_ << "timestamp_ns,price,qty,side\n";

        if (feature_file_.is_open())
            feature_file_ << "timestamp_ns"
                ",imb1,imb5,imb20,ob_slope,depth_conc,cancel_spike,liq_wall"
                ",aggr_ratio,avg_trade_sz,trade_vel,trade_accel,vol_accel"
                ",microprice,spread_bps,spread_chg,mid_mom,volatility"
                ",mp_dev,st_pressure,bid_depth,ask_depth"
                ",d_imb_dt,d2_imb_dt2,d_vol_dt,d_mom_dt\n";

        if (signal_file_.is_open())
            signal_file_ << "timestamp_ns,side,price,qty,confidence,"
                            "regime,fill_prob,expected_pnl,threshold\n";

        if (pnl_file_.is_open())
            pnl_file_ << "timestamp_ns,realized_pnl,unrealized_pnl,"
                         "position_size,entry_price,mark_price\n";

        if (ob_file_.is_open())
            ob_file_ << "timestamp_ns,best_bid,best_ask,bid_depth,"
                        "ask_depth,microprice,spread\n";

        if (prediction_file_.is_open())
            prediction_file_ << "timestamp_ns,"
                "prob_up_100ms,prob_down_100ms,"
                "prob_up_500ms,prob_down_500ms,"
                "prob_up_1s,prob_down_1s,"
                "prob_up_3s,prob_down_3s,"
                "confidence,inference_ns\n";

        if (regime_file_.is_open())
            regime_file_ << "timestamp_ns,prev_regime,curr_regime,"
                            "confidence,volatility\n";

        if (order_file_.is_open())
            order_file_ << "timestamp_ns,order_id,event,side,"
                           "price,qty,filled_qty\n";
    }

    void run() {
        while (running_.load(std::memory_order_acquire)) {
            flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(flush_interval_ms_));
        }
    }

    void flush() {
        ResearchRecord rec;
        int count = 0;
        while (buffer_.pop(rec)) {
            switch (rec.type) {
                case ResearchRecord::Type::Trade:
                    if (trade_file_.is_open()) trade_file_ << rec.data << '\n';
                    break;
                case ResearchRecord::Type::Feature:
                    if (feature_file_.is_open()) feature_file_ << rec.data << '\n';
                    break;
                case ResearchRecord::Type::Signal:
                    if (signal_file_.is_open()) signal_file_ << rec.data << '\n';
                    break;
                case ResearchRecord::Type::Pnl:
                    if (pnl_file_.is_open()) pnl_file_ << rec.data << '\n';
                    break;
                case ResearchRecord::Type::ObSnapshot:
                    if (ob_file_.is_open()) ob_file_ << rec.data << '\n';
                    break;
                case ResearchRecord::Type::ModelPrediction:
                    if (prediction_file_.is_open()) prediction_file_ << rec.data << '\n';
                    break;
                case ResearchRecord::Type::RegimeChange:
                    if (regime_file_.is_open()) regime_file_ << rec.data << '\n';
                    break;
                case ResearchRecord::Type::OrderEvent:
                    if (order_file_.is_open()) order_file_ << rec.data << '\n';
                    break;
            }
            ++count;
        }

        if (count > 0) {
            trade_file_.flush();
            feature_file_.flush();
            signal_file_.flush();
            pnl_file_.flush();
            ob_file_.flush();
            prediction_file_.flush();
            regime_file_.flush();
            order_file_.flush();
        }
    }

    void close_files() {
        trade_file_.close();
        feature_file_.close();
        signal_file_.close();
        pnl_file_.close();
        ob_file_.close();
        prediction_file_.close();
        regime_file_.close();
        order_file_.close();
    }

    std::string log_dir_;
    int flush_interval_ms_;
    std::atomic<bool> running_{false};
    std::thread writer_thread_;

    RingBuffer<ResearchRecord, BUFFER_SIZE> buffer_;

    std::ofstream trade_file_;
    std::ofstream feature_file_;
    std::ofstream signal_file_;
    std::ofstream pnl_file_;
    std::ofstream ob_file_;
    std::ofstream prediction_file_;
    std::ofstream regime_file_;
    std::ofstream order_file_;
};

} // namespace bybit
