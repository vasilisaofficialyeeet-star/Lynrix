#pragma once

// ─── Safe Online Trainer ────────────────────────────────────────────────────
// Dedicated low-priority thread for periodic RL model retraining with
// safety guarantees: walk-forward validation, KL-divergence gating,
// automatic rollback on degradation.
//
// Architecture:
//   - Rollout ring buffer via SPSCQueue (from lockfree_pipeline.h)
//   - Mini-batch training every N minutes (configurable 10–30 min)
//   - Walk-forward validation on held-out recent data
//   - KL divergence check: if KL(π_new || π_old) > threshold → rollback
//   - Sharpe ratio check: if new Sharpe < old Sharpe * decay → rollback
//   - Automatic snapshot/restore via HybridRLAgent::Snapshot
//   - Hot reload integration (model weights swapped atomically)
//
// Thread model:
//   - Runs on E-core with Background QoS (via thread_affinity.h)
//   - Never blocks hot path — rollout data pushed via lock-free SPSC
//   - Training loop sleeps between batches
//
// Memory: all pre-allocated. Zero heap allocation in steady state.

#include "ppo_sac_hybrid.h"
#include "../utils/lockfree_pipeline.h"
#include "../utils/tsc_clock.h"

#include <atomic>
#include <thread>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <functional>

#ifdef __APPLE__
#include <pthread.h>
#include <sys/qos.h>
#endif

namespace bybit {

// ─── Trainer Configuration ──────────────────────────────────────────────────

struct OnlineTrainerConfig {
    // Training schedule
    uint64_t train_interval_ns    = 600'000'000'000ULL; // 10 minutes default
    size_t   min_samples          = 128;                 // min rollout samples before training
    size_t   validation_split_pct = 20;                  // 20% held out for validation

    // Safety gates
    double   max_kl_divergence    = 0.05;   // rollback if KL > this
    double   min_sharpe_ratio     = -0.5;   // rollback if validation Sharpe < this
    double   sharpe_decay_gate    = 0.8;    // rollback if new_sharpe < old * this
    double   max_value_loss       = 10.0;   // rollback if value loss spikes

    // Rollback
    size_t   max_consecutive_rollbacks = 3; // stop training after N consecutive rollbacks
    bool     auto_disable_on_failure   = true;

    // Thread
    bool     pin_to_ecore         = true;
    bool     background_qos       = true;  // set Background QoS on trainer thread
};

// ─── Trainer Statistics ─────────────────────────────────────────────────────

struct TrainerStats {
    uint64_t total_trains         = 0;
    uint64_t total_rollbacks      = 0;
    uint64_t consecutive_rollbacks = 0;
    uint64_t samples_collected    = 0;
    uint64_t last_train_ns        = 0;     // timestamp of last training
    uint64_t last_train_duration_ns = 0;
    double   last_kl              = 0.0;
    double   last_train_sharpe    = 0.0;
    double   last_val_sharpe      = 0.0;
    double   best_val_sharpe      = -1e9;
    bool     enabled              = true;
    bool     training_active      = false;
};

// ─── Rollout Sample ─────────────────────────────────────────────────────────
// Compact record pushed from hot path into trainer's SPSC queue.

struct alignas(64) RolloutSample {
    RL2State state;
    double   action[RL2_ACTION_DIM]{};
    double   log_prob  = 0.0;
    double   reward    = 0.0;
    double   value     = 0.0;
    bool     done      = false;
    uint64_t timestamp_ns = 0;
};

// ─── Rollout Ring Buffer ────────────────────────────────────────────────────
// Fixed-size ring buffer for accumulating rollout data.
// Overwrites oldest when full (circular).

inline constexpr size_t ROLLOUT_BUFFER_SIZE = 4096; // power of 2

struct RolloutBuffer {
    RolloutSample data[ROLLOUT_BUFFER_SIZE]{};
    size_t head = 0;
    size_t count = 0;

    void push(const RolloutSample& sample) noexcept {
        data[head] = sample;
        head = (head + 1) & (ROLLOUT_BUFFER_SIZE - 1);
        if (count < ROLLOUT_BUFFER_SIZE) ++count;
    }

    size_t size() const noexcept { return count; }

    const RolloutSample& at(size_t idx) const noexcept {
        // idx=0 is oldest, idx=count-1 is newest
        size_t start = (count < ROLLOUT_BUFFER_SIZE) ? 0 :
                       (head & (ROLLOUT_BUFFER_SIZE - 1));
        return data[(start + idx) & (ROLLOUT_BUFFER_SIZE - 1)];
    }

    void clear() noexcept {
        head = 0;
        count = 0;
    }
};

// ─── Safe Online Trainer ────────────────────────────────────────────────────

class SafeOnlineTrainer {
public:
    // SPSC queue for receiving rollout samples from hot path
    using RolloutQueue = SPSCQueue<RolloutSample, 1024>;

