#pragma once

#include "../config/types.h"
#include "../utils/clock.h"
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>
#include <numeric>
#include <fstream>
#include <string>
#include <cstdint>

namespace bybit {

// ─── RL State / Action / Experience ─────────────────────────────────────────

static constexpr size_t RL_STATE_DIM  = 10;
static constexpr size_t RL_ACTION_DIM = 4;

struct RLState {
    double volatility         = 0.0;
    double spread_bps         = 0.0;
    double liquidity_depth    = 0.0;
    double model_confidence   = 0.0;
    double recent_pnl         = 0.0;
    double drawdown           = 0.0;
    double win_rate           = 0.0;
    double sharpe             = 0.0;
    double fill_rate          = 0.0;
    double regime_stability   = 0.0;

    std::array<double, RL_STATE_DIM> as_array() const noexcept {
        return {volatility, spread_bps, liquidity_depth, model_confidence,
                recent_pnl, drawdown, win_rate, sharpe, fill_rate, regime_stability};
    }
};

struct RLAction {
    double signal_threshold_delta  = 0.0;  // [-0.1, +0.1]
    double position_size_scale     = 1.0;  // [0.2, 2.0]
    double order_offset_bps        = 0.0;  // [-2.0, +2.0]
    double requote_freq_scale      = 1.0;  // [0.5, 2.0]

    std::array<double, RL_ACTION_DIM> as_array() const noexcept {
        return {signal_threshold_delta, position_size_scale,
                order_offset_bps, requote_freq_scale};
    }

    void from_array(const std::array<double, RL_ACTION_DIM>& a) noexcept {
        signal_threshold_delta = std::clamp(a[0], -0.1, 0.1);
        position_size_scale    = std::clamp(a[1], 0.2, 2.0);
        order_offset_bps       = std::clamp(a[2], -2.0, 2.0);
        requote_freq_scale     = std::clamp(a[3], 0.5, 2.0);
    }
};

struct RLExperience {
    RLState  state;
    RLAction action;
    double   reward    = 0.0;
    RLState  next_state;
    bool     done      = false;
};

// ─── Simple Neural Network (2-layer MLP) ────────────────────────────────────
// Lightweight inline network for policy/value — no external deps.

class MLP {
public:
    static constexpr size_t HIDDEN = 32;

    MLP(size_t input_dim, size_t output_dim) noexcept
        : in_dim_(input_dim), out_dim_(output_dim) {
        w1_.resize(input_dim * HIDDEN, 0.0);
        b1_.resize(HIDDEN, 0.0);
        w2_.resize(HIDDEN * output_dim, 0.0);
        b2_.resize(output_dim, 0.0);
        init_weights();
    }

    std::vector<double> forward(const std::array<double, RL_STATE_DIM>& input) const {
        // Layer 1: tanh activation
        std::vector<double> h(HIDDEN, 0.0);
        for (size_t j = 0; j < HIDDEN; ++j) {
            double sum = b1_[j];
            for (size_t i = 0; i < in_dim_; ++i) {
                sum += input[i] * w1_[i * HIDDEN + j];
            }
            h[j] = std::tanh(sum);
        }

        // Layer 2: linear output
        std::vector<double> out(out_dim_, 0.0);
        for (size_t j = 0; j < out_dim_; ++j) {
            double sum = b2_[j];
            for (size_t i = 0; i < HIDDEN; ++i) {
                sum += h[i] * w2_[i * out_dim_ + j];
            }
            out[j] = sum;
        }
        return out;
    }

    // Simple SGD update
    void update(const std::vector<double>& grad_w1, const std::vector<double>& grad_b1,
                const std::vector<double>& grad_w2, const std::vector<double>& grad_b2,
                double lr) {
        for (size_t i = 0; i < w1_.size(); ++i) w1_[i] -= lr * grad_w1[i];
        for (size_t i = 0; i < b1_.size(); ++i) b1_[i] -= lr * grad_b1[i];
        for (size_t i = 0; i < w2_.size(); ++i) w2_[i] -= lr * grad_w2[i];
        for (size_t i = 0; i < b2_.size(); ++i) b2_[i] -= lr * grad_b2[i];
    }

