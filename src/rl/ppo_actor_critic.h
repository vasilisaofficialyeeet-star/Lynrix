#pragma once

// ─── PPO Actor-Critic RL Engine ─────────────────────────────────────────────
// Full Proximal Policy Optimization in C++ for real-time risk/execution tuning.
// No Python dependency — runs inline in the trading loop.
//
// Actor: adjusts {signal_threshold, position_size_scale, order_offset_bps, requote_freq}
// Critic: estimates value of current market state for advantage computation
//
// State vector (16 dims):
//   - volatility, trend_strength, imbalance, spread_bps
//   - position_size_pct, unrealized_pnl_pct, drawdown_pct
//   - recent_win_rate, recent_sharpe, fill_rate
//   - regime (one-hot x5), time_of_day_sin
//
// Action vector (4 dims, continuous):
//   - signal_threshold_delta [-0.1, +0.1]
//   - position_size_scale [0.5, 2.0]
//   - order_offset_bps [-5.0, +5.0]
//   - requote_freq_scale [0.5, 3.0]
//
// Training: online mini-batch PPO with GAE-λ advantage estimation.
// Update frequency: every 64 steps (configurable).

#include "../config/types.h"
#include "../feature_engine/simd_indicators.h"
#include "../utils/clock.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <random>

namespace bybit {

// ─── RL Hyperparameters ─────────────────────────────────────────────────────

struct PPOConfig {
    double gamma           = 0.99;    // Discount factor
    double lambda_gae      = 0.95;    // GAE lambda
    double clip_epsilon    = 0.2;     // PPO clip ratio
    double lr_actor        = 3e-4;    // Actor learning rate
    double lr_critic       = 1e-3;    // Critic learning rate
    double entropy_coeff   = 0.01;    // Entropy bonus coefficient
    double max_grad_norm   = 0.5;     // Gradient clipping
    size_t batch_size      = 64;      // Mini-batch size
    size_t update_epochs   = 4;       // PPO update epochs per batch
    size_t horizon         = 256;     // Rollout horizon before update
    double action_std_init = 0.5;     // Initial action standard deviation
    double action_std_min  = 0.05;    // Minimum action std (exploration floor)
    double action_std_decay = 0.9995; // Per-step std decay
};

// ─── Dimensions ─────────────────────────────────────────────────────────────

inline constexpr size_t RL_STATE_DIM  = 16;
inline constexpr size_t RL_ACTION_DIM = 4;
inline constexpr size_t RL_HIDDEN_DIM = 64;

// ─── Action Bounds ──────────────────────────────────────────────────────────

struct ActionBounds {
    double lo[RL_ACTION_DIM] = {-0.1, 0.5, -5.0, 0.5};
    double hi[RL_ACTION_DIM] = { 0.1, 2.0,  5.0, 3.0};
};

// ─── RL State ───────────────────────────────────────────────────────────────

struct RLState {
    alignas(64) double features[RL_STATE_DIM]{};
};

// ─── RL Action ──────────────────────────────────────────────────────────────

struct RLAction {
    double signal_threshold_delta = 0.0;
    double position_size_scale    = 1.0;
    double order_offset_bps       = 0.0;
    double requote_freq_scale     = 1.0;

    double as_array[RL_ACTION_DIM]{};

    void to_array() noexcept {
        as_array[0] = signal_threshold_delta;
        as_array[1] = position_size_scale;
        as_array[2] = order_offset_bps;
        as_array[3] = requote_freq_scale;
    }

