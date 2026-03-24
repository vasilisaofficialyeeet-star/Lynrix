#pragma once

#include "../config/types.h"
#include "../config/config_loader.h"
#include "../orderbook/orderbook.h"
#include "../trade_flow/trade_flow_engine.h"
#include "../feature_engine/advanced_feature_engine.h"
#include "../model_engine/gru_model.h"
#include "../model_engine/onnx_inference.h"
#include "../model_engine/accuracy_tracker.h"
#include "../execution_engine/smart_execution.h"
#include "../risk_engine/enhanced_risk_engine.h"
#include "../regime/regime_detector.h"
#include "../strategy/adaptive_threshold.h"
#include "../strategy/fill_probability.h"
#include "../strategy/adaptive_position_sizer.h"
#include "../portfolio/portfolio.h"
#include "../persistence/research_recorder.h"
#include "../metrics/latency_histogram.h"
#include "../networking/ws_manager.h"
#include "../networking/ws_trade_client.h"
#include "../rest_client/rest_client.h"
#include "../analytics/strategy_metrics.h"
#include "../analytics/strategy_health.h"
#include "../analytics/feature_importance.h"
#include "../monitoring/system_monitor.h"
#include "../rl/rl_optimizer.h"
#include "../bridge/ui_snapshot.h"
#include "../utils/clock.h"
#include "../utils/thread_affinity.h"
#include "../core/hot_path.h"
#include "../core/system_control.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>

#include <simdjson.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <csignal>
#include <chrono>
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>

namespace bybit {

namespace net = boost::asio;
namespace ssl = net::ssl;

// Self-pipe for async-signal-safe signal notification
namespace detail {
    inline int g_signal_pipe[2] = {-1, -1};
    inline void signal_write(int sig) {
        const char msg[] = "SIG\n";
        (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        (void)::write(g_signal_pipe[1], &sig, sizeof(sig));
    }
}

class Application {
public:
    explicit Application(const AppConfig& cfg)
        : cfg_(cfg)
        , ssl_ctx_(ssl::context::tlsv12_client)
        , ioc_(cfg.io_threads)
        , cold_ioc_(1)    // #3: Isolated io_context for cold work
        , strategy_strand_(net::make_strand(ioc_))  // C4: Serialize strategy_tick
        , adaptive_threshold_(cfg.signal_threshold, cfg.adaptive_threshold_min, cfg.adaptive_threshold_max)
        , position_sizer_(cfg)
        , risk_(cfg.risk, cfg.circuit_breaker)
        , rest_(ioc_, ssl_ctx_, cfg)
        , exec_(rest_, risk_, portfolio_, cfg, metrics_)
        , ws_mgr_(ioc_, ssl_ctx_, cfg, ob_, tf_, portfolio_, exec_, metrics_, rest_)
        , recorder_(cfg.log_dir, cfg.batch_flush_ms)
    {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(ssl::verify_peer);

        // C3: Wire execution engine to OB for paper fill simulation
        exec_.set_orderbook(&ob_);

        // E2: ONNX Runtime is the PRIMARY production inference path.
        // Native GRU is a FALLBACK/DEMO only — it ships with random Xavier init
        // and produces meaningless predictions unless trained offline.
        if (cfg.onnx_enabled && !cfg.onnx_model_path.empty()) {
            if (onnx_engine_.load(cfg.onnx_model_path, cfg.onnx_intra_threads)) {
                spdlog::info("[ML] ONNX model loaded: {} (backend: {})",
                             cfg.onnx_model_path,
                             OnnxInferenceEngine::backend_name(onnx_engine_.backend()));
                use_onnx_ = true;
            } else {
                spdlog::error("[ML] Failed to load ONNX model: {} — {}",
                              cfg.onnx_model_path, onnx_engine_.last_error());
            }
        }
        if (!use_onnx_ && !cfg.ml_model_path.empty()) {
            if (gru_model_.load_weights(cfg.ml_model_path)) {
                spdlog::warn("[ML] Using native GRU fallback from {} "
                             "(not production-grade — train via ONNX)", cfg.ml_model_path);
            } else {
                spdlog::warn("[ML] GRU weights not found at {}, using random init "
                             "(DEMO ONLY — predictions are meaningless)", cfg.ml_model_path);
            }
        }
        if (!use_onnx_ && cfg.ml_model_enabled) {
            spdlog::warn("[ML] No ONNX model loaded. Running with native GRU fallback. "
                         "Set onnx_enabled=true and provide onnx_model_path for production.");
        }
    }