    std::vector<double>& w1() { return w1_; }
    std::vector<double>& b1() { return b1_; }
    std::vector<double>& w2() { return w2_; }
    std::vector<double>& b2() { return b2_; }
    const std::vector<double>& w1() const { return w1_; }
    const std::vector<double>& b1() const { return b1_; }
    const std::vector<double>& w2() const { return w2_; }
    const std::vector<double>& b2() const { return b2_; }

private:
    void init_weights() {
        std::mt19937 rng(42);
        double scale1 = std::sqrt(2.0 / in_dim_);
        double scale2 = std::sqrt(2.0 / HIDDEN);
        std::normal_distribution<double> dist1(0.0, scale1);
        std::normal_distribution<double> dist2(0.0, scale2);
        for (auto& w : w1_) w = dist1(rng);
        for (auto& w : w2_) w = dist2(rng);
    }

    size_t in_dim_, out_dim_;
    std::vector<double> w1_, b1_, w2_, b2_;
};

// ─── PPO-based RL Optimizer ─────────────────────────────────────────────────
// Optimizes trading strategy parameters using Proximal Policy Optimization.
//
// State: market conditions + recent performance
// Action: parameter adjustments (threshold, size, offset, requote)
// Reward: risk-adjusted PnL (Sharpe-like)

struct RLOptimizerConfig {
    double learning_rate    = 0.0003;
    double gamma            = 0.99;     // discount factor
    double lambda           = 0.95;     // GAE lambda
    double clip_epsilon     = 0.2;      // PPO clip range
    double entropy_coeff    = 0.01;     // entropy bonus
    double value_coeff      = 0.5;      // value loss coefficient
    int    batch_size       = 64;
    int    update_epochs    = 4;
    double exploration_std  = 0.1;      // Gaussian exploration noise
    int    min_experiences  = 128;      // minimum before first update
    bool   online_learning  = true;
};

struct RLOptimizerSnapshot {
    RLAction current_action;
    double   avg_reward       = 0.0;
    double   value_estimate   = 0.0;
    double   policy_loss      = 0.0;
    double   value_loss       = 0.0;
    int      total_steps      = 0;
    int      total_updates    = 0;
    bool     exploring        = true;
    uint64_t last_update_ns   = 0;
};

class RLOptimizer {
public:
    explicit RLOptimizer(const RLOptimizerConfig& cfg = {}) noexcept
        : cfg_(cfg)
        , policy_(RL_STATE_DIM, RL_ACTION_DIM)
        , value_(RL_STATE_DIM, 1)
        , rng_(std::random_device{}())
    {}

    // Get action for current state (with exploration noise)
    RLAction act(const RLState& state) noexcept {
        auto state_arr = state.as_array();
        auto raw_action = policy_.forward(state_arr);

        RLAction action;
        std::array<double, RL_ACTION_DIM> a;
        std::normal_distribution<double> noise(0.0, cfg_.exploration_std);

        for (size_t i = 0; i < RL_ACTION_DIM; ++i) {
            a[i] = raw_action[i];
            if (snapshot_.exploring) {
                a[i] += noise(rng_);
            }
        }
        action.from_array(a);

        // Value estimate
        auto v = value_.forward(state_arr);
        snapshot_.value_estimate = v[0];
        snapshot_.current_action = action;

        return action;
    }

    // Record experience
    void step(const RLState& state, const RLAction& action,
              double reward, const RLState& next_state, bool done = false) {
        RLExperience exp;
        exp.state = state;
        exp.action = action;
        exp.reward = reward;
        exp.next_state = next_state;
        exp.done = done;

        replay_buffer_.push_back(exp);
        if (replay_buffer_.size() > MAX_BUFFER_SIZE) {
            replay_buffer_.erase(replay_buffer_.begin());
        }

        snapshot_.total_steps++;

        // Accumulate reward for logging
        reward_sum_ += reward;
        reward_count_++;
        snapshot_.avg_reward = reward_sum_ / reward_count_;

        // Auto-update if online learning enabled
        if (cfg_.online_learning &&
            replay_buffer_.size() >= static_cast<size_t>(cfg_.min_experiences) &&
            snapshot_.total_steps % cfg_.batch_size == 0) {
            update();
        }
    }

    // Compute reward from trading metrics (risk-adjusted PnL)
    static double compute_reward(double pnl_change, double drawdown,
                                  double sharpe, double win_rate) noexcept {
        // Primary: PnL change (normalized)
        double reward = pnl_change * 10.0;

        // Penalty for drawdown
        reward -= drawdown * 5.0;

        // Bonus for good Sharpe
        if (sharpe > 1.0) reward += 0.1;

        // Bonus for high win rate
        if (win_rate > 0.55) reward += 0.05;

        return std::clamp(reward, -10.0, 10.0);
    }