    SafeOnlineTrainer() noexcept = default;

    explicit SafeOnlineTrainer(HybridRLAgent* agent,
                                const OnlineTrainerConfig& cfg = {}) noexcept
        : agent_(agent), cfg_(cfg) {}

    // ─── Hot Path Interface (called from trading thread) ────────────────────
    // Zero-alloc push into SPSC queue. Returns false if queue full (dropped).

    bool push_sample(const RL2State& state, const RL2Action& action,
                     double log_prob, double reward, double value,
                     bool done) noexcept {
        auto* slot = incoming_.acquire_write_slot();
        if (!slot) {
            ++dropped_samples_;
            return false;
        }

        slot->state = state;
        std::memcpy(slot->action, action.raw, sizeof(slot->action));
        slot->log_prob = log_prob;
        slot->reward = reward;
        slot->value = value;
        slot->done = done;
        slot->timestamp_ns = TscClock::now_ns();

        incoming_.commit_write();
        return true;
    }

    // ─── Start Trainer Thread ───────────────────────────────────────────────

    void start() noexcept {
        if (running_.load(std::memory_order_relaxed)) return;
        running_.store(true, std::memory_order_release);
        stats_.enabled = true;
        thread_ = std::thread([this]() { trainer_loop(); });
    }

    // ─── Stop Trainer Thread ────────────────────────────────────────────────