    void from_array() noexcept {
        signal_threshold_delta = as_array[0];
        position_size_scale    = as_array[1];
        order_offset_bps       = as_array[2];
        requote_freq_scale     = as_array[3];
    }
};

// ─── Transition (experience buffer entry) ───────────────────────────────────

struct Transition {
    RLState  state;
    double   action[RL_ACTION_DIM]{};
    double   log_prob = 0.0;
    double   reward   = 0.0;
    double   value    = 0.0;
    bool     done     = false;
};

// ─── Simple 2-Layer MLP ─────────────────────────────────────────────────────
// Shared architecture for actor mean and critic value networks.
// input -> hidden (tanh) -> output (linear)

template <size_t InDim, size_t HidDim, size_t OutDim>
struct MLP {
    alignas(64) double W1[HidDim][InDim]{};
    alignas(64) double b1[HidDim]{};
    alignas(64) double W2[OutDim][HidDim]{};
    alignas(64) double b2[OutDim]{};

    // Intermediate buffer for backprop
    alignas(64) double hidden[HidDim]{};
    alignas(64) double pre_act[HidDim]{}; // pre-activation for gradient

    void forward(const double* input, double* output) noexcept {
        // Layer 1: hidden = tanh(W1 * input + b1)
        simd::matvec(&W1[0][0], input, pre_act, HidDim, InDim);
        for (size_t i = 0; i < HidDim; ++i) {
            pre_act[i] += b1[i];
            hidden[i] = std::tanh(pre_act[i]);
        }
        // Layer 2: output = W2 * hidden + b2
        simd::matvec(&W2[0][0], hidden, output, OutDim, HidDim);
        for (size_t i = 0; i < OutDim; ++i) {
            output[i] += b2[i];
        }
    }

    // Xavier initialization
    void init_weights(uint64_t seed) noexcept {
        auto fill = [](double* data, size_t size, size_t fan_in, size_t fan_out,
                       uint64_t& s) {
            double scale = std::sqrt(2.0 / (fan_in + fan_out));
            for (size_t i = 0; i < size; ++i) {
                s = s * 6364136223846793005ULL + 1442695040888963407ULL;
                double u = static_cast<double>(s >> 33) / static_cast<double>(1ULL << 31) - 1.0;
                data[i] = u * scale;
            }
        };
        fill(&W1[0][0], HidDim * InDim, InDim, HidDim, seed);
        fill(&W2[0][0], OutDim * HidDim, HidDim, OutDim, seed);
        std::memset(b1, 0, sizeof(b1));
        std::memset(b2, 0, sizeof(b2));
    }
};

// ─── PPO Actor-Critic Agent ─────────────────────────────────────────────────

class PPOAgent {
public:
    using ActorNet  = MLP<RL_STATE_DIM, RL_HIDDEN_DIM, RL_ACTION_DIM>;
    using CriticNet = MLP<RL_STATE_DIM, RL_HIDDEN_DIM, 1>;

    PPOAgent() noexcept {
        actor_.init_weights(42);
        critic_.init_weights(137);
        action_log_std_.fill(std::log(cfg_.action_std_init));
    }

    explicit PPOAgent(const PPOConfig& cfg) noexcept : cfg_(cfg) {
        actor_.init_weights(42);
        critic_.init_weights(137);
        action_log_std_.fill(std::log(cfg_.action_std_init));
    }

    // ─── Select Action (inference) ──────────────────────────────────────────
    // Returns action sampled from Gaussian policy π(a|s) = N(μ(s), σ²)

    RLAction select_action(const RLState& state, bool explore = true) noexcept {
        // Forward pass through actor network -> action mean
        alignas(64) double action_mean[RL_ACTION_DIM];
        actor_.forward(state.features, action_mean);

        // Compute value estimate
        alignas(64) double value_out[1];
        critic_.forward(state.features, value_out);
        last_value_ = value_out[0];

        RLAction action;
        double log_prob = 0.0;

        if (explore && exploring_) {
            // Sample from N(mean, std²)
            for (size_t i = 0; i < RL_ACTION_DIM; ++i) {
                double std = std::exp(action_log_std_[i]);
                std::normal_distribution<double> dist(action_mean[i], std);
                action.as_array[i] = dist(rng_);

                // Log probability: log N(a|μ,σ) = -0.5*((a-μ)/σ)² - log(σ) - 0.5*log(2π)
                double diff = action.as_array[i] - action_mean[i];
                log_prob += -0.5 * (diff / std) * (diff / std)
                            - action_log_std_[i] - 0.9189385332;
            }
        } else {
            // Deterministic: use mean
            std::memcpy(action.as_array, action_mean, sizeof(action_mean));
            log_prob = 0.0;
        }

        // Clamp actions to bounds
        for (size_t i = 0; i < RL_ACTION_DIM; ++i) {
            action.as_array[i] = std::clamp(action.as_array[i],
                                             bounds_.lo[i], bounds_.hi[i]);
        }

        action.from_array();
        last_log_prob_ = log_prob;

        return action;
    }

