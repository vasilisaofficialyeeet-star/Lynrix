// strategy_pipeline.inl — Core strategy tick pipeline (hot/warm/cold)
// Extracted from application.h for M2 (god header breakup).
// Included inside the Application class body.

    // ─── CORE STRATEGY PIPELINE (Stage 4: hot/warm/cold separation) ────
    //
    // Structure:
    //   HOT   (stages 1-8): bounded, no logging, no formatting, no alloc.
    //   WARM  (stages 9-10): mark-to-market, latency capture, UI publish.
    //   COLD  (stages 11-15): logging, RL, analytics, health — deferred.
    //         Load-shed cold work if hot path exceeded budget.
    //
    void strategy_tick() {
        uint64_t start_ns = Clock::now_ns();
        last_strategy_tick_ns_ = start_ns;  // #2: Track for fallback timer
        TickLatency tl{};
        tl.tick_id = tick_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
        ++hot_counters_.ticks_total;
        cold_work_.clear();

        if (!ob_.valid()) return;

        // ══════════════════════════════════════════════════════════════════
        // HOT PATH — no spdlog, no snprintf, no alloc, bounded stages
        // ══════════════════════════════════════════════════════════════════

        // ── 1. Compute 25 features (HOT) ────────────────────────────────
        Features f{};
        {
            BYBIT_HOT_PATH
            ScopedStageTimer _t(PipelineStage::FeatureCompute,
                                tl.stage_ns[static_cast<size_t>(PipelineStage::FeatureCompute)]);
            f = feature_engine_.compute(ob_, tf_);
        }
        uint64_t feat_ns = Clock::now_ns();
        cold_work_.feat_latency_ns = feat_ns - start_ns;
        ++hot_counters_.features_computed;

        // ── 2. Market regime detection (HOT) ────────────────────────────
        RegimeState regime{};
        {
            BYBIT_HOT_PATH
            ScopedStageTimer _t(PipelineStage::RegimeDetect,
                                tl.stage_ns[static_cast<size_t>(PipelineStage::RegimeDetect)]);
            regime = regime_detector_.update(f);
        }
        MarketRegime current_regime = regime.current;

        // Capture regime change for cold-path logging (no spdlog here)
        if (regime.current != regime.previous && cfg_.regime_detection_enabled) {
            cold_work_.regime_changed = true;
            cold_work_.regime_prev = static_cast<uint8_t>(regime.previous);
            cold_work_.regime_curr = static_cast<uint8_t>(regime.current);
            cold_work_.regime_confidence = regime.confidence;
            cold_work_.regime_volatility = regime.volatility;
        }

        // ── 3. ML model inference (HOT) ─────────────────────────────────
        ModelOutput prediction;
        if (cfg_.ml_model_enabled && feature_engine_.tick_count() >= 20) {
            // E4: Confidence gate — skip inference if rolling confidence is
            // consistently below minimum (saves ~2-5µs/tick during dead zones)
            bool gate_open = true;
            if (inference_gate_skip_ > 0) {
                --inference_gate_skip_;
                gate_open = false;
                ++hot_counters_.inference_gated;
            } else if (last_prediction_.model_confidence < 0.15 &&
                       accuracy_tracker_.metrics().rolling_accuracy < 0.35) {
                // Low confidence + low accuracy → gate for 10 ticks (~100ms)
                inference_gate_skip_ = 10;
                gate_open = false;
                ++hot_counters_.inference_gated;
            }

            if (gate_open) {
                BYBIT_HOT_PATH
                ScopedStageTimer _t(PipelineStage::MLInference,
                                    tl.stage_ns[static_cast<size_t>(PipelineStage::MLInference)]);
                if (use_onnx_) {
                    prediction = onnx_engine_.predict(feature_engine_.history());
                } else {
                    prediction = gru_model_.predict(feature_engine_.history());
                }
                accuracy_tracker_.record_prediction(prediction, ob_.mid_price());
                ++hot_counters_.models_inferred;
            }
        }
        last_prediction_ = prediction;

        // ── 3b. Evaluate pending predictions (HOT) ──────────────────────
        accuracy_tracker_.evaluate_pending(start_ns, ob_.mid_price());

        if (accuracy_tracker_.metrics().total_predictions > 0) {
            double rolling_acc = accuracy_tracker_.metrics().rolling_accuracy;
            if (accuracy_tracker_.metrics().total_predictions % 10 == 0) {
                adaptive_threshold_.record_outcome(rolling_acc > 0.4);
            }
        }
        uint64_t model_ns = Clock::now_ns();
        cold_work_.model_latency_ns = model_ns - feat_ns;

        // Defer prediction logging flag (actual write in cold drain)
        cold_work_.log_prediction = cfg_.record_features;

        // ── 4. Adaptive threshold (HOT) ─────────────────────────────────
        double threshold = cfg_.signal_threshold;
        {
            BYBIT_HOT_PATH
            ScopedStageTimer _t(PipelineStage::SignalGenerate,
                                tl.stage_ns[static_cast<size_t>(PipelineStage::SignalGenerate)]);
            if (cfg_.adaptive_threshold_enabled) {
                threshold = adaptive_threshold_.update(f, regime);
            }
            if (cfg_.regime_detection_enabled) {
                double regime_threshold = regime_detector_.current_params().signal_threshold;
                threshold = (threshold + regime_threshold) * 0.5;
            }
        }

        // ── 5. Signal generation (HOT) ──────────────────────────────────
        bool signal_generated = false;
        Signal signal;
        signal.timestamp_ns = start_ns;
        signal.regime = current_regime;
        signal.adaptive_threshold = threshold;

        bool cb_ok = !risk_.circuit_breaker().is_tripped();
        // Stage 6: Also gate on control plane — new orders must be allowed
        bool ctrl_ok = control_plane_.risk_fsm().allows_new_orders() &&
                       control_plane_.exec_fsm().allows_new_orders();

        if (cb_ok && ctrl_ok) {
            if (prediction.probability_up > threshold) {
                signal.side = Side::Buy;
                double offset = regime_detector_.current_params().entry_offset_bps;
                signal.price = Price(ob_.best_bid() + (ob_.spread() * offset / 10000.0));
                signal.confidence = prediction.probability_up;
                signal_generated = true;
            } else if (prediction.probability_down > threshold) {
                signal.side = Side::Sell;
                double offset = regime_detector_.current_params().entry_offset_bps;
                signal.price = Price(ob_.best_ask() - (ob_.spread() * offset / 10000.0));
                signal.confidence = prediction.probability_down;
                signal_generated = true;
            }
        }

        // ── 6. Fill probability + adaptive position sizing (HOT) ────────
        if (signal_generated) {
            BYBIT_HOT_PATH
            ScopedStageTimer _t(PipelineStage::RiskCheck,
                                tl.stage_ns[static_cast<size_t>(PipelineStage::RiskCheck)]);

            FillProbability fp = fill_prob_model_.estimate(
                signal.side, signal.price, signal.qty.raw() > 0.0 ? signal.qty : Qty(cfg_.order_qty), ob_, tf_, f);
            signal.fill_prob = fp.prob_fill_500ms;

            Qty qty = Qty(cfg_.order_qty);
            if (cfg_.adaptive_sizing_enabled) {
                Position pos = portfolio_.snapshot();
                double liquidity = (f.bid_depth_total + f.ask_depth_total) * 0.5;
                qty = position_sizer_.compute(
                    signal.confidence, f.volatility, liquidity,
                    f.spread_bps, pos, current_regime);
            }
            // Stage 6: Apply control plane position scale
            double ctrl_scale = control_plane_.risk_fsm().position_scale();
            double health_scale = strategy_health_.snapshot().activity_scale;
            double combined_scale = ctrl_scale * health_scale;
            if (combined_scale < 1.0) {
                qty = Qty(qty.raw() * combined_scale);
            }
            signal.qty = qty;

            signal.expected_move = BasisPoints(prediction.horizons[1].predicted_move_bps);
            signal.expected_pnl = Notional(signal.qty.raw() * ob_.mid_price() * signal.expected_move.raw() / 10000.0);
        }

        // ── 7. Smart execution dispatch (HOT) ───────────────────────────
        if (signal_generated) {
            BYBIT_HOT_PATH
            ScopedStageTimer _t(PipelineStage::ExecutionDecide,
                                tl.stage_ns[static_cast<size_t>(PipelineStage::ExecutionDecide)]);
            exec_.on_signal(signal, ob_, fill_prob_model_, f, tf_);
            ++hot_counters_.signals_generated;
            ++hot_counters_.orders_dispatched;

            // Capture signal data for cold-path logging
            cold_work_.log_signal = true;
            cold_work_.signal_side = static_cast<uint8_t>(signal.side);
            cold_work_.signal_price = signal.price.raw();
            cold_work_.signal_qty = signal.qty.raw();
            cold_work_.signal_confidence = signal.confidence;
            cold_work_.signal_fill_prob = signal.fill_prob;
        }

        // ── 8. Order re-quote check (HOT) ───────────────────────────────
        if (cfg_.requote_enabled) {
            exec_.requote_check(ob_, fill_prob_model_, f, tf_);
        } else {
            exec_.cancel_stale_orders(ob_);
        }

        // ══════════════════════════════════════════════════════════════════
        // WARM PATH — bounded, may enqueue, no formatting
        // ══════════════════════════════════════════════════════════════════

        // ── 9. Mark to market (WARM) ────────────────────────────────────
        {
            BYBIT_WARM_PATH
            ScopedStageTimer _t(PipelineStage::MarkToMarket,
                                tl.stage_ns[static_cast<size_t>(PipelineStage::MarkToMarket)]);
            portfolio_.mark_to_market(Price(ob_.mid_price()));
        }

        // ── 10. End-to-end latency capture + publish counters (WARM) ────
        {
            BYBIT_WARM_PATH
            uint64_t end_ns = Clock::now_ns();
            uint64_t e2e_latency = end_ns - start_ns;
            tl.total_ns = e2e_latency;
            tl.budget_exceeded = (tl.hot_path_ns() > TOTAL_HOT_BUDGET_NS);
            last_tick_latency_ = tl;
            cold_work_.e2e_latency_ns = e2e_latency;
            cold_work_.budget_warning = (e2e_latency > 1'000'000);

            if (tl.budget_exceeded) {
                ++hot_counters_.budget_exceeded_count;
            }

            // Publish hot counters to shared metrics (relaxed atomics)
            metrics_.feature_calc_latency.record(cold_work_.feat_latency_ns);
            metrics_.model_inference_latency.record(cold_work_.model_latency_ns);
            metrics_.end_to_end_latency.record(e2e_latency);
            if (signal_generated) {
                metrics_.signals_total.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Defer feature/OB logging flags
        cold_work_.log_features = cfg_.record_features;
        cold_work_.log_ob_snapshot = cfg_.record_ob_snapshots;

        // ── 10b. Stage 6: Overload evaluation + risk state (WARM) ────────
        {
            BYBIT_WARM_PATH
            // Build overload signals from current tick data
            OverloadSignals overload_sigs;
            overload_sigs.tick_budget_exceeded = tl.budget_exceeded;
            overload_sigs.latency_spike = (tl.total_ns > TOTAL_HOT_BUDGET_NS * 3);
            overload_sigs.queue_depth_high = (deferred_q_.size() >= 48);
            overload_sigs.timestamp_ns = Clock::now_ns();

            // Evaluate overload → may escalate/deescalate exec FSM
            control_plane_.evaluate_overload(overload_sigs, tl.tick_id);

            // Evaluate risk state → maps CB/drawdown/loss to RiskEvent
            risk_.evaluate_risk_state(control_plane_, tl.tick_id);

            // Resolve composite system mode (used by UI and next tick)
            auto hl = static_cast<uint8_t>(strategy_health_.snapshot().level);
            double as = strategy_health_.snapshot().activity_scale;
            last_system_mode_ = control_plane_.resolve_mode(hl, as);
        }

        // ── 11. Publish UI snapshot (WARM — SeqLock, bounded) ───────────
        {
            BYBIT_WARM_PATH
            ScopedStageTimer _t(PipelineStage::UIPublish,
                                tl.stage_ns[static_cast<size_t>(PipelineStage::UIPublish)]);
            publish_ui_snapshot();
        }

        // ══════════════════════════════════════════════════════════════════
        // COLD PATH — deferred logging, analytics, RL, health
        // Load-shed if hot path exceeded budget.
        // ══════════════════════════════════════════════════════════════════

        bool shed_cold = tl.budget_exceeded;
        load_shed_.record_tick(shed_cold, tl.tick_id);
        if (shed_cold) {
            ++hot_counters_.cold_shed_count;
            tl.load_shed = true;
            last_tick_latency_ = tl;
            return; // Skip all cold work this tick
        }

        drain_cold_work(f, prediction, regime, current_regime, signal, signal_generated, threshold);
    }

    // ─── COLD DRAIN (Stage 4) ───────────────────────────────────────────
    // Called after hot+warm stages. May log, format, allocate.
    // Skipped entirely when load-shedding.

    BYBIT_COLD void drain_cold_work(
            const Features& f, const ModelOutput& prediction,
            const RegimeState& regime, MarketRegime current_regime,
            const Signal& signal, bool signal_generated, double threshold) {
        BYBIT_COLD_PATH

        // ── 12. Logging (COLD) ──────────────────────────────────────────
        if (cold_work_.regime_changed) {
            spdlog::info("[REGIME] {} -> {} (conf={:.2f} vol={:.6f})",
                         RegimeDetector::regime_name(static_cast<MarketRegime>(cold_work_.regime_prev)),
                         RegimeDetector::regime_name(static_cast<MarketRegime>(cold_work_.regime_curr)),
                         cold_work_.regime_confidence, cold_work_.regime_volatility);
            recorder_.log_regime_change(f.timestamp_ns,
                                        static_cast<MarketRegime>(cold_work_.regime_prev),
                                        static_cast<MarketRegime>(cold_work_.regime_curr),
                                        cold_work_.regime_confidence,
                                        cold_work_.regime_volatility);
        }

        if (cold_work_.log_prediction) {
            recorder_.log_prediction(f.timestamp_ns,
                prediction.horizons[0].prob_up, prediction.horizons[0].prob_down,
                prediction.horizons[1].prob_up, prediction.horizons[1].prob_down,
                prediction.horizons[2].prob_up, prediction.horizons[2].prob_down,
                prediction.horizons[3].prob_up, prediction.horizons[3].prob_down,
                prediction.model_confidence, prediction.inference_latency_ns);
        }

        if (cold_work_.log_signal && signal_generated) {
            recorder_.log_signal(signal);
            spdlog::debug("[SIGNAL] {} conf={:.3f} thresh={:.3f} qty={:.4f} "
                          "fill_prob={:.3f} regime={} expected_pnl={:.4f}",
                          signal.side == Side::Buy ? "BUY" : "SELL",
                          signal.confidence, threshold, signal.qty.raw(),
                          signal.fill_prob,
                          RegimeDetector::regime_name(current_regime),
                          signal.expected_pnl.raw());
        }

        if (cold_work_.log_features) {
            recorder_.log_features(f);
        }
        if (cold_work_.log_ob_snapshot) {
            recorder_.log_ob_snapshot(f.timestamp_ns, ob_.best_bid(), ob_.best_ask(),
                                      f.bid_depth_total, f.ask_depth_total,
                                      f.microprice, ob_.spread());
        }

        if (cold_work_.budget_warning) {
            spdlog::warn("[LATENCY] e2e={:.1f}µs exceeded 1ms budget",
                         static_cast<double>(cold_work_.e2e_latency_ns) / 1000.0);
        }

        // ── 13. Strategy metrics update (COLD) ─────────────────────────
        {
            Position pos = portfolio_.snapshot();
            double equity = pos.realized_pnl.raw() + pos.unrealized_pnl.raw() + pos.funding_impact.raw();
            strategy_metrics_.tick(equity, prev_equity_);
            prev_equity_ = equity;
        }

        // ── 14. Feature importance (COLD, periodic every 100 ticks) ─────
        if (feature_engine_.tick_count() % 100 == 0 && feature_engine_.tick_count() > 200) {
            double mid = ob_.mid_price();
            if (std::abs(prev_mid_for_fi_) > 1e-12 && mid > 0.0) {
                double move_bps = (mid - prev_mid_for_fi_) / prev_mid_for_fi_ * 10000.0;
                int target = (move_bps > 1.0) ? 1 : (move_bps < -1.0 ? -1 : 0);
                feature_importance_.record_sample(f, target, move_bps);
            }
            prev_mid_for_fi_ = mid;

            if (feature_engine_.tick_count() % 500 == 0) {
                feature_importance_.compute();
            }
        }

        // ── 15. RL optimizer step (COLD, periodic every 50 ticks) ───────
        if (cfg_.rl_enabled && feature_engine_.tick_count() % 50 == 0) {
            RLState rl_state;
            rl_state.volatility = f.volatility;
            rl_state.spread_bps = f.spread_bps;
            rl_state.liquidity_depth = (f.bid_depth_total + f.ask_depth_total) * 0.5;
            rl_state.model_confidence = prediction.model_confidence;
            rl_state.recent_pnl = strategy_metrics_.snapshot().total_pnl;
            rl_state.drawdown = strategy_metrics_.snapshot().current_drawdown;
            rl_state.win_rate = strategy_metrics_.snapshot().win_rate;
            rl_state.sharpe = strategy_metrics_.snapshot().sharpe_ratio;
            // S6: Actual fill rate from execution stats
            auto es = exec_.stats();
            rl_state.fill_rate = (es.orders_submitted > 0)
                ? static_cast<double>(es.orders_filled) / static_cast<double>(es.orders_submitted)
                : 0.5;
            rl_state.regime_stability = regime.confidence;

            double reward = RLOptimizer::compute_reward(
                strategy_metrics_.snapshot().total_pnl - prev_rl_pnl_,
                strategy_metrics_.snapshot().current_drawdown,
                strategy_metrics_.snapshot().sharpe_ratio,
                strategy_metrics_.snapshot().win_rate);
            prev_rl_pnl_ = strategy_metrics_.snapshot().total_pnl;

            RLAction action = rl_optimizer_.act(rl_state);
            rl_optimizer_.step(prev_rl_state_, prev_rl_action_, reward, rl_state);
            prev_rl_state_ = rl_state;
            prev_rl_action_ = action;
        }

        // ── 16. Strategy health update (COLD, every 200 ticks) ──────────
        if (feature_engine_.tick_count() % 200 == 0) {
            double rolling_acc = accuracy_tracker_.metrics().rolling_accuracy;
            // S6: Actual fill rate from execution stats
            auto es = exec_.stats();
            double fill_rate = (es.orders_submitted > 0)
                ? static_cast<double>(es.orders_filled) / static_cast<double>(es.orders_submitted)
                : 0.5;
            strategy_health_.update(strategy_metrics_.snapshot(),
                                    rolling_acc, fill_rate, current_regime);

            // Stage 6: Fire health events into control plane
            auto hl = strategy_health_.snapshot().level;
            if (hl >= StrategyHealthLevel::Warning) {
                control_plane_.risk_event(RiskEvent::HealthDegraded, tick_counter_,
                    StrategyHealthMonitor::level_name(hl));
                spdlog::warn("[HEALTH] Strategy health: {} (score={:.2f} scale={:.2f})",
                             StrategyHealthMonitor::level_name(hl),
                             strategy_health_.snapshot().health_score,
                             strategy_health_.snapshot().activity_scale);
            } else if (hl <= StrategyHealthLevel::Good) {
                control_plane_.risk_event(RiskEvent::HealthRecovered, tick_counter_,
                    StrategyHealthMonitor::level_name(hl));
            }
        }
    }