    void run() {
        setup_logging();
        spdlog::info("═══════════════════════════════════════════════════");
#ifndef LYNRIX_VERSION
#define LYNRIX_VERSION "2.5.0"
#endif
        spdlog::info("  Lynrix High Precision AI Edition v{}", LYNRIX_VERSION);
        spdlog::info("  Symbol: {}  Paper: {}", cfg_.symbol, cfg_.paper_trading);
        spdlog::info("  OB Levels: {}  IO Threads: {}", cfg_.ob_levels, cfg_.io_threads);
        spdlog::info("  Features: {}  GRU Hidden: {}  Seq Len: {}",
                     FEATURE_COUNT, GRU_HIDDEN_SIZE, FEATURE_SEQ_LEN);
        spdlog::info("  Regime Detection: {}  Adaptive Threshold: {}",
                     cfg_.regime_detection_enabled ? "ON" : "OFF",
                     cfg_.adaptive_threshold_enabled ? "ON" : "OFF");
        spdlog::info("  Fill Prob Model: {}  Re-quote: {}  Adaptive Sizing: {}",
                     cfg_.fill_prob_enabled ? "ON" : "OFF",
                     cfg_.requote_enabled ? "ON" : "OFF",
                     cfg_.adaptive_sizing_enabled ? "ON" : "OFF");
        spdlog::info("  ML Backend: {} ({})  ONNX: {}",
                     use_onnx_ ? "ONNX Runtime" : "Native GRU",
                     OnnxInferenceEngine::backend_name(onnx_engine_.backend()),
                     onnx_engine_.loaded() ? cfg_.onnx_model_path : "N/A");
        spdlog::info("═══════════════════════════════════════════════════");

        running_.store(true, std::memory_order_release);

        // Self-pipe for signal delivery
        if (::pipe(detail::g_signal_pipe) != 0) {
            throw std::runtime_error("Failed to create signal pipe");
        }
        ::fcntl(detail::g_signal_pipe[0], F_SETFL, O_NONBLOCK);
        ::fcntl(detail::g_signal_pipe[1], F_SETFL, O_NONBLOCK);

        struct sigaction sa{};
        sa.sa_handler = detail::signal_write;
        sa.sa_flags = SA_RESETHAND;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);

        // Start persistence
        recorder_.start();

        // #2: Register event-driven strategy tick callback
        // C4: Dispatch through strand to prevent data races between
        // WS callback and fallback timer across IO threads
        ws_mgr_.set_on_book_update([this]() {
            if (!running_.load(std::memory_order_acquire)) return;
            net::post(strategy_strand_, [this]() {
                if (!running_.load(std::memory_order_acquire)) return;
                strategy_tick();
            });
        });

        // C3: Start WS Trade client for low-latency order submission
        if (!cfg_.paper_trading && !cfg_.api_key.empty()) {
            ws_trade_ = std::make_unique<WsTradeClient>(ioc_, ssl_ctx_, cfg_);
            ws_trade_->start();
            exec_.set_ws_trade_client(ws_trade_.get());
        }

        // Start WebSocket connections
        ws_mgr_.start();

        // #19: Start fallback strategy timer (fires only if no OB updates arrive)
        start_strategy_timer();

        // Start metrics reporter (cold work)
        start_metrics_timer();

        // Start watchdog
        start_watchdog();

        // Start PnL logger
        start_pnl_logger();

        // #8: Start order reconciliation timer
        start_reconciliation_timer();

        // #3: Run IO threads with thread affinity
        std::vector<std::thread> io_threads;
        for (int i = 0; i < cfg_.io_threads; ++i) {
            io_threads.emplace_back([this, i]() {
                // #10: Pin IO/strategy threads to P-cores
                if (i == 0) {
                    configure_hot_thread(AffinityGroup::OrderBook, "io-strategy");
                } else {
                    configure_hot_thread(AffinityGroup::WebSocket, "io-network");
                }
                ioc_.run();
            });
        }

        // #3: Start cold work thread on E-core
        std::thread cold_thread([this]() {
            configure_background_thread(AffinityGroup::Monitoring, "cold-work");
            auto guard = net::make_work_guard(cold_ioc_);
            cold_ioc_.run();
        });

        spdlog::info("System started, {} IO threads + 1 cold thread", cfg_.io_threads);

        // Main thread: block on signal pipe via select()
        {
            fd_set rfds;
            int pipe_rd = detail::g_signal_pipe[0];
            while (running_.load(std::memory_order_acquire)) {
                FD_ZERO(&rfds);
                FD_SET(pipe_rd, &rfds);
                struct timeval tv { .tv_sec = 0, .tv_usec = 200000 };
                int ret = ::select(pipe_rd + 1, &rfds, nullptr, nullptr, &tv);
                if (ret > 0 && FD_ISSET(pipe_rd, &rfds)) {
                    int sig = 0;
                    (void)::read(pipe_rd, &sig, sizeof(sig));
                    spdlog::info("Signal {} received, stopping...", sig);
                    request_stop();
                    break;
                }
            }
            ::close(detail::g_signal_pipe[0]);
            ::close(detail::g_signal_pipe[1]);
        }