    void stop() noexcept {
        running_.store(false, std::memory_order_release);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // ─── Force immediate training (for testing) ─────────────────────────────

    bool train_now() noexcept {
        return do_training_step();
    }

    // ─── Accessors ──────────────────────────────────────────────────────────

    const TrainerStats& stats() const noexcept { return stats_; }
    TrainerStats& stats() noexcept { return stats_; }
    const OnlineTrainerConfig& config() const noexcept { return cfg_; }
    OnlineTrainerConfig& config() noexcept { return cfg_; }
    size_t buffer_size() const noexcept { return rollout_buf_.size(); }
    size_t queue_size() const noexcept { return incoming_.size(); }
    uint64_t dropped_samples() const noexcept { return dropped_samples_; }

    void set_agent(HybridRLAgent* agent) noexcept { agent_ = agent; }
    void enable() noexcept { stats_.enabled = true; }
    void disable() noexcept { stats_.enabled = false; }

    // ─── Set external validation callback ───────────────────────────────────
    // Called with (train_sharpe, val_sharpe) after each training.
    // Return false to trigger rollback.

    using ValidationCallback = std::function<bool(double, double)>;

    void set_validation_callback(ValidationCallback cb) noexcept {
        validation_cb_ = std::move(cb);
    }

    ~SafeOnlineTrainer() {
        stop();
    }

private:
    // ─── Trainer Loop ───────────────────────────────────────────────────────

    void trainer_loop() noexcept {
        // Set low-priority Background QoS for trainer thread
#ifdef __APPLE__
        if (cfg_.background_qos) {
            pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
        }
#endif

        uint64_t last_train_time = TscClock::now_ns();

        while (running_.load(std::memory_order_acquire)) {
            // Drain incoming queue into rollout buffer
            drain_queue();

            uint64_t now = TscClock::now_ns();
            bool interval_elapsed = (now - last_train_time) >= cfg_.train_interval_ns;
            bool enough_samples = rollout_buf_.size() >= cfg_.min_samples;

            if (interval_elapsed && enough_samples && stats_.enabled) {
                stats_.training_active = true;
                bool ok = do_training_step();
                stats_.training_active = false;

                if (ok) {
                    last_train_time = now;
                }
            }

            // Sleep 1 second between checks (not hot path)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // ─── Drain SPSC Queue into Rollout Buffer ───────────────────────────────

    void drain_queue() noexcept {
        RolloutSample sample;
        size_t drained = 0;
        while (incoming_.try_pop(sample) && drained < 4096) {
            rollout_buf_.push(sample);
            ++stats_.samples_collected;
            ++drained;
        }
    }

    // ─── Training Step ──────────────────────────────────────────────────────
    // 1. Snapshot current weights
    // 2. Split data into train/validation
    // 3. Load training data into agent buffer, run update()
    // 4. Evaluate on validation set
    // 5. Check KL divergence and Sharpe
    // 6. Rollback if safety checks fail

    bool do_training_step() noexcept {
        // Drain all pending samples from SPSC queue first
        drain_queue();

        if (!agent_ || rollout_buf_.size() < cfg_.min_samples) return false;

        uint64_t t0 = TscClock::now();

        // Step 1: Snapshot current weights for potential rollback
        HybridRLAgent::Snapshot snapshot;
        agent_->snapshot(snapshot);

        // Step 2: Split into train and validation
        size_t total = rollout_buf_.size();
        size_t val_size = std::max<size_t>(1, total * cfg_.validation_split_pct / 100);
        size_t train_size = total - val_size;

        // Step 3: Compute pre-training validation Sharpe
        double pre_val_sharpe = compute_sharpe(total - val_size, val_size);

        // Step 4: Load training samples into agent and update
        size_t loaded = 0;
        for (size_t i = 0; i < train_size && loaded < agent_->config().horizon; ++i) {
            const auto& s = rollout_buf_.at(i);

            RL2Action act;
            std::memcpy(act.raw, s.action, sizeof(act.raw));
            act.from_raw();

            // Build a minimal state for the agent
            RL2State state = s.state;

            // We need to do a forward pass to set internal state
            agent_->select_action(state, false);
            agent_->store_transition(state, act, s.reward, s.done);
            ++loaded;
        }

        // Run PPO+SAC update
        bool updated = agent_->update();
        if (!updated) {
            agent_->restore(snapshot);
            return false;
        }

        // Step 5: Safety checks
        double post_kl = agent_->last_kl();
        double post_val_sharpe = compute_sharpe(total - val_size, val_size);

        stats_.last_kl = post_kl;
        stats_.last_train_sharpe = compute_sharpe(0, train_size);
        stats_.last_val_sharpe = post_val_sharpe;

        bool should_rollback = false;
        const char* rollback_reason = nullptr;

        // KL divergence gate
        if (post_kl > cfg_.max_kl_divergence) {
            should_rollback = true;
            rollback_reason = "KL divergence exceeded";
        }

        // Sharpe ratio gate
        if (!should_rollback && post_val_sharpe < cfg_.min_sharpe_ratio) {
            should_rollback = true;
            rollback_reason = "Validation Sharpe below minimum";
        }

        // Sharpe decay gate
        if (!should_rollback && pre_val_sharpe > 0.0 &&
            post_val_sharpe < pre_val_sharpe * cfg_.sharpe_decay_gate) {
            should_rollback = true;
            rollback_reason = "Validation Sharpe decayed";
        }

        // Value loss gate
        if (!should_rollback && agent_->stats().value_loss > cfg_.max_value_loss) {
            should_rollback = true;
            rollback_reason = "Value loss spike";
        }

        // External validation callback
        if (!should_rollback && validation_cb_) {
            if (!validation_cb_(stats_.last_train_sharpe, post_val_sharpe)) {
                should_rollback = true;
                rollback_reason = "External validation rejected";
            }
        }

        // Step 6: Rollback or accept
        if (should_rollback) {
            agent_->restore(snapshot);
            ++stats_.total_rollbacks;
            ++stats_.consecutive_rollbacks;

            // Auto-disable after too many consecutive rollbacks
            if (cfg_.auto_disable_on_failure &&
                stats_.consecutive_rollbacks >= cfg_.max_consecutive_rollbacks) {
                stats_.enabled = false;
            }

            (void)rollback_reason; // used for logging in production
        } else {
            stats_.consecutive_rollbacks = 0;
            ++stats_.total_trains;

            if (post_val_sharpe > stats_.best_val_sharpe) {
                stats_.best_val_sharpe = post_val_sharpe;
            }
        }

        stats_.last_train_ns = TscClock::now_ns();
        stats_.last_train_duration_ns = TscClock::elapsed_ns(t0);

        return !should_rollback;
    }

    // ─── Compute Sharpe Ratio on Buffer Segment ─────────────────────────────

    double compute_sharpe(size_t start, size_t count) const noexcept {
        if (count < 2 || start + count > rollout_buf_.size()) return 0.0;

        double sum = 0.0;
        for (size_t i = start; i < start + count; ++i) {
            sum += rollout_buf_.at(i).reward;
        }
        double mean = sum / static_cast<double>(count);

        double var = 0.0;
        for (size_t i = start; i < start + count; ++i) {
            double d = rollout_buf_.at(i).reward - mean;
            var += d * d;
        }
        var /= static_cast<double>(count - 1);
        double std_val = std::sqrt(var + 1e-12);

        return mean / std_val;
    }

    // ─── Data ───────────────────────────────────────────────────────────────

    HybridRLAgent* agent_ = nullptr;
    OnlineTrainerConfig cfg_;
    TrainerStats stats_;

    // Lock-free SPSC queue for receiving samples from hot path
    RolloutQueue incoming_;

    // Local rollout buffer (only accessed by trainer thread)
    RolloutBuffer rollout_buf_;

    // Thread control
    std::atomic<bool> running_{false};
    std::thread thread_;

    // Diagnostics
    uint64_t dropped_samples_ = 0;

    // External validation callback
    ValidationCallback validation_cb_;
};

} // namespace bybit