    // ─── Store Transition ───────────────────────────────────────────────────

    void store_transition(const RLState& state, const RLAction& action,
                          double reward, bool done) noexcept {
        if (buffer_pos_ >= cfg_.horizon) return; // buffer full, need update

        auto& t = buffer_[buffer_pos_];
        t.state = state;
        std::memcpy(t.action, action.as_array, sizeof(t.action));
        t.log_prob = last_log_prob_;
        t.reward = reward;
        t.value = last_value_;
        t.done = done;

        ++buffer_pos_;
        ++total_steps_;

        // Decay exploration
        for (size_t i = 0; i < RL_ACTION_DIM; ++i) {
            double new_std = std::exp(action_log_std_[i]) * cfg_.action_std_decay;
            action_log_std_[i] = std::log(std::max(new_std, cfg_.action_std_min));
        }
    }

    // ─── PPO Update ─────────────────────────────────────────────────────────
    // Call when buffer is full. Performs GAE advantage estimation + PPO clipped update.
    // Returns true if update was performed.

    bool update() noexcept {
        if (buffer_pos_ < cfg_.batch_size) return false;

        // Step 1: Compute GAE advantages and returns
        compute_gae();

        // Step 2: PPO clipped update for multiple epochs
        for (size_t epoch = 0; epoch < cfg_.update_epochs; ++epoch) {
            double total_policy_loss = 0.0;
            double total_value_loss = 0.0;
            double total_entropy = 0.0;

            for (size_t i = 0; i < buffer_pos_; ++i) {
                auto& t = buffer_[i];

                // Re-evaluate action under current policy
                alignas(64) double action_mean[RL_ACTION_DIM];
                actor_.forward(t.state.features, action_mean);

                alignas(64) double value_out[1];
                critic_.forward(t.state.features, value_out);

                // Compute new log probability
                double new_log_prob = 0.0;
                double entropy = 0.0;
                for (size_t j = 0; j < RL_ACTION_DIM; ++j) {
                    double std = std::exp(action_log_std_[j]);
                    double diff = t.action[j] - action_mean[j];
                    new_log_prob += -0.5 * (diff / std) * (diff / std)
                                   - action_log_std_[j] - 0.9189385332;
                    entropy += action_log_std_[j] + 0.5 * (1.0 + std::log(2.0 * M_PI));
                }

                // PPO ratio: r(θ) = exp(log π_new - log π_old)
                double ratio = std::exp(new_log_prob - t.log_prob);
                ratio = std::clamp(ratio, 0.01, 100.0); // safety clamp

                // Clipped surrogate objective
                double surr1 = ratio * advantages_[i];
                double surr2 = std::clamp(ratio,
                    1.0 - cfg_.clip_epsilon, 1.0 + cfg_.clip_epsilon) * advantages_[i];
                double policy_loss = -std::min(surr1, surr2);

                // Value loss: MSE
                double value_loss = 0.5 * (returns_[i] - value_out[0]) *
                                          (returns_[i] - value_out[0]);

                total_policy_loss += policy_loss;
                total_value_loss += value_loss;
                total_entropy += entropy;

                // SGD update — simplified gradient (numerical approximation)
                // In production, use proper automatic differentiation
                double total_loss = policy_loss + 0.5 * value_loss
                                  - cfg_.entropy_coeff * entropy;

                // Update actor: gradient ≈ -advantage * ∇log π
                update_actor_grads(t.state, t.action, action_mean, advantages_[i], ratio);

                // Update critic: gradient ≈ (V(s) - R) * ∇V
                update_critic_grads(t.state, value_out[0], returns_[i]);
            }

            last_policy_loss_ = total_policy_loss / buffer_pos_;
            last_value_loss_ = total_value_loss / buffer_pos_;
        }

        // Reset buffer
        buffer_pos_ = 0;
        ++total_updates_;
        return true;
    }

    // ─── Check if Update Needed ─────────────────────────────────────────────

    bool should_update() const noexcept {
        return buffer_pos_ >= cfg_.horizon;
    }

    // ─── Compute Reward ─────────────────────────────────────────────────────
    // Reward function for the RL agent. Call after each trading step.

    static double compute_reward(double pnl_change, double drawdown_pct,
                                 double fill_rate, double latency_us) noexcept {
        // Multi-objective reward:
        // + PnL change (primary signal)
        // - Drawdown penalty (risk control)
        // + Fill rate bonus (execution quality)
        // - Latency penalty (speed)
        double reward = 0.0;

        // PnL component (scaled to typical BTC tick values)
        reward += pnl_change * 10.0;

        // Drawdown penalty: exponential for large drawdowns
        if (drawdown_pct > 0.02) {
            reward -= std::pow(drawdown_pct * 100.0, 2) * 0.1;
        }

        // Fill rate bonus
        reward += fill_rate * 0.5;

        // Latency penalty (penalize > 200 µs)
        if (latency_us > 200.0) {
            reward -= (latency_us - 200.0) * 0.001;
        }

        return std::clamp(reward, -10.0, 10.0);
    }

    // ─── Accessors ──────────────────────────────────────────────────────────

    uint64_t total_steps() const noexcept { return total_steps_; }
    uint64_t total_updates() const noexcept { return total_updates_; }
    double last_value() const noexcept { return last_value_; }
    double last_policy_loss() const noexcept { return last_policy_loss_; }
    double last_value_loss() const noexcept { return last_value_loss_; }
    bool exploring() const noexcept { return exploring_; }
    void set_exploring(bool e) noexcept { exploring_ = e; }

    double avg_reward() const noexcept {
        if (buffer_pos_ == 0) return 0.0;
        double sum = 0.0;
        for (size_t i = 0; i < buffer_pos_; ++i) sum += buffer_[i].reward;
        return sum / buffer_pos_;
    }

    PPOConfig& config() noexcept { return cfg_; }
    const PPOConfig& config() const noexcept { return cfg_; }

private:
    // ─── GAE Advantage Estimation ───────────────────────────────────────────

    void compute_gae() noexcept {
        // Bootstrap value for last state
        double next_value = 0.0;
        if (buffer_pos_ > 0 && !buffer_[buffer_pos_ - 1].done) {
            alignas(64) double v[1];
            critic_.forward(buffer_[buffer_pos_ - 1].state.features, v);
            next_value = v[0];
        }

        double gae = 0.0;
        for (int i = static_cast<int>(buffer_pos_) - 1; i >= 0; --i) {
            double mask = buffer_[i].done ? 0.0 : 1.0;
            double nv = (i + 1 < static_cast<int>(buffer_pos_))
                        ? buffer_[i + 1].value : next_value;
            double delta = buffer_[i].reward + cfg_.gamma * nv * mask - buffer_[i].value;
            gae = delta + cfg_.gamma * cfg_.lambda_gae * mask * gae;
            advantages_[i] = gae;
            returns_[i] = gae + buffer_[i].value;
        }

        // Normalize advantages
        double mean = 0.0, var = 0.0;
        for (size_t i = 0; i < buffer_pos_; ++i) mean += advantages_[i];
        mean /= buffer_pos_;
        for (size_t i = 0; i < buffer_pos_; ++i) {
            double d = advantages_[i] - mean;
            var += d * d;
        }
        var /= buffer_pos_;
        double std = std::sqrt(var + 1e-8);
        for (size_t i = 0; i < buffer_pos_; ++i) {
            advantages_[i] = (advantages_[i] - mean) / std;
        }
    }

    // ─── Actor Gradient Update (simplified SGD) ─────────────────────────────

    void update_actor_grads(const RLState& state, const double* action,
                            const double* action_mean, double advantage,
                            double ratio) noexcept {
        // Gradient of clipped PPO loss w.r.t. action mean:
        // ∂L/∂μ ≈ -advantage * (a - μ) / σ² (when not clipped)
        bool clipped = (ratio < 1.0 - cfg_.clip_epsilon) ||
                       (ratio > 1.0 + cfg_.clip_epsilon);

        if (clipped && advantage * (ratio - 1.0) > 0) return; // clipped, skip

        for (size_t j = 0; j < RL_ACTION_DIM; ++j) {
            double std = std::exp(action_log_std_[j]);
            double grad_mean = advantage * (action[j] - action_mean[j]) / (std * std);
            grad_mean = std::clamp(grad_mean, -cfg_.max_grad_norm, cfg_.max_grad_norm);

            // Update output layer of actor
            for (size_t k = 0; k < RL_HIDDEN_DIM; ++k) {
                actor_.W2[j][k] += cfg_.lr_actor * grad_mean * actor_.hidden[k];
            }
            actor_.b2[j] += cfg_.lr_actor * grad_mean;
        }
    }

    // ─── Critic Gradient Update ─────────────────────────────────────────────

    void update_critic_grads(const RLState& state, double predicted,
                             double target) noexcept {
        double grad = predicted - target;
        grad = std::clamp(grad, -cfg_.max_grad_norm, cfg_.max_grad_norm);

        // Update output layer of critic
        for (size_t k = 0; k < RL_HIDDEN_DIM; ++k) {
            critic_.W2[0][k] -= cfg_.lr_critic * grad * critic_.hidden[k];
        }
        critic_.b2[0] -= cfg_.lr_critic * grad;

        // Propagate to hidden layer (chain rule through tanh)
        for (size_t k = 0; k < RL_HIDDEN_DIM; ++k) {
            double tanh_deriv = 1.0 - critic_.hidden[k] * critic_.hidden[k];
            double hidden_grad = grad * critic_.W2[0][k] * tanh_deriv;
            hidden_grad = std::clamp(hidden_grad, -cfg_.max_grad_norm, cfg_.max_grad_norm);

            for (size_t j = 0; j < RL_STATE_DIM; ++j) {
                critic_.W1[k][j] -= cfg_.lr_critic * hidden_grad * state.features[j];
            }
            critic_.b1[k] -= cfg_.lr_critic * hidden_grad;
        }
    }

    // ─── Data ───────────────────────────────────────────────────────────────

    PPOConfig cfg_;
    ActorNet actor_;
    CriticNet critic_;
    ActionBounds bounds_;

    std::array<double, RL_ACTION_DIM> action_log_std_{};

    // Experience buffer (fixed size, no heap allocation)
    static constexpr size_t MAX_HORIZON = 512;
    Transition buffer_[MAX_HORIZON]{};
    double advantages_[MAX_HORIZON]{};
    double returns_[MAX_HORIZON]{};
    size_t buffer_pos_ = 0;

    // State
    double last_value_ = 0.0;
    double last_log_prob_ = 0.0;
    double last_policy_loss_ = 0.0;
    double last_value_loss_ = 0.0;
    uint64_t total_steps_ = 0;
    uint64_t total_updates_ = 0;
    bool exploring_ = true;

    // RNG for exploration
    std::mt19937 rng_{42};
};

} // namespace bybit