    // Manual update trigger
    void update() {
        if (replay_buffer_.size() < static_cast<size_t>(cfg_.batch_size)) return;

        // Compute advantages using GAE
        compute_advantages();

        // PPO update for multiple epochs
        for (int epoch = 0; epoch < cfg_.update_epochs; ++epoch) {
            ppo_step();
        }

        snapshot_.total_updates++;
        snapshot_.last_update_ns = Clock::now_ns();

        // Decay exploration
        if (cfg_.exploration_std > 0.01) {
            cfg_.exploration_std *= 0.999;
        }
        snapshot_.exploring = cfg_.exploration_std > 0.02;
    }

    // Apply current RL action to strategy parameters
    void apply_to_config(AppConfig& config) const noexcept {
        const auto& a = snapshot_.current_action;

        // Adjust signal threshold
        config.signal_threshold = std::clamp(
            config.signal_threshold + a.signal_threshold_delta,
            0.35, 0.90);

        // Scale position sizing
        config.base_order_qty = std::clamp(
            config.base_order_qty * a.position_size_scale,
            config.min_order_qty, config.max_order_qty);

        // Adjust entry offset
        config.entry_offset_bps = std::clamp(
            config.entry_offset_bps + a.order_offset_bps,
            0.1, 10.0);

        // Adjust requote frequency
        config.requote_interval_ms = std::clamp(
            static_cast<int>(config.requote_interval_ms * a.requote_freq_scale),
            20, 500);
    }

    // E4: Save policy checkpoint to disk for restart persistence.
    // Format: policy weights, value weights, config, snapshot state.
    bool save_checkpoint(const std::string& path) const noexcept {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        // Magic + version
        uint32_t magic = 0x524C4350; // "RLCP"
        uint32_t version = 1;
        file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));

        // Policy network weights
        auto write_vec = [&](const std::vector<double>& v) {
            size_t sz = v.size();
            file.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
            file.write(reinterpret_cast<const char*>(v.data()), sz * sizeof(double));
        };
        write_vec(policy_.w1()); write_vec(policy_.b1());
        write_vec(policy_.w2()); write_vec(policy_.b2());
        write_vec(value_.w1());  write_vec(value_.b1());
        write_vec(value_.w2());  write_vec(value_.b2());

        // Snapshot state for continuity
        file.write(reinterpret_cast<const char*>(&snapshot_), sizeof(snapshot_));
        file.write(reinterpret_cast<const char*>(&cfg_.exploration_std), sizeof(double));
        file.write(reinterpret_cast<const char*>(&reward_sum_), sizeof(double));
        file.write(reinterpret_cast<const char*>(&reward_count_), sizeof(int));

        return file.good();
    }

    // E4: Load policy checkpoint from disk. Returns true on success.
    bool load_checkpoint(const std::string& path) noexcept {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t magic = 0, version = 0;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (magic != 0x524C4350 || version != 1) return false;

        auto read_vec = [&](std::vector<double>& v) {
            size_t sz = 0;
            file.read(reinterpret_cast<char*>(&sz), sizeof(sz));
            if (sz != v.size()) { file.setstate(std::ios::failbit); return; }
            file.read(reinterpret_cast<char*>(v.data()), sz * sizeof(double));
        };
        read_vec(policy_.w1()); read_vec(policy_.b1());
        read_vec(policy_.w2()); read_vec(policy_.b2());
        read_vec(value_.w1());  read_vec(value_.b1());
        read_vec(value_.w2());  read_vec(value_.b2());

        file.read(reinterpret_cast<char*>(&snapshot_), sizeof(snapshot_));
        file.read(reinterpret_cast<char*>(&cfg_.exploration_std), sizeof(double));
        file.read(reinterpret_cast<char*>(&reward_sum_), sizeof(double));
        file.read(reinterpret_cast<char*>(&reward_count_), sizeof(int));

        checkpoint_loaded_ = file.good();
        return checkpoint_loaded_;
    }

    bool checkpoint_loaded() const noexcept { return checkpoint_loaded_; }

    const RLOptimizerSnapshot& snapshot() const noexcept { return snapshot_; }
    size_t buffer_size() const noexcept { return replay_buffer_.size(); }

private:
    static constexpr size_t MAX_BUFFER_SIZE = 10000;

    void compute_advantages() {
        size_t n = replay_buffer_.size();
        advantages_.resize(n, 0.0);
        returns_.resize(n, 0.0);

        double last_value = 0.0;
        double last_gae = 0.0;

        for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
            auto& exp = replay_buffer_[i];
            auto next_v = value_.forward(exp.next_state.as_array());
            double next_value = exp.done ? 0.0 : next_v[0];

            double delta = exp.reward + cfg_.gamma * next_value - last_value;
            last_gae = delta + cfg_.gamma * cfg_.lambda * last_gae;

            advantages_[i] = last_gae;
            returns_[i] = last_gae + last_value;
            last_value = next_value;
        }

        // Normalize advantages
        double mean = 0.0, var = 0.0;
        for (auto a : advantages_) mean += a;
        mean /= advantages_.size();
        for (auto a : advantages_) var += (a - mean) * (a - mean);
        var /= advantages_.size();
        double std_dev = std::sqrt(var + 1e-8);
        for (auto& a : advantages_) a = (a - mean) / std_dev;
    }

    void ppo_step() {
        size_t n = std::min(replay_buffer_.size(),
                            static_cast<size_t>(cfg_.batch_size));

        double total_policy_loss = 0.0;
        double total_value_loss = 0.0;

        // Simplified gradient accumulation
        auto grad_pw1 = std::vector<double>(policy_.w1().size(), 0.0);
        auto grad_pb1 = std::vector<double>(policy_.b1().size(), 0.0);
        auto grad_pw2 = std::vector<double>(policy_.w2().size(), 0.0);
        auto grad_pb2 = std::vector<double>(policy_.b2().size(), 0.0);

        auto grad_vw1 = std::vector<double>(value_.w1().size(), 0.0);
        auto grad_vb1 = std::vector<double>(value_.b1().size(), 0.0);
        auto grad_vw2 = std::vector<double>(value_.w2().size(), 0.0);
        auto grad_vb2 = std::vector<double>(value_.b2().size(), 0.0);

        // Sample random indices
        std::vector<size_t> indices(replay_buffer_.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::shuffle(indices.begin(), indices.end(), rng_);

        for (size_t batch_i = 0; batch_i < n; ++batch_i) {
            size_t idx = indices[batch_i];
            auto& exp = replay_buffer_[idx];
            auto state_arr = exp.state.as_array();

            // Policy forward pass
            auto action_pred = policy_.forward(state_arr);
            auto value_pred = value_.forward(state_arr);

            double advantage = advantages_[idx];
            double target_return = returns_[idx];

            // Value loss
            double v_err = value_pred[0] - target_return;
            total_value_loss += v_err * v_err;

            // Policy gradient (simplified: perturb output weights by advantage)
            for (size_t j = 0; j < RL_ACTION_DIM; ++j) {
                double action_err = exp.action.as_array()[j] - action_pred[j];
                double pg = -advantage * action_err;
                pg = std::clamp(pg, -cfg_.clip_epsilon, cfg_.clip_epsilon);

                // Accumulate into output layer gradient
                for (size_t h = 0; h < MLP::HIDDEN; ++h) {
                    grad_pw2[h * RL_ACTION_DIM + j] += pg / static_cast<double>(n);
                }
                grad_pb2[j] += pg / static_cast<double>(n);
            }

            // Value gradient
            for (size_t h = 0; h < MLP::HIDDEN; ++h) {
                grad_vw2[h] += cfg_.value_coeff * v_err / static_cast<double>(n);
            }
            grad_vb2[0] += cfg_.value_coeff * v_err / static_cast<double>(n);

            total_policy_loss += std::abs(advantage);
        }

        // Apply gradients
        policy_.update(grad_pw1, grad_pb1, grad_pw2, grad_pb2, cfg_.learning_rate);
        value_.update(grad_vw1, grad_vb1, grad_vw2, grad_vb2, cfg_.learning_rate);

        snapshot_.policy_loss = total_policy_loss / n;
        snapshot_.value_loss = total_value_loss / n;
    }

    RLOptimizerConfig cfg_;
    MLP policy_;
    MLP value_;
    std::mt19937 rng_;

    std::vector<RLExperience> replay_buffer_;
    std::vector<double> advantages_;
    std::vector<double> returns_;

    RLOptimizerSnapshot snapshot_;
    double reward_sum_ = 0.0;
    int reward_count_ = 0;
    bool checkpoint_loaded_ = false;  // E4: true if weights restored from checkpoint
};

} // namespace bybit
