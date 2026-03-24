// timer_manager.inl — Timer scheduling, metrics reporting, watchdog, PnL logging
// Extracted from application.h for M2 (god header breakup).
// Included inside the Application class body.

    void start_strategy_timer() {
        strategy_timer_.emplace(ioc_);
        schedule_strategy_tick();
    }

    // #2/#19: Fallback timer — only fires if no OB deltas arrive within interval.
    // In event-driven mode this serves as a heartbeat / catch-all.
    void schedule_strategy_tick() {
        if (!running_.load(std::memory_order_acquire)) return;

        int tick_ms = cfg_.feature_tick_ms > 0 ? cfg_.feature_tick_ms : FEATURE_TICK_MS;
        // #19: Use 2x the configured tick as fallback interval (event-driven is primary)
        strategy_timer_->expires_after(std::chrono::milliseconds(tick_ms * 2));
        strategy_timer_->async_wait([this](boost::system::error_code ec) {
            if (ec || !running_.load(std::memory_order_acquire)) return;
            // C4: Dispatch fallback tick through same strand as WS-driven ticks
            net::post(strategy_strand_, [this]() {
                if (!running_.load(std::memory_order_acquire)) return;
                uint64_t since_last = Clock::now_ns() - last_strategy_tick_ns_;
                int tick_ms = cfg_.feature_tick_ms > 0 ? cfg_.feature_tick_ms : FEATURE_TICK_MS;
                if (since_last > static_cast<uint64_t>(tick_ms) * 1'500'000ULL) {
                    strategy_tick();
                }
            });
            schedule_strategy_tick();
        });
    }

    void start_metrics_timer() {
        metrics_timer_.emplace(ioc_);
        schedule_metrics_report();
    }

    void schedule_metrics_report() {
        if (!running_.load(std::memory_order_acquire)) return;

        metrics_timer_->expires_after(std::chrono::seconds(30));
        metrics_timer_->async_wait([this](boost::system::error_code ec) {
            if (ec || !running_.load(std::memory_order_acquire)) return;
            report_metrics();
            schedule_metrics_report();
        });
    }

    void report_metrics() {
        Position pos = portfolio_.snapshot();
        double net = portfolio_.net_pnl().raw();
        auto& regime = regime_detector_.state();

        spdlog::info("─── Metrics (AI Edition) ─────────────────────────");
        spdlog::info("  OB updates: {}  Trades: {}  Signals: {}",
                     metrics_.ob_updates_total.load(std::memory_order_relaxed),
                     metrics_.trades_total.load(std::memory_order_relaxed),
                     metrics_.signals_total.load(std::memory_order_relaxed));
        spdlog::info("  Orders: sent={} filled={} cancelled={}",
                     metrics_.orders_sent_total.load(std::memory_order_relaxed),
                     metrics_.orders_filled_total.load(std::memory_order_relaxed),
                     metrics_.orders_cancelled_total.load(std::memory_order_relaxed));
        spdlog::info("  Latency (p50/p99): e2e={}/{}µs feat={}/{}µs model={}/{}µs",
                     metrics_.end_to_end_latency.percentile(0.50) / 1000,
                     metrics_.end_to_end_latency.percentile(0.99) / 1000,
                     metrics_.feature_calc_latency.percentile(0.50) / 1000,
                     metrics_.feature_calc_latency.percentile(0.99) / 1000,
                     metrics_.model_inference_latency.percentile(0.50) / 1000,
                     metrics_.model_inference_latency.percentile(0.99) / 1000);
        spdlog::info("  Regime: {} (conf={:.2f}) Threshold: {:.3f} Accuracy: {:.2f}",
                     RegimeDetector::regime_name(regime.current),
                     regime.confidence,
                     adaptive_threshold_.current(),
                     adaptive_threshold_.state().recent_accuracy);
        spdlog::info("  Model: prob_up={:.3f} prob_down={:.3f} conf={:.3f} "
                     "inference={}µs",
                     last_prediction_.probability_up,
                     last_prediction_.probability_down,
                     last_prediction_.model_confidence,
                     last_prediction_.inference_latency_ns / 1000);
        spdlog::info("  Position: size={:.4f} entry={:.1f} side={}",
                     pos.size.raw(), pos.entry_price.raw(),
                     pos.side == Side::Buy ? "Buy" : "Sell");
        spdlog::info("  PnL: realized={:.4f} unrealized={:.4f} net={:.4f} "
                     "drawdown={:.2f}%",
                     pos.realized_pnl.raw(), pos.unrealized_pnl.raw(), net,
                     risk_.drawdown() * 100.0);
        if (risk_.circuit_breaker().tripped()) {
            spdlog::warn("  CIRCUIT BREAKER: TRIPPED ({})",
                         risk_.circuit_breaker().trip_reason());
        }
        auto& acc = accuracy_tracker_.metrics();
        if (acc.total_predictions > 0) {
            spdlog::info("  Accuracy: total={} acc={:.3f} rolling={:.3f} calib_err={:.3f}",
                         acc.total_predictions, acc.accuracy,
                         acc.rolling_accuracy, acc.calibration_error);
            spdlog::info("  Precision (up/dn/fl): {:.3f}/{:.3f}/{:.3f}  "
                         "Recall: {:.3f}/{:.3f}/{:.3f}",
                         acc.per_class[0].precision, acc.per_class[1].precision,
                         acc.per_class[2].precision,
                         acc.per_class[0].recall, acc.per_class[1].recall,
                         acc.per_class[2].recall);
        }
        // Strategy metrics
        auto& sm = strategy_metrics_.snapshot();
        spdlog::info("  Strategy: sharpe={:.2f} sortino={:.2f} winrate={:.1f}% "
                     "pf={:.2f} maxDD={:.2f}%",
                     sm.sharpe_ratio, sm.sortino_ratio, sm.win_rate * 100.0,
                     sm.profit_factor, sm.max_drawdown_pct * 100.0);
        spdlog::info("  Health: {} (score={:.2f} activity={:.1f}x)",
                     StrategyHealthMonitor::level_name(strategy_health_.snapshot().level),
                     strategy_health_.snapshot().health_score,
                     strategy_health_.snapshot().activity_scale);
        if (cfg_.rl_enabled) {
            spdlog::info("  RL: steps={} updates={} reward={:.3f}",
                         rl_optimizer_.snapshot().total_steps,
                         rl_optimizer_.snapshot().total_updates,
                         rl_optimizer_.snapshot().avg_reward);
        }
        spdlog::info("─────────────────────────────────────────────────");

        // Update system monitor
        system_monitor_.update(
            metrics_.ob_updates_total.load(std::memory_order_relaxed),
            metrics_.signals_total.load(std::memory_order_relaxed),
            metrics_.orders_sent_total.load(std::memory_order_relaxed),
            metrics_.orders_filled_total.load(std::memory_order_relaxed));
        system_monitor_.update_latencies(
            metrics_.ws_message_latency.percentile(0.50) / 1000.0,
            metrics_.ws_message_latency.percentile(0.99) / 1000.0,
            metrics_.ob_update_latency.percentile(0.50) / 1000.0,
            metrics_.ob_update_latency.percentile(0.99) / 1000.0,
            metrics_.feature_calc_latency.percentile(0.50) / 1000.0,
            metrics_.feature_calc_latency.percentile(0.99) / 1000.0,
            metrics_.model_inference_latency.percentile(0.50) / 1000.0,
            metrics_.model_inference_latency.percentile(0.99) / 1000.0,
            metrics_.risk_check_latency.percentile(0.50) / 1000.0,
            metrics_.risk_check_latency.percentile(0.99) / 1000.0,
            metrics_.order_submit_latency.percentile(0.50) / 1000.0,
            metrics_.order_submit_latency.percentile(0.99) / 1000.0,
            metrics_.end_to_end_latency.percentile(0.50) / 1000.0,
            metrics_.end_to_end_latency.percentile(0.99) / 1000.0);
    }

    void start_watchdog() {
        watchdog_timer_.emplace(ioc_);
        schedule_watchdog();
    }

    void schedule_watchdog() {
        if (!running_.load(std::memory_order_acquire)) return;

        watchdog_timer_->expires_after(std::chrono::milliseconds(500));
        watchdog_timer_->async_wait([this](boost::system::error_code ec) {
            if (ec || !running_.load(std::memory_order_acquire)) return;
            watchdog_check();
            schedule_watchdog();
        });
    }

    void watchdog_check() {
        if (ob_.valid()) {
            uint64_t age_ns = Clock::now_ns() - ob_.last_update_ns();
            uint64_t max_age_ns = 10'000'000'000ULL;
            if (age_ns > max_age_ns) {
                spdlog::warn("Watchdog: OB stale for {:.1f}s",
                             static_cast<double>(age_ns) / 1e9);
            }
        }

        // Circuit breaker auto-recovery check
        if (risk_.circuit_breaker().tripped() && !risk_.circuit_breaker().is_tripped()) {
            spdlog::info("Circuit breaker cooldown expired, trading resumed");
        }
    }

    void start_pnl_logger() {
        pnl_timer_.emplace(ioc_);
        schedule_pnl_log();
    }

    void schedule_pnl_log() {
        if (!running_.load(std::memory_order_acquire)) return;

        pnl_timer_->expires_after(std::chrono::seconds(5));
        pnl_timer_->async_wait([this](boost::system::error_code ec) {
            if (ec || !running_.load(std::memory_order_acquire)) return;

            Position pos = portfolio_.snapshot();
            recorder_.log_pnl(Clock::now_ns(), pos.realized_pnl.raw(), pos.unrealized_pnl.raw(),
                              pos.size.raw(), pos.entry_price.raw(), pos.mark_price.raw());

            // Update risk engine
            double equity = pos.realized_pnl.raw() + pos.unrealized_pnl.raw() + pos.funding_impact.raw();
            risk_.update_pnl(Notional(pos.realized_pnl.raw()), Notional(equity));

            schedule_pnl_log();
        });
    }