        for (auto& t : io_threads) {
            if (t.joinable()) t.join();
        }
        cold_ioc_.stop();
        if (cold_thread.joinable()) cold_thread.join();

        shutdown();
    }

    void request_stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        ioc_.stop();
        cold_ioc_.stop();
    }

    bool is_running() const { return running_.load(std::memory_order_acquire); }

    // ─── Thread-safe snapshot for GUI (SeqLock, zero mutex) ────────────
    UISnapshot ui_snapshot() const { return ui_seqlock_.read(); }
    uint64_t snapshot_version() const { return ui_seqlock_.version(); }

    // ─── Legacy accessors (kept for compatibility, prefer ui_snapshot) ──
    const OrderBook& orderbook() const { return ob_; }
    const Portfolio& portfolio() const { return portfolio_; }
    const Metrics& metrics() const { return metrics_; }
    const RegimeState& regime_state() const { return regime_detector_.state(); }
    const ModelOutput& last_prediction() const { return last_prediction_; }
    const AdaptiveThresholdState& threshold_state() const { return adaptive_threshold_.state(); }
    const Features& last_features() const { return feature_engine_.last(); }
    double current_drawdown() const { return risk_.drawdown(); }
    bool circuit_breaker_tripped() const { return risk_.circuit_breaker().is_tripped(); }
    const AccuracyMetrics& accuracy_metrics() const { return accuracy_tracker_.metrics(); }
    bool using_onnx() const { return use_onnx_; }
    const StrategyMetricsSnapshot& strategy_metrics() const { return strategy_metrics_.snapshot(); }
    const StrategyHealthSnapshot& strategy_health() const { return strategy_health_.snapshot(); }
    const FeatureImportanceSnapshot& feature_importance() const { return feature_importance_.snapshot(); }
    const SystemMonitorSnapshot& system_monitor() const { return system_monitor_.snapshot(); }
    const RLOptimizerSnapshot& rl_state() const { return rl_optimizer_.snapshot(); }
    const char* inference_backend_name() const {
        return OnnxInferenceEngine::backend_name(onnx_engine_.backend());
    }

    // ─── Engine control ─────────────────────────────────────────────────
    void emergency_stop() {
        spdlog::warn("[EMERGENCY] Emergency stop triggered!");
        // Stage 6: Notify control plane
        control_plane_.emergency_stop(tick_counter_);
        // Cancel all orders via controlled path
        exec_.emergency_cancel_all_controlled(control_plane_, tick_counter_);
        request_stop();
        // Fallback: also fire REST cancel-all for safety
        if (!cfg_.paper_trading && exec_.has_active_orders()) {
            rest_.cancel_all_orders(cfg_.symbol, [](bool ok, const std::string& body) {
                if (!ok) spdlog::error("Emergency cancel failed: {}", body);
            });
        }
    }

    bool reload_model(const std::string& path) {
        if (path.empty()) return false;
        bool ok = false;
        if (cfg_.onnx_enabled) {
            ok = onnx_engine_.load(path, cfg_.onnx_intra_threads);
            if (ok) {
                use_onnx_ = true;
                spdlog::info("[RELOAD] ONNX model reloaded from {}", path);
            }
        } else {
            ok = gru_model_.load_weights(path);
            if (ok) spdlog::info("[RELOAD] GRU model reloaded from {}", path);
        }
        if (!ok) spdlog::error("[RELOAD] Failed to reload model from {}", path);
        return ok;
    }

    void reset_circuit_breaker() {
        risk_.circuit_breaker().manual_reset();
        // Stage 6: Reset control plane FSMs
        control_plane_.manual_reset(tick_counter_);
        spdlog::info("[CONTROL] Circuit breaker and control plane manually reset");
    }

    // Stage 6: Control plane accessors
    const ControlPlane& control_plane() const { return control_plane_; }
    SystemModeSnapshot system_mode() const {
        auto hl = static_cast<uint8_t>(strategy_health_.snapshot().level);
        double as = strategy_health_.snapshot().activity_scale;
        return control_plane_.resolve_mode(hl, as);
    }

private:
    void setup_logging() {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);

        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            cfg_.log_dir + "/system.log", 50 * 1024 * 1024, 5);
        file_sink->set_level(spdlog::level::debug);

        auto logger = std::make_shared<spdlog::logger>("bybit",
            spdlog::sinks_init_list{console_sink, file_sink});
        logger->set_level(spdlog::level::debug);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
        spdlog::set_default_logger(logger);
    }

    // ─── Timer scheduling (strategy fallback, metrics, watchdog, PnL) ───
