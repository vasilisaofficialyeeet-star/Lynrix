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

namespace bybit {

// Async batch writer running on dedicated thread.
// Uses lock-free ring buffer to collect records from hot path.
// Flushes in batches to disk.

struct LogRecord {
    enum class Type : uint8_t { Trade, Feature, Signal, Pnl };
    Type type = Type::Trade;
    uint64_t timestamp_ns = 0;
    char data[512] = {};
};

class AsyncWriter {
public:
    static constexpr size_t BUFFER_SIZE = 16384; // must be power of 2

    explicit AsyncWriter(const std::string& log_dir, int flush_interval_ms = 1000)
        : log_dir_(log_dir)
        , flush_interval_ms_(flush_interval_ms)
    {}

    ~AsyncWriter() {
        stop();
    }

    void start() {
        if (running_.load(std::memory_order_acquire)) return;

        // Create log directory
        std::filesystem::create_directories(log_dir_);

        // Open log files
        std::string ts = fmt::format("{}", Clock::wall_ms());
        trade_file_.open(log_dir_ + "/trades_" + ts + ".csv");
        feature_file_.open(log_dir_ + "/features_" + ts + ".csv");
        signal_file_.open(log_dir_ + "/signals_" + ts + ".csv");
        pnl_file_.open(log_dir_ + "/pnl_" + ts + ".csv");

        // Write headers
        if (trade_file_.is_open()) {
            trade_file_ << "timestamp_ns,price,qty,side\n";
        }
        if (feature_file_.is_open()) {
            feature_file_ << "timestamp_ns,imb5,imb20,liq_slope,cancel_spike,spread_chg,"
                          << "aggr_ratio,vol_accel,trade_vel,mp_dev,ewma_vol,st_pressure\n";
        }
        if (signal_file_.is_open()) {
            signal_file_ << "timestamp_ns,side,price,qty,confidence\n";
        }
        if (pnl_file_.is_open()) {
            pnl_file_ << "timestamp_ns,realized_pnl,unrealized_pnl,position_size,entry_price\n";
        }

        running_.store(true, std::memory_order_release);
        writer_thread_ = std::thread([this]() { run(); });
        spdlog::info("AsyncWriter started, log_dir={}", log_dir_);
    }

    void stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        if (writer_thread_.joinable()) writer_thread_.join();

        // Final flush
        flush();

        trade_file_.close();
        feature_file_.close();
        signal_file_.close();
        pnl_file_.close();
        spdlog::info("AsyncWriter stopped");
    }

    // Non-blocking enqueue from hot path
    bool log_trade(uint64_t ts, double price, double qty, bool is_buy) noexcept {
        LogRecord rec;
        rec.type = LogRecord::Type::Trade;
        rec.timestamp_ns = ts;
        std::snprintf(rec.data, sizeof(rec.data), "%" PRIu64 ",%.1f,%.4f,%s",
                      ts, price, qty, is_buy ? "buy" : "sell");
        return buffer_.push(rec);
    }

    bool log_features(const Features& f) noexcept {
        LogRecord rec;
        rec.type = LogRecord::Type::Feature;
        rec.timestamp_ns = f.timestamp_ns;
        std::snprintf(rec.data, sizeof(rec.data),
                      "%" PRIu64 ",%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f",
                      f.timestamp_ns, f.imbalance_5, f.imbalance_20,
                      f.ob_slope, f.cancel_spike, f.spread_change_rate,
                      f.aggression_ratio, f.volume_accel, f.trade_velocity,
                      f.microprice_dev, f.volatility, f.short_term_pressure);
        return buffer_.push(rec);
    }

    bool log_signal(const Signal& s) noexcept {
        LogRecord rec;
        rec.type = LogRecord::Type::Signal;
        rec.timestamp_ns = s.timestamp_ns;
        std::snprintf(rec.data, sizeof(rec.data), "%" PRIu64 ",%s,%.1f,%.4f,%.4f",
                      s.timestamp_ns, s.side == Side::Buy ? "buy" : "sell",
                      s.price, s.qty, s.confidence);
        return buffer_.push(rec);
    }

    bool log_pnl(uint64_t ts, double realized, double unrealized, double pos_size, double entry) noexcept {
        LogRecord rec;
        rec.type = LogRecord::Type::Pnl;
        rec.timestamp_ns = ts;
        std::snprintf(rec.data, sizeof(rec.data), "%" PRIu64 ",%.4f,%.4f,%.4f,%.1f",
                      ts, realized, unrealized, pos_size, entry);
        return buffer_.push(rec);
    }

private:
    void run() {
        while (running_.load(std::memory_order_acquire)) {
            flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(flush_interval_ms_));
        }
    }

    void flush() {
        LogRecord rec;
        int count = 0;
        while (buffer_.pop(rec)) {
            switch (rec.type) {
                case LogRecord::Type::Trade:
                    if (trade_file_.is_open()) trade_file_ << rec.data << '\n';
                    break;
                case LogRecord::Type::Feature:
                    if (feature_file_.is_open()) feature_file_ << rec.data << '\n';
                    break;
                case LogRecord::Type::Signal:
                    if (signal_file_.is_open()) signal_file_ << rec.data << '\n';
                    break;
                case LogRecord::Type::Pnl:
                    if (pnl_file_.is_open()) pnl_file_ << rec.data << '\n';
                    break;
            }
            ++count;
        }

        if (count > 0) {
            trade_file_.flush();
            feature_file_.flush();
            signal_file_.flush();
            pnl_file_.flush();
        }
    }

    std::string log_dir_;
    int flush_interval_ms_;
    std::atomic<bool> running_{false};
    std::thread writer_thread_;

    RingBuffer<LogRecord, BUFFER_SIZE> buffer_;

    std::ofstream trade_file_;
    std::ofstream feature_file_;
    std::ofstream signal_file_;
    std::ofstream pnl_file_;
};

} // namespace bybit
