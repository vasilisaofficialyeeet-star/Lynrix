#include <gtest/gtest.h>

#include "config/types.h"
#include "config/config_loader.h"
#include "persistence/async_writer.h"
#include "utils/clock.h"

#include <fstream>
#include <filesystem>
#include <string>
#include <thread>
#include <chrono>

using namespace bybit;

// ═══════════════════════════════════════════════════════════════════════════
// ConfigLoader Tests
// ═══════════════════════════════════════════════════════════════════════════

class ConfigLoaderTest : public ::testing::Test {
protected:
    std::string tmp_dir_;
    std::string config_path_;

    void SetUp() override {
        tmp_dir_ = std::filesystem::temp_directory_path().string() + "/bybit_test_" +
                   std::to_string(Clock::now_ns());
        std::filesystem::create_directories(tmp_dir_);
        config_path_ = tmp_dir_ + "/test_config.json";
    }

    void TearDown() override {
        std::filesystem::remove_all(tmp_dir_);
    }

    void write_config(const std::string& json) {
        std::ofstream f(config_path_);
        f << json;
        f.close();
    }
};

TEST_F(ConfigLoaderTest, LoadMinimalConfig) {
    write_config(R"({"symbol": "ETHUSDT"})");
    auto cfg = ConfigLoader::load(config_path_);
    EXPECT_EQ(cfg.symbol, "ETHUSDT");
}

TEST_F(ConfigLoaderTest, LoadFullConfig) {
    write_config(R"({
        "symbol": "BTCUSDT",
        "paper_trading": true,
        "ws": {
            "public_url": "wss://test.bybit.com/v5/public/linear",
            "private_url": "wss://test.bybit.com/v5/private",
            "ping_interval_sec": 15,
            "stale_timeout_sec": 25,
            "reconnect_base_ms": 500,
            "reconnect_max_ms": 20000
        },
        "rest": {
            "base_url": "https://test-api.bybit.com",
            "timeout_ms": 3000,
            "max_retries": 2
        },
        "trading": {
            "order_qty": 0.005,
            "signal_threshold": 0.7,
            "signal_ttl_ms": 400,
            "entry_offset_bps": 0.8
        },
        "risk": {
            "max_position_size": 0.5,
            "max_leverage": 5.0,
            "max_daily_loss": 200.0,
            "max_drawdown": 0.08,
            "max_orders_per_sec": 3
        },
        "model": {
            "bias": -0.1,
            "weights": [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1]
        },
        "persistence": {
            "log_dir": "./test_logs",
            "batch_flush_ms": 500
        },
        "performance": {
            "ob_levels": 100,
            "io_threads": 4
        }
    })");

    auto cfg = ConfigLoader::load(config_path_);
    EXPECT_EQ(cfg.symbol, "BTCUSDT");
    EXPECT_TRUE(cfg.paper_trading);
    EXPECT_EQ(cfg.ws_public_url, "wss://test.bybit.com/v5/public/linear");
    EXPECT_EQ(cfg.ws_private_url, "wss://test.bybit.com/v5/private");
    EXPECT_EQ(cfg.ws_ping_interval_sec, 15);
    EXPECT_EQ(cfg.ws_stale_timeout_sec, 25);
    EXPECT_EQ(cfg.ws_reconnect_base_ms, 500);
    EXPECT_EQ(cfg.ws_reconnect_max_ms, 20000);
    EXPECT_EQ(cfg.rest_base_url, "https://test-api.bybit.com");
    EXPECT_EQ(cfg.rest_timeout_ms, 3000);
    EXPECT_EQ(cfg.rest_max_retries, 2);
    EXPECT_DOUBLE_EQ(cfg.order_qty, 0.005);
    EXPECT_DOUBLE_EQ(cfg.signal_threshold, 0.7);
    EXPECT_EQ(cfg.signal_ttl_ms, 400);
    EXPECT_DOUBLE_EQ(cfg.entry_offset_bps, 0.8);
    EXPECT_DOUBLE_EQ(cfg.risk.max_position_size.raw(), 0.5);
    EXPECT_DOUBLE_EQ(cfg.risk.max_leverage, 5.0);
    EXPECT_DOUBLE_EQ(cfg.risk.max_daily_loss.raw(), 200.0);
    EXPECT_DOUBLE_EQ(cfg.risk.max_drawdown, 0.08);
    EXPECT_EQ(cfg.risk.max_orders_per_sec, 3);
    EXPECT_DOUBLE_EQ(cfg.model_bias, -0.1);
    EXPECT_DOUBLE_EQ(cfg.model_weights[0], 0.1);
    EXPECT_DOUBLE_EQ(cfg.model_weights[10], 1.1);
    EXPECT_EQ(cfg.log_dir, "./test_logs");
    EXPECT_EQ(cfg.batch_flush_ms, 500);
    EXPECT_EQ(cfg.ob_levels, 100);
    EXPECT_EQ(cfg.io_threads, 4);
}

