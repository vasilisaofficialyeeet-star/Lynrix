// reconciliation.inl — Order reconciliation with exchange
// Extracted from application.h for M2 (god header breakup).
// Included inside the Application class body.

    // #8: Order reconciliation — periodic check that local state matches exchange
    void start_reconciliation_timer() {
        if (cfg_.paper_trading) return;  // No reconciliation in paper mode
        reconciliation_timer_.emplace(ioc_);
        schedule_reconciliation();
    }

    void schedule_reconciliation() {
        if (!running_.load(std::memory_order_acquire)) return;
        if (!reconciliation_timer_) return;

        reconciliation_timer_->expires_after(std::chrono::seconds(15));
        reconciliation_timer_->async_wait([this](boost::system::error_code ec) {
            if (ec || !running_.load(std::memory_order_acquire)) return;
            reconcile_orders();
            schedule_reconciliation();
        });
    }

    void reconcile_orders() {
        if (!exec_.has_active_orders()) return;

        rest_.get_open_orders(cfg_.symbol,
            [this](bool success, const std::string& body) {
                if (!success) {
                    spdlog::warn("[RECONCILE] Failed to fetch open orders");
                    return;
                }

                auto padded = simdjson::padded_string(body);
                auto err = reconcile_parser_.iterate(padded);
                if (err.error()) return;
                simdjson::ondemand::document doc = std::move(err.value_unsafe());

                auto rc = doc.find_field("retCode");
                if (!rc.error() && rc.get_int64().value() != 0) return;

                auto result = doc.find_field("result");
                if (result.error()) return;
                auto list = result.find_field("list");
                if (list.error()) return;

                // Parse exchange-side order IDs into a flat set
                static constexpr size_t MAX_RECONCILE = 128;
                std::array<char[48], MAX_RECONCILE> exchange_ids{};
                size_t exchange_count = 0;

                for (auto item : list.get_array()) {
                    if (exchange_count >= MAX_RECONCILE) break;
                    auto oid = item.find_field("orderId");
                    if (oid.error()) continue;
                    std::string_view oid_sv = oid.get_string().value();
                    std::memset(exchange_ids[exchange_count], 0, 48);
                    std::memcpy(exchange_ids[exchange_count], oid_sv.data(),
                                std::min(oid_sv.size(), size_t(47)));
                    ++exchange_count;
                }

                size_t local_count = exec_.active_order_count();

                // Detect local orders not present on exchange (stale/phantom)
                size_t phantom_count = 0;
                for (size_t i = 0; i < local_count; ++i) {
                    const char* local_id = exec_.active_order_id(i);
                    if (!local_id) continue;
                    bool found = false;
                    for (size_t j = 0; j < exchange_count; ++j) {
                        if (std::strcmp(local_id, exchange_ids[j]) == 0) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        ++phantom_count;
                        spdlog::warn("[RECONCILE] Phantom local order not on exchange: {}",
                                     local_id);
                    }
                }

                if (exchange_count != local_count || phantom_count > 0) {
                    spdlog::warn("[RECONCILE] Mismatch: local={} exchange={} phantoms={}",
                                 local_count, exchange_count, phantom_count);
                    metrics_.reconciliation_mismatches.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }
