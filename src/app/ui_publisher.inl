// ui_publisher.inl — Thread-safe UI snapshot publishing via SeqLock
// Extracted from application.h for M2 (god header breakup).
// Included inside the Application class body.

    void publish_ui_snapshot() noexcept {
        UISnapshot snap;
        snap.snapshot_ns = Clock::now_ns();
        snap.engine_running = running_.load(std::memory_order_relaxed);

        // OrderBook
        snap.ob_valid = ob_.valid();
        if (snap.ob_valid) {
            snap.best_bid = ob_.best_bid();
            snap.best_ask = ob_.best_ask();
            snap.mid_price = ob_.mid_price();
            snap.spread = ob_.spread();
            snap.microprice = ob_.microprice();
            snap.ob_last_update = ob_.last_update_ns();
            const auto* b = ob_.bids();
            const auto* a = ob_.asks();
            snap.bid_count = static_cast<int>(std::min(ob_.bid_count(), static_cast<size_t>(UI_OB_LEVELS)));
            snap.ask_count = static_cast<int>(std::min(ob_.ask_count(), static_cast<size_t>(UI_OB_LEVELS)));
            for (int i = 0; i < snap.bid_count; ++i) { snap.bids[i] = {b[i].price, b[i].qty}; }
            for (int i = 0; i < snap.ask_count; ++i) { snap.asks[i] = {a[i].price, a[i].qty}; }
        }

        // Position
        auto pos = portfolio_.snapshot();
        snap.pos_size = pos.size.raw();
        snap.pos_entry = pos.entry_price.raw();
        snap.pos_unrealized = pos.unrealized_pnl.raw();
        snap.pos_realized = pos.realized_pnl.raw();
        snap.pos_funding = pos.funding_impact.raw();
        snap.pos_is_long = (pos.side == Side::Buy);

        // Metrics (atomics — safe to read)
        snap.ob_updates = metrics_.ob_updates_total.load(std::memory_order_relaxed);
        snap.trades_total = metrics_.trades_total.load(std::memory_order_relaxed);
        snap.signals_total = metrics_.signals_total.load(std::memory_order_relaxed);
        snap.orders_sent = metrics_.orders_sent_total.load(std::memory_order_relaxed);
        snap.orders_filled = metrics_.orders_filled_total.load(std::memory_order_relaxed);
        snap.orders_cancelled = metrics_.orders_cancelled_total.load(std::memory_order_relaxed);
        snap.ws_reconnects = metrics_.ws_reconnects_total.load(std::memory_order_relaxed);
        snap.e2e_p50_ns = metrics_.end_to_end_latency.percentile(0.50);
        snap.e2e_p99_ns = metrics_.end_to_end_latency.percentile(0.99);
        snap.feat_p50_ns = metrics_.feature_calc_latency.percentile(0.50);
        snap.feat_p99_ns = metrics_.feature_calc_latency.percentile(0.99);
        snap.model_p50_ns = metrics_.model_inference_latency.percentile(0.50);
        snap.model_p99_ns = metrics_.model_inference_latency.percentile(0.99);

        // Features, Regime, Prediction, Threshold
        snap.features = feature_engine_.last();
        snap.regime = regime_detector_.state();
        snap.prediction = last_prediction_;
        snap.threshold = adaptive_threshold_.state();

        // Circuit Breaker
        snap.cb_tripped = risk_.circuit_breaker().is_tripped();
        snap.cb_cooldown = risk_.circuit_breaker().tripped(); // raw tripped flag (includes cooldown)
        snap.cb_consec_losses = risk_.circuit_breaker().consecutive_losses();
        snap.cb_peak_pnl = risk_.circuit_breaker().peak_pnl();
        snap.cb_drawdown = risk_.drawdown();

        // Accuracy
        snap.accuracy = accuracy_tracker_.metrics();
        snap.using_onnx = use_onnx_;

        // Strategy / Health / System / RL / Feature Importance
        snap.strategy_metrics = strategy_metrics_.snapshot();
        snap.strategy_health = strategy_health_.snapshot();
        snap.system_monitor = system_monitor_.snapshot();
        snap.rl_state = rl_optimizer_.snapshot();
        snap.feature_importance = feature_importance_.snapshot();

        // Stage 6: Control Plane State
        snap.risk_state = static_cast<uint8_t>(last_system_mode_.risk_state);
        snap.exec_state = static_cast<uint8_t>(last_system_mode_.exec_state);
        snap.system_mode = static_cast<uint8_t>(last_system_mode_.mode);
        snap.ctrl_position_scale = last_system_mode_.position_scale;
        snap.ctrl_throttle_factor = last_system_mode_.throttle_factor;
        snap.ctrl_allows_new_orders = last_system_mode_.allows_new_orders;
        snap.ctrl_allows_increase = last_system_mode_.allows_increase;
        snap.ctrl_total_transitions = static_cast<uint32_t>(
            control_plane_.risk_fsm().snapshot().transitions_total +
            control_plane_.exec_fsm().snapshot().transitions_total);
        snap.ctrl_audit_depth = control_plane_.audit_trail().count();

        // Inference backend
        const char* be = OnnxInferenceEngine::backend_name(onnx_engine_.backend());
        std::strncpy(snap.inference_backend, be, sizeof(snap.inference_backend) - 1);

        // E5: WebSocket RTT
        snap.ws_rtt_us = ws_mgr_.ws_rtt_us();

        ui_seqlock_.publish(snap);
    }