TEST_F(ConfigLoaderTest, MissingFileThrows) {
    EXPECT_THROW(ConfigLoader::load("/nonexistent/path.json"), std::runtime_error);
}

TEST_F(ConfigLoaderTest, InvalidJsonThrows) {
    write_config("not valid json {{{");
    EXPECT_THROW(ConfigLoader::load(config_path_), nlohmann::json::exception);
}

TEST_F(ConfigLoaderTest, EmptyJsonUsesDefaults) {
    write_config("{}");
    auto cfg = ConfigLoader::load(config_path_);
    // Should use defaults from AppConfig
    EXPECT_EQ(cfg.symbol, "BTCUSDT");
    EXPECT_TRUE(cfg.paper_trading);
}

TEST_F(ConfigLoaderTest, PartialConfig) {
    write_config(R"({"symbol": "SOLUSDT", "trading": {"order_qty": 10.0}})");
    auto cfg = ConfigLoader::load(config_path_);
    EXPECT_EQ(cfg.symbol, "SOLUSDT");
    EXPECT_DOUBLE_EQ(cfg.order_qty, 10.0);
    // Other fields should be defaults
    EXPECT_TRUE(cfg.paper_trading);
}

// ═══════════════════════════════════════════════════════════════════════════
// AsyncWriter Tests
// ═══════════════════════════════════════════════════════════════════════════

class AsyncWriterTest : public ::testing::Test {
protected:
    std::string tmp_dir_;

    void SetUp() override {
        tmp_dir_ = std::filesystem::temp_directory_path().string() + "/bybit_writer_test_" +
                   std::to_string(Clock::now_ns());
    }

    void TearDown() override {
        std::filesystem::remove_all(tmp_dir_);
    }

    // Find first file in tmp_dir_ whose name starts with prefix
    std::string find_file(const std::string& prefix) {
        for (auto& entry : std::filesystem::directory_iterator(tmp_dir_)) {
            auto fname = entry.path().filename().string();
            if (fname.starts_with(prefix) && fname.ends_with(".csv")) {
                return entry.path().string();
            }
        }
        return {};
    }

    std::string read_file_content(const std::string& path) {
        std::ifstream f(path);
        return std::string(std::istreambuf_iterator<char>(f),
                           std::istreambuf_iterator<char>());
    }
};

TEST_F(AsyncWriterTest, CreatesLogDirectory) {
    AsyncWriter writer(tmp_dir_, 50);
    writer.start();
    EXPECT_TRUE(std::filesystem::exists(tmp_dir_));
    writer.stop();
}

TEST_F(AsyncWriterTest, CreatesLogFiles) {
    AsyncWriter writer(tmp_dir_, 50);
    writer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    writer.stop();

    EXPECT_FALSE(find_file("trades_").empty());
    EXPECT_FALSE(find_file("features_").empty());
    EXPECT_FALSE(find_file("signals_").empty());
    EXPECT_FALSE(find_file("pnl_").empty());
}