#include "timer_manager.inl"

    // ─── Core strategy pipeline (hot/warm/cold path separation) ─────────
#include "strategy_pipeline.inl"

    // ─── Order reconciliation (exchange state sync) ───────────────────
#include "reconciliation.inl"

    void shutdown() {
        spdlog::info("Shutting down...");

        ws_mgr_.stop();
        if (ws_trade_) ws_trade_->stop();

        if (!cfg_.paper_trading && exec_.has_active_orders()) {
            spdlog::info("Cancelling all active orders...");
            rest_.cancel_all_orders(cfg_.symbol, [](bool ok, const std::string& body) {
                if (!ok) spdlog::warn("Failed to cancel all orders on shutdown: {}", body);
            });
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        // Save model weights for next session
        if (!cfg_.ml_model_path.empty()) {
            std::string save_path = cfg_.ml_model_path + ".updated";
            if (gru_model_.save_weights(save_path)) {
                spdlog::info("Model weights saved to {}", save_path);
            }
        }

        recorder_.stop();

        report_metrics();
        spdlog::info("Shutdown complete");
    }

    AppConfig cfg_;
    ssl::context ssl_ctx_;
    net::io_context ioc_;
    net::io_context cold_ioc_;  // #3: Isolated io_context for cold/background work
    net::strand<net::io_context::executor_type> strategy_strand_;  // C4: Serializes strategy_tick

    // ── Core data engines (stack-allocated, no heap in hot path) ─────────
    OrderBook ob_;
    TradeFlowEngine tf_;
    Portfolio portfolio_;
    Metrics metrics_;

    // ── AI / Strategy engines ───────────────────────────────────────────
    AdvancedFeatureEngine feature_engine_;
    GRUModelEngine gru_model_;
    OnnxInferenceEngine onnx_engine_;
    ModelAccuracyTracker accuracy_tracker_;
    RegimeDetector regime_detector_;
    bool use_onnx_ = false;
    AdaptiveThreshold adaptive_threshold_;
    FillProbabilityModel fill_prob_model_;
    AdaptivePositionSizer position_sizer_;
    ModelOutput last_prediction_;
    int inference_gate_skip_ = 0;  // E4: ticks remaining to skip inference

    // ── Analytics & RL ──────────────────────────────────────────────────
    StrategyMetrics strategy_metrics_;
    StrategyHealthMonitor strategy_health_;
    FeatureImportanceAnalyzer feature_importance_;
    SystemMonitor system_monitor_;
    RLOptimizer rl_optimizer_;
    double prev_equity_ = 0.0;
    double prev_mid_for_fi_ = 0.0;
    double prev_rl_pnl_ = 0.0;
    RLState prev_rl_state_;
    RLAction prev_rl_action_;

    // ── Risk & Execution ────────────────────────────────────────────────
    EnhancedRiskEngine risk_;
    RestClient rest_;
    SmartExecutionEngine exec_;
    WsManager ws_mgr_;
    std::unique_ptr<WsTradeClient> ws_trade_;  // C3: WS Trade API client

    // ── Stage 6: Control Plane ──────────────────────────────────────────
    ControlPlane control_plane_;
    SystemModeSnapshot last_system_mode_;

    // ── Persistence ─────────────────────────────────────────────────────
    ResearchRecorder recorder_;
    simdjson::ondemand::parser reconcile_parser_;  // M4: Reused in reconcile_orders

    // ── Timers (M3: std::optional avoids heap allocation) ──────────────
    std::optional<net::steady_timer> strategy_timer_;
    std::optional<net::steady_timer> metrics_timer_;
    std::optional<net::steady_timer> watchdog_timer_;
    std::optional<net::steady_timer> pnl_timer_;
    std::optional<net::steady_timer> reconciliation_timer_;  // #8: Order reconciliation

    std::atomic<bool> running_{false};
    std::atomic<uint64_t> tick_counter_{0};  // C4: Atomic for cross-thread reads
    uint64_t last_strategy_tick_ns_ = 0;  // #2: For fallback timer check
    TickLatency last_tick_latency_{};

    // ── Stage 4: Hot-path isolation ────────────────────────────────────
    DeferredWorkQueue deferred_q_;
    HotPathCounters   hot_counters_;
    LoadShedState     load_shed_;
    TickColdWork      cold_work_;

    // ── Thread-safe snapshot for GUI ────────────────────────────────────
    SnapshotSeqLock ui_seqlock_;

    // ─── UI snapshot publisher (SeqLock-based, zero-mutex) ──────────────
#include "ui_publisher.inl"
};

} // namespace bybit