TEST_F(AsyncWriterTest, LogTrade) {
    AsyncWriter writer(tmp_dir_, 50);
    writer.start();

    uint64_t ts = 1700000000000000000ULL;
    EXPECT_TRUE(writer.log_trade(ts, 50000.5, 0.1, true));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    writer.stop();

    auto path = find_file("trades_");
    ASSERT_FALSE(path.empty());
    auto content = read_file_content(path);
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("50000.5"), std::string::npos);
    EXPECT_NE(content.find("buy"), std::string::npos);
}

TEST_F(AsyncWriterTest, LogFeatures) {
    AsyncWriter writer(tmp_dir_, 50);
    writer.start();

    Features f{};
    f.timestamp_ns = Clock::now_ns();
    f.imbalance_5 = 0.123456;
    f.aggression_ratio = 0.654321;
    EXPECT_TRUE(writer.log_features(f));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    writer.stop();

    auto path = find_file("features_");
    ASSERT_FALSE(path.empty());
    auto content = read_file_content(path);
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("0.123456"), std::string::npos);
}

TEST_F(AsyncWriterTest, LogSignal) {
    AsyncWriter writer(tmp_dir_, 50);
    writer.start();

    Signal s;
    s.timestamp_ns = Clock::now_ns();
    s.side = Side::Sell;
    s.price = Price(49500.0);
    s.qty = Qty(0.05);
    s.confidence = 0.85;
    EXPECT_TRUE(writer.log_signal(s));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    writer.stop();

    auto path = find_file("signals_");
    ASSERT_FALSE(path.empty());
    auto content = read_file_content(path);
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("sell"), std::string::npos);
    EXPECT_NE(content.find("49500"), std::string::npos);
}

TEST_F(AsyncWriterTest, LogPnl) {
    AsyncWriter writer(tmp_dir_, 50);
    writer.start();

    uint64_t ts = Clock::now_ns();
    EXPECT_TRUE(writer.log_pnl(ts, 150.0, -20.0, 0.5, 50100.0));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    writer.stop();

    auto path = find_file("pnl_");
    ASSERT_FALSE(path.empty());
    auto content = read_file_content(path);
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("150.0000"), std::string::npos);
}

TEST_F(AsyncWriterTest, BatchFlush) {
    AsyncWriter writer(tmp_dir_, 50);
    writer.start();

    // Write many records
    for (int i = 0; i < 100; ++i) {
        writer.log_trade(Clock::now_ns(), 50000.0 + i, 0.01 * i, i % 2 == 0);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    writer.stop();

    auto path = find_file("trades_");
    ASSERT_FALSE(path.empty());
    auto content = read_file_content(path);
    // Count lines (should have header + ~100 data lines)
    int lines = 0;
    for (char c : content) {
        if (c == '\n') ++lines;
    }
    EXPECT_GE(lines, 100);
}

TEST_F(AsyncWriterTest, StartStopMultipleTimes) {
    AsyncWriter writer(tmp_dir_, 50);
    writer.start();
    writer.log_trade(Clock::now_ns(), 50000.0, 1.0, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    writer.stop();

    // Restart
    writer.start();
    writer.log_trade(Clock::now_ns(), 51000.0, 2.0, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    writer.stop();

    // Should not crash
    SUCCEED();
}

TEST_F(AsyncWriterTest, NonBlockingEnqueue) {
    AsyncWriter writer(tmp_dir_, 50);
    writer.start();

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        writer.log_trade(Clock::now_ns(), 50000.0, 0.01, true);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double avg_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 10000.0;

    // Enqueue must be fast (< 1µs per call)
    EXPECT_LT(avg_ns, 1000.0);
    std::cout << "AsyncWriter enqueue avg: " << avg_ns << " ns" << std::endl;

    writer.stop();
}
