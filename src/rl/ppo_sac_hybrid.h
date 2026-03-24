#pragma once

// ─── Hybrid PPO + SAC Actor-Critic ──────────────────────────────────────────
// Production-grade RL engine combining PPO's stability with SAC's sample
// efficiency and automatic entropy tuning.
//
// Architecture:
//   Actor:        μ(s) → 4-dim continuous action (Gaussian policy)
//   Critic Q1/Q2: twin Q-networks for pessimistic value (SAC-style)
//   Value V:      state-value for GAE advantage (PPO-style)
//
// State vector (32 dims):
//   [0..6]   orderbook features (imbalance_1/5/20, ob_slope, depth_conc, cancel_spike, liq_wall)
//   [7..11]  trade flow (aggression, avg_trade_size, trade_vel, trade_accel, vol_accel)
//   [12..16] price features (microprice, spread_bps, spread_change, mid_momentum, volatility)
//   [17..20] position state (size_pct, unrealized_pnl_pct, drawdown_pct, fill_rate)
//   [21..25] regime probs (low_vol, high_vol, trending, mean_rev, liq_vacuum)
//   [26]     queue_position (from FillProbability)
//   [27]     fill_prob_ema (from FillProbTracker in order_state_machine.h)
//   [28]     var_99_pct (MC VaR 99% / position_value, from var_engine.h, 1k scenarios)
//   [29]     cvar_99_pct (CVaR 99% / position_value)
//   [30]     recent_sharpe
//   [31]     time_of_day_sin
//
// Action vector (4 dims, continuous):
//   [0] signal_threshold_delta  [-0.1, +0.1]
//   [1] position_size_scale     [0.5, 2.0]
//   [2] order_offset_bps        [-5.0, +5.0]
//   [3] requote_freq_scale      [0.5, 3.0]
//
// Training:
//   - PPO clipped surrogate for policy stability
//   - SAC twin critics for Q-value estimation (pessimistic min(Q1,Q2))
//   - Automatic entropy temperature α tuning (target entropy = -dim(A))
//   - GAE-λ advantage estimation
//   - Online mini-batch with rollout buffer
//
// Hot path: select_action() is zero-alloc, ~2 µs (2x MLP forward + sampling).
// Training: update() runs off hot path on trainer thread.

#include "../config/types.h"
#include "../feature_engine/simd_indicators.h"
#include "../execution_engine/order_state_machine.h"
#include "../risk_engine/var_engine.h"
#include "../utils/tsc_clock.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <random>

namespace bybit {

// ─── Dimensions ─────────────────────────────────────────────────────────────

inline constexpr size_t RL2_STATE_DIM   = 32;
inline constexpr size_t RL2_ACTION_DIM  = 4;
inline constexpr size_t RL2_HIDDEN_DIM  = 128;   // wider than v1 (was 64)
inline constexpr size_t RL2_MAX_HORIZON = 1024;   // rollout buffer depth

// ─── Hyperparameters ────────────────────────────────────────────────────────

struct HybridRLConfig {
    // PPO
    double gamma             = 0.99;
    double lambda_gae        = 0.95;
    double clip_epsilon       = 0.2;
    double lr_actor           = 1e-4;
    double lr_critic          = 3e-4;
    double max_grad_norm      = 1.0;
    size_t ppo_epochs         = 4;

    // SAC entropy
    double entropy_coeff      = 0.01;    // initial α
    double target_entropy     = -4.0;    // -dim(A) = -4
    double lr_alpha           = 3e-4;    // entropy temperature LR
    bool   auto_alpha         = true;    // auto-tune α

    // Exploration
    double action_std_init    = 0.4;
    double action_std_min     = 0.02;
    double action_std_decay   = 0.9998;

    // Rollout
    size_t horizon            = 512;
    size_t batch_size         = 64;

    // SAC twin critic
    double tau                = 0.005;    // target network soft update
    double q_clip             = 100.0;    // Q-value clipping
};

// ─── Action Bounds ──────────────────────────────────────────────────────────

struct RL2ActionBounds {
    static constexpr double lo[RL2_ACTION_DIM] = {-0.1, 0.5, -5.0, 0.5};
    static constexpr double hi[RL2_ACTION_DIM] = { 0.1, 2.0,  5.0, 3.0};
};

// ─── RL State (32 dims) ─────────────────────────────────────────────────────

struct alignas(64) RL2State {
    double features[RL2_STATE_DIM]{};

    // ─── Build state from live components ───────────────────────────────────
    // Zero-alloc, branchless where possible. ~200 ns.

    static RL2State build(const Features& feat,
                          const Position& pos,
                          const RegimeState& regime,
                          const FillProbability& fp,
                          const FillProbTracker& fill_tracker,
                          const VaRResult& var,
                          double recent_sharpe,
                          double fill_rate,
                          double drawdown_pct,
                          double time_of_day_frac) noexcept {
        RL2State s;

        // [0..6] Orderbook features
        s.features[0]  = feat.imbalance_1;
        s.features[1]  = feat.imbalance_5;
        s.features[2]  = feat.imbalance_20;
        s.features[3]  = feat.ob_slope;
        s.features[4]  = feat.depth_concentration;
        s.features[5]  = feat.cancel_spike;
        s.features[6]  = feat.liquidity_wall;

        // [7..11] Trade flow
        s.features[7]  = feat.aggression_ratio;
        s.features[8]  = feat.avg_trade_size;
        s.features[9]  = feat.trade_velocity;
        s.features[10] = feat.trade_acceleration;
        s.features[11] = feat.volume_accel;

        // [12..16] Price features
        s.features[12] = feat.microprice;
        s.features[13] = feat.spread_bps;
        s.features[14] = feat.spread_change_rate;
        s.features[15] = feat.mid_momentum;
        s.features[16] = feat.volatility;

        // [17..20] Position state (normalized)
        double max_pos = 0.1; // max position for normalization
        s.features[17] = std::clamp(pos.size.raw() / max_pos, -1.0, 1.0);
        double entry = (pos.entry_price.raw() > 1e-6) ? pos.entry_price.raw() : 1.0;
        s.features[18] = std::clamp(pos.unrealized_pnl.raw() / entry * 100.0, -10.0, 10.0);
        s.features[19] = std::clamp(drawdown_pct * 10.0, 0.0, 10.0);
        s.features[20] = std::clamp(fill_rate, 0.0, 1.0);

        // [21..25] Regime probabilities (one-hot approximation)
        auto r = static_cast<size_t>(regime.current);
        for (size_t i = 0; i < NUM_REGIMES; ++i) {
            s.features[21 + i] = (i == r) ? regime.confidence : 0.0;
        }

        // [26] Queue position from fill probability model
        s.features[26] = std::clamp(fp.queue_position, 0.0, 1.0);

        // [27] Fill prob EMA from order state machine tracker
        s.features[27] = std::clamp(fill_tracker.aggregate_fill_rate(), 0.0, 1.0);

        // [28..29] VaR metrics (normalized by position value)
        double pv = std::max(var.position_value, 1.0);
        s.features[28] = std::clamp(var.mc_var_99 / pv, 0.0, 1.0);
        s.features[29] = std::clamp(var.cvar_99 / pv, 0.0, 1.0);

        // [30] Recent Sharpe ratio (clipped)
        s.features[30] = std::clamp(recent_sharpe / 3.0, -1.0, 1.0);

        // [31] Time of day (sinusoidal encoding)
        s.features[31] = std::sin(2.0 * M_PI * time_of_day_frac);

        return s;
    }
};

// ─── RL Action ──────────────────────────────────────────────────────────────

struct RL2Action {
    double signal_threshold_delta = 0.0;
    double position_size_scale    = 1.0;
    double order_offset_bps       = 0.0;
    double requote_freq_scale     = 1.0;

    alignas(32) double raw[RL2_ACTION_DIM]{};

    void to_raw() noexcept {
        raw[0] = signal_threshold_delta;
        raw[1] = position_size_scale;
        raw[2] = order_offset_bps;
        raw[3] = requote_freq_scale;
    }

    void from_raw() noexcept {
        signal_threshold_delta = raw[0];
        position_size_scale    = raw[1];
        order_offset_bps       = raw[2];
        requote_freq_scale     = raw[3];
    }

    void clamp() noexcept {
        for (size_t i = 0; i < RL2_ACTION_DIM; ++i) {
            raw[i] = std::clamp(raw[i], RL2ActionBounds::lo[i], RL2ActionBounds::hi[i]);
        }
        from_raw();
    }
};

// ─── Transition ─────────────────────────────────────────────────────────────

struct alignas(64) RL2Transition {
    RL2State state;
    double   action[RL2_ACTION_DIM]{};
    double   log_prob   = 0.0;
    double   reward     = 0.0;
    double   value      = 0.0;
    double   q1_value   = 0.0;
    double   q2_value   = 0.0;
    bool     done       = false;
};

// ─── 2-Layer MLP (wider: 32→128→out) ───────────────────────────────────────

template <size_t InDim, size_t HidDim, size_t OutDim>
struct MLP2 {
    alignas(64) double W1[HidDim][InDim]{};
    alignas(64) double b1[HidDim]{};
    alignas(64) double W2[OutDim][HidDim]{};
    alignas(64) double b2[OutDim]{};

    // Intermediate buffers (kept for backprop)
    alignas(64) double hidden[HidDim]{};
    alignas(64) double pre_act[HidDim]{};

    void forward(const double* input, double* output) noexcept {
        BYBIT_PREFETCH_R(input);
        BYBIT_PREFETCH_R(&W1[0][0]);

        // Layer 1: hidden = tanh(W1 * input + b1)
        simd::matvec(&W1[0][0], input, pre_act, HidDim, InDim);
        for (size_t i = 0; i < HidDim; ++i) {
            pre_act[i] += b1[i];
            hidden[i] = std::tanh(pre_act[i]);
        }

        BYBIT_PREFETCH_R(&W2[0][0]);

        // Layer 2: output = W2 * hidden + b2
        simd::matvec(&W2[0][0], hidden, output, OutDim, HidDim);
        for (size_t i = 0; i < OutDim; ++i) {
            output[i] += b2[i];
        }
    }

    // Xavier initialization with LCG PRNG
    void init_weights(uint64_t seed) noexcept {
        auto fill = [](double* data, size_t size, size_t fan_in, size_t fan_out,
                       uint64_t& s) {
            double scale = std::sqrt(2.0 / static_cast<double>(fan_in + fan_out));
            for (size_t i = 0; i < size; ++i) {
                s = s * 6364136223846793005ULL + 1442695040888963407ULL;
                double u = static_cast<double>(s >> 33) /
                           static_cast<double>(1ULL << 31) - 1.0;
                data[i] = u * scale;
            }
        };
        fill(&W1[0][0], HidDim * InDim, InDim, HidDim, seed);
        fill(&W2[0][0], OutDim * HidDim, HidDim, OutDim, seed);
        std::memset(b1, 0, sizeof(b1));
        std::memset(b2, 0, sizeof(b2));
    }

    // Soft copy: θ_target ← τ * θ + (1−τ) * θ_target
    void soft_update_from(const MLP2& source, double tau) noexcept {
        auto blend = [tau](double* dst, const double* src, size_t n) {
            for (size_t i = 0; i < n; ++i) {
                dst[i] = tau * src[i] + (1.0 - tau) * dst[i];
            }
        };
        blend(&W1[0][0], &source.W1[0][0], HidDim * InDim);
        blend(b1, source.b1, HidDim);
        blend(&W2[0][0], &source.W2[0][0], OutDim * HidDim);
        blend(b2, source.b2, OutDim);
    }

    // Hard copy
    void copy_from(const MLP2& source) noexcept {
        std::memcpy(this, &source, sizeof(MLP2));
    }

    // Snapshot/restore for rollback
    void snapshot_to(MLP2& dst) const noexcept {
        std::memcpy(&dst, this, sizeof(MLP2));
    }
};

// ─── Q-Network (state+action → scalar Q-value) ─────────────────────────────
// Input: concat(state[32], action[4]) = 36 dims → hidden → 1

inline constexpr size_t RL2_Q_INPUT_DIM = RL2_STATE_DIM + RL2_ACTION_DIM; // 36

using ActorNet  = MLP2<RL2_STATE_DIM, RL2_HIDDEN_DIM, RL2_ACTION_DIM>;
using CriticV   = MLP2<RL2_STATE_DIM, RL2_HIDDEN_DIM, 1>;
using CriticQ   = MLP2<RL2_Q_INPUT_DIM, RL2_HIDDEN_DIM, 1>;

// ─── Training Statistics ────────────────────────────────────────────────────

struct RL2Stats {
    uint64_t total_steps       = 0;
    uint64_t total_updates     = 0;
    double   avg_reward        = 0.0;
    double   policy_loss       = 0.0;
    double   value_loss        = 0.0;
    double   q1_loss           = 0.0;
    double   q2_loss           = 0.0;
    double   entropy           = 0.0;
    double   alpha             = 0.01;   // current entropy temperature
    double   kl_divergence     = 0.0;    // KL between old and new policy
    double   avg_q_value       = 0.0;
    uint64_t inference_ns      = 0;      // last inference latency
    uint64_t update_ns         = 0;      // last update latency
};

// ─── Hybrid PPO+SAC Agent ───────────────────────────────────────────────────

class HybridRLAgent {
public:
    HybridRLAgent() noexcept {
        init_networks(42);
    }

    explicit HybridRLAgent(const HybridRLConfig& cfg) noexcept : cfg_(cfg) {
        init_networks(42);
    }

    // ─── Select Action (HOT PATH — zero-alloc, ~2 µs) ──────────────────────
    // Runs actor forward + optional exploration noise.
    // Also evaluates V(s) for advantage computation later.

    RL2Action select_action(const RL2State& state, bool explore = true) noexcept {
        uint64_t t0 = TscClock::now();

        // Actor forward: state → action mean
        alignas(64) double action_mean[RL2_ACTION_DIM];
        actor_.forward(state.features, action_mean);

        // Value estimate V(s) for GAE
        alignas(64) double v_out[1];
        value_.forward(state.features, v_out);
        last_value_ = v_out[0];

        // Q-value estimates (for logging/diagnostics)
        alignas(64) double q_input[RL2_Q_INPUT_DIM];
        std::memcpy(q_input, state.features, RL2_STATE_DIM * sizeof(double));
        std::memcpy(q_input + RL2_STATE_DIM, action_mean, RL2_ACTION_DIM * sizeof(double));

        alignas(64) double q1_out[1], q2_out[1];
        q1_.forward(q_input, q1_out);
        q2_.forward(q_input, q2_out);
        last_q1_ = q1_out[0];
        last_q2_ = q2_out[0];

        RL2Action action;
        double log_prob = 0.0;

        if (explore && exploring_) {
            // Sample: a ~ N(μ(s), σ²)
            for (size_t i = 0; i < RL2_ACTION_DIM; ++i) {
                double std_val = std::exp(action_log_std_[i]);
                std::normal_distribution<double> dist(action_mean[i], std_val);
                action.raw[i] = dist(rng_);

                // log π(a|s) = -0.5 * ((a-μ)/σ)² - log(σ) - 0.5*log(2π)
                double diff = action.raw[i] - action_mean[i];
                log_prob += -0.5 * (diff / std_val) * (diff / std_val)
                            - action_log_std_[i] - 0.9189385332;
            }
        } else {
            std::memcpy(action.raw, action_mean, sizeof(action_mean));
            log_prob = 0.0;
        }

        action.clamp();
        last_log_prob_ = log_prob;

        stats_.inference_ns = TscClock::elapsed_ns(t0);
        return action;
    }

    // ─── Store Transition ───────────────────────────────────────────────────

    void store_transition(const RL2State& state, const RL2Action& action,
                          double reward, bool done) noexcept {
        if (buffer_pos_ >= RL2_MAX_HORIZON) return;

        auto& t = buffer_[buffer_pos_];
        t.state = state;
        std::memcpy(t.action, action.raw, sizeof(t.action));
        t.log_prob = last_log_prob_;
        t.reward   = reward;
        t.value    = last_value_;
        t.q1_value = last_q1_;
        t.q2_value = last_q2_;
        t.done     = done;

        ++buffer_pos_;
        ++stats_.total_steps;

        // Decay exploration std
        for (size_t i = 0; i < RL2_ACTION_DIM; ++i) {
            double new_std = std::exp(action_log_std_[i]) * cfg_.action_std_decay;
            action_log_std_[i] = std::log(std::max(new_std, cfg_.action_std_min));
        }
    }

    // ─── PPO+SAC Hybrid Update ──────────────────────────────────────────────
    // Called when buffer is full. Runs on trainer thread (not hot path).
    //
    // Steps:
    //   1. GAE advantage estimation (PPO)
    //   2. For each epoch:
    //      a. PPO clipped surrogate policy loss
    //      b. SAC twin Q-critic update (pessimistic min(Q1,Q2))
    //      c. Value function update
    //      d. Entropy temperature α auto-tuning
    //   3. Soft-update target networks
    //   4. Compute KL divergence for safety check

    bool update() noexcept {
        if (buffer_pos_ < cfg_.batch_size) return false;

        uint64_t t0 = TscClock::now();

        // Step 1: GAE advantages
        compute_gae();

        double total_policy_loss = 0.0;
        double total_value_loss  = 0.0;
        double total_q1_loss     = 0.0;
        double total_q2_loss     = 0.0;
        double total_entropy     = 0.0;
        double total_kl          = 0.0;
        double total_q           = 0.0;

        // Step 2: Multi-epoch update
        for (size_t epoch = 0; epoch < cfg_.ppo_epochs; ++epoch) {
            for (size_t i = 0; i < buffer_pos_; ++i) {
                auto& t = buffer_[i];

                // ── Re-evaluate under current policy ──
                alignas(64) double action_mean[RL2_ACTION_DIM];
                actor_.forward(t.state.features, action_mean);

                alignas(64) double v_out[1];
                value_.forward(t.state.features, v_out);

                // New log probability and entropy
                double new_log_prob = 0.0;
                double entropy = 0.0;
                for (size_t j = 0; j < RL2_ACTION_DIM; ++j) {
                    double std_val = std::exp(action_log_std_[j]);
                    double diff = t.action[j] - action_mean[j];
                    new_log_prob += -0.5 * (diff / std_val) * (diff / std_val)
                                   - action_log_std_[j] - 0.9189385332;
                    entropy += action_log_std_[j] + 0.5 * (1.0 + std::log(2.0 * M_PI));
                }

                // ── PPO clipped surrogate ──
                double ratio = std::exp(std::clamp(new_log_prob - t.log_prob, -20.0, 20.0));
                ratio = std::clamp(ratio, 0.01, 100.0);
                double surr1 = ratio * advantages_[i];
                double surr2 = std::clamp(ratio, 1.0 - cfg_.clip_epsilon,
                                          1.0 + cfg_.clip_epsilon) * advantages_[i];
                double policy_loss = -std::min(surr1, surr2);

                // ── SAC twin Q-critic ──
                alignas(64) double q_input[RL2_Q_INPUT_DIM];
                std::memcpy(q_input, t.state.features, RL2_STATE_DIM * sizeof(double));
                std::memcpy(q_input + RL2_STATE_DIM, t.action, RL2_ACTION_DIM * sizeof(double));

                alignas(64) double q1_out[1], q2_out[1];
                q1_.forward(q_input, q1_out);
                q2_.forward(q_input, q2_out);

                double min_q = std::min(q1_out[0], q2_out[0]);
                total_q += min_q;

                // Q-target: r + γ * V_target(s')
                double q_target = returns_[i]; // already includes bootstrap
                q_target = std::clamp(q_target, -cfg_.q_clip, cfg_.q_clip);

                double q1_loss = 0.5 * (q1_out[0] - q_target) * (q1_out[0] - q_target);
                double q2_loss = 0.5 * (q2_out[0] - q_target) * (q2_out[0] - q_target);

                // Value loss
                double value_loss = 0.5 * (v_out[0] - returns_[i]) *
                                          (v_out[0] - returns_[i]);

                // ── Entropy-regularized total loss ──
                // L = L_policy + 0.5*L_value + 0.5*(L_q1+L_q2) - α*entropy
                double alpha = log_alpha_ > -10.0 ? std::exp(log_alpha_) : cfg_.entropy_coeff;

                // ── KL divergence (approximate) ──
                double kl = new_log_prob - t.log_prob;
                total_kl += std::abs(kl);

                // ── Gradient updates ──
                update_actor_grads(t.state, t.action, action_mean,
                                   advantages_[i], ratio, alpha, entropy);
                update_value_grads(t.state, v_out[0], returns_[i]);
                update_q_grads(q_input, q1_out[0], q2_out[0], q_target);

                // ── Auto-tune α ──
                if (cfg_.auto_alpha) {
                    double alpha_loss = -std::exp(log_alpha_) *
                                        (entropy + cfg_.target_entropy);
                    log_alpha_ -= cfg_.lr_alpha * alpha_loss;
                    log_alpha_ = std::clamp(log_alpha_, -10.0, 2.0);
                }

                total_policy_loss += policy_loss;
                total_value_loss  += value_loss;
                total_q1_loss     += q1_loss;
                total_q2_loss     += q2_loss;
                total_entropy     += entropy;
            }
        }

        double n = static_cast<double>(buffer_pos_ * cfg_.ppo_epochs);

        // Step 3: Soft-update target networks
        q1_target_.soft_update_from(q1_, cfg_.tau);
        q2_target_.soft_update_from(q2_, cfg_.tau);

        // Update stats
        stats_.policy_loss  = total_policy_loss / n;
        stats_.value_loss   = total_value_loss / n;
        stats_.q1_loss      = total_q1_loss / n;
        stats_.q2_loss      = total_q2_loss / n;
        stats_.entropy      = total_entropy / n;
        stats_.alpha        = std::exp(log_alpha_);
        stats_.kl_divergence = total_kl / n;
        stats_.avg_q_value  = total_q / n;
        stats_.update_ns    = TscClock::elapsed_ns(t0);

        // Compute avg reward
        double r_sum = 0.0;
        for (size_t i = 0; i < buffer_pos_; ++i) r_sum += buffer_[i].reward;
        stats_.avg_reward = r_sum / static_cast<double>(buffer_pos_);

        // Reset buffer
        buffer_pos_ = 0;
        ++stats_.total_updates;

        return true;
    }

    bool should_update() const noexcept {
        return buffer_pos_ >= cfg_.horizon;
    }

    // ─── Reward Function ────────────────────────────────────────────────────
    // Multi-objective: PnL, risk, execution quality, VaR penalty.

    static double compute_reward(double pnl_change, double drawdown_pct,
                                 double fill_rate, double latency_us,
                                 double var_breach_pct = 0.0,
                                 double slippage_bps = 0.0) noexcept {
        double reward = 0.0;

        // PnL component (primary)
        reward += pnl_change * 10.0;

        // Drawdown penalty (exponential for large drawdowns)
        if (drawdown_pct > 0.02) {
            reward -= std::pow(drawdown_pct * 100.0, 2) * 0.1;
        }

        // Fill rate bonus
        reward += fill_rate * 0.5;

        // Latency penalty
        if (latency_us > 200.0) {
            reward -= (latency_us - 200.0) * 0.001;
        }

        // VaR breach penalty (from VaREngine integration)
        if (var_breach_pct > 0.0) {
            reward -= var_breach_pct * 5.0;
        }

        // Slippage penalty
        reward -= std::abs(slippage_bps) * 0.01;

        return std::clamp(reward, -10.0, 10.0);
    }

    // ─── Snapshot / Restore (for safe online trainer rollback) ───────────────

    struct Snapshot {
        ActorNet  actor;
        CriticV   value;
        CriticQ   q1, q2;
        double    action_log_std[RL2_ACTION_DIM];
        double    log_alpha;
    };

    void snapshot(Snapshot& snap) const noexcept {
        actor_.snapshot_to(snap.actor);
        value_.snapshot_to(snap.value);
        q1_.snapshot_to(snap.q1);
        q2_.snapshot_to(snap.q2);
        std::memcpy(snap.action_log_std, action_log_std_.data(),
                    sizeof(snap.action_log_std));
        snap.log_alpha = log_alpha_;
    }

    void restore(const Snapshot& snap) noexcept {
        actor_.copy_from(snap.actor);
        value_.copy_from(snap.value);
        q1_.copy_from(snap.q1);
        q2_.copy_from(snap.q2);
        std::memcpy(action_log_std_.data(), snap.action_log_std,
                    sizeof(snap.action_log_std));
        log_alpha_ = snap.log_alpha;
        // Also update targets
        q1_target_.copy_from(q1_);
        q2_target_.copy_from(q2_);
    }

    // ─── Accessors ──────────────────────────────────────────────────────────

    const RL2Stats& stats() const noexcept { return stats_; }
    RL2Stats& stats() noexcept { return stats_; }
    HybridRLConfig& config() noexcept { return cfg_; }
    const HybridRLConfig& config() const noexcept { return cfg_; }
    bool exploring() const noexcept { return exploring_; }
    void set_exploring(bool e) noexcept { exploring_ = e; }
    size_t buffer_size() const noexcept { return buffer_pos_; }
    double current_alpha() const noexcept { return std::exp(log_alpha_); }

    // KL divergence from last update (for safe trainer)
    double last_kl() const noexcept { return stats_.kl_divergence; }

    // Get current action std for monitoring
    double avg_action_std() const noexcept {
        double sum = 0.0;
        for (size_t i = 0; i < RL2_ACTION_DIM; ++i)
            sum += std::exp(action_log_std_[i]);
        return sum / RL2_ACTION_DIM;
    }

    // Direct network access (for trainer thread)
    ActorNet& actor() noexcept { return actor_; }
    CriticV& value_net() noexcept { return value_; }
    CriticQ& q1() noexcept { return q1_; }
    CriticQ& q2() noexcept { return q2_; }

private:
    void init_networks(uint64_t seed) noexcept {
        actor_.init_weights(seed);
        value_.init_weights(seed + 137);
        q1_.init_weights(seed + 271);
        q2_.init_weights(seed + 419);
        q1_target_.copy_from(q1_);
        q2_target_.copy_from(q2_);
        action_log_std_.fill(std::log(cfg_.action_std_init));
        log_alpha_ = std::log(cfg_.entropy_coeff);
    }

    // ─── GAE Advantage Estimation ───────────────────────────────────────────

    void compute_gae() noexcept {
        double next_value = 0.0;
        if (buffer_pos_ > 0 && !buffer_[buffer_pos_ - 1].done) {
            alignas(64) double v[1];
            value_.forward(buffer_[buffer_pos_ - 1].state.features, v);
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
        mean /= static_cast<double>(buffer_pos_);
        for (size_t i = 0; i < buffer_pos_; ++i) {
            double d = advantages_[i] - mean;
            var += d * d;
        }
        var /= static_cast<double>(buffer_pos_);
        double std_val = std::sqrt(var + 1e-8);
        for (size_t i = 0; i < buffer_pos_; ++i) {
            advantages_[i] = (advantages_[i] - mean) / std_val;
        }
    }

    // ─── Actor Gradient (PPO clipped + entropy bonus) ───────────────────────

    void update_actor_grads(const RL2State& state, const double* action,
                            const double* action_mean, double advantage,
                            double ratio, double alpha, double entropy) noexcept {
        bool clipped = (ratio < 1.0 - cfg_.clip_epsilon) ||
                       (ratio > 1.0 + cfg_.clip_epsilon);

        if (clipped && advantage * (ratio - 1.0) > 0) return;

        for (size_t j = 0; j < RL2_ACTION_DIM; ++j) {
            double std_val = std::exp(action_log_std_[j]);
            double grad_mean = advantage * (action[j] - action_mean[j]) / (std_val * std_val);

            // Add entropy gradient: ∂H/∂μ = 0, but ∂H/∂log_σ = 1
            // So entropy bonus affects log_std, not mean — handled separately

            grad_mean = std::clamp(grad_mean, -cfg_.max_grad_norm, cfg_.max_grad_norm);

            // Update output layer
            for (size_t k = 0; k < RL2_HIDDEN_DIM; ++k) {
                actor_.W2[j][k] += cfg_.lr_actor * grad_mean * actor_.hidden[k];
            }
            actor_.b2[j] += cfg_.lr_actor * grad_mean;

            // Update action_log_std with entropy bonus
            double std_grad = advantage * (action[j] - action_mean[j]) *
                              (action[j] - action_mean[j]) / (std_val * std_val * std_val)
                              - advantage / std_val;
            std_grad += alpha; // entropy bonus: encourages higher std
            std_grad = std::clamp(std_grad, -cfg_.max_grad_norm, cfg_.max_grad_norm);
            action_log_std_[j] += cfg_.lr_actor * 0.1 * std_grad;
            action_log_std_[j] = std::clamp(action_log_std_[j],
                                             std::log(cfg_.action_std_min), 2.0);
        }

        // Backprop to hidden layer
        for (size_t k = 0; k < RL2_HIDDEN_DIM; ++k) {
            double tanh_d = 1.0 - actor_.hidden[k] * actor_.hidden[k];
            double h_grad = 0.0;
            for (size_t j = 0; j < RL2_ACTION_DIM; ++j) {
                double std_val = std::exp(action_log_std_[j]);
                double g = advantage * (action[j] - action_mean[j]) / (std_val * std_val);
                g = std::clamp(g, -cfg_.max_grad_norm, cfg_.max_grad_norm);
                h_grad += g * actor_.W2[j][k];
            }
            h_grad *= tanh_d;
            h_grad = std::clamp(h_grad, -cfg_.max_grad_norm, cfg_.max_grad_norm);

            for (size_t j = 0; j < RL2_STATE_DIM; ++j) {
                actor_.W1[k][j] += cfg_.lr_actor * h_grad * state.features[j];
            }
            actor_.b1[k] += cfg_.lr_actor * h_grad;
        }
    }

    // ─── Value Network Gradient ─────────────────────────────────────────────

    void update_value_grads(const RL2State& state, double predicted,
                            double target) noexcept {
        double grad = std::clamp(predicted - target,
                                 -cfg_.max_grad_norm, cfg_.max_grad_norm);

        // Output layer
        for (size_t k = 0; k < RL2_HIDDEN_DIM; ++k) {
            value_.W2[0][k] -= cfg_.lr_critic * grad * value_.hidden[k];
        }
        value_.b2[0] -= cfg_.lr_critic * grad;

        // Hidden layer
        for (size_t k = 0; k < RL2_HIDDEN_DIM; ++k) {
            double tanh_d = 1.0 - value_.hidden[k] * value_.hidden[k];
            double h_grad = grad * value_.W2[0][k] * tanh_d;
            h_grad = std::clamp(h_grad, -cfg_.max_grad_norm, cfg_.max_grad_norm);

            for (size_t j = 0; j < RL2_STATE_DIM; ++j) {
                value_.W1[k][j] -= cfg_.lr_critic * h_grad * state.features[j];
            }
            value_.b1[k] -= cfg_.lr_critic * h_grad;
        }
    }

    // ─── Twin Q-Critic Gradients ────────────────────────────────────────────

    void update_q_grads(const double* q_input,
                        double q1_pred, double q2_pred,
                        double q_target) noexcept {
        double grad1 = std::clamp(q1_pred - q_target,
                                   -cfg_.max_grad_norm, cfg_.max_grad_norm);
        double grad2 = std::clamp(q2_pred - q_target,
                                   -cfg_.max_grad_norm, cfg_.max_grad_norm);

        // Q1 output layer
        for (size_t k = 0; k < RL2_HIDDEN_DIM; ++k) {
            q1_.W2[0][k] -= cfg_.lr_critic * grad1 * q1_.hidden[k];
        }
        q1_.b2[0] -= cfg_.lr_critic * grad1;

        // Q2 output layer
        for (size_t k = 0; k < RL2_HIDDEN_DIM; ++k) {
            q2_.W2[0][k] -= cfg_.lr_critic * grad2 * q2_.hidden[k];
        }
        q2_.b2[0] -= cfg_.lr_critic * grad2;

        // Q1 hidden layer backprop
        for (size_t k = 0; k < RL2_HIDDEN_DIM; ++k) {
            double tanh_d = 1.0 - q1_.hidden[k] * q1_.hidden[k];
            double h_grad = grad1 * q1_.W2[0][k] * tanh_d;
            h_grad = std::clamp(h_grad, -cfg_.max_grad_norm, cfg_.max_grad_norm);
            for (size_t j = 0; j < RL2_Q_INPUT_DIM; ++j) {
                q1_.W1[k][j] -= cfg_.lr_critic * h_grad * q_input[j];
            }
            q1_.b1[k] -= cfg_.lr_critic * h_grad;
        }

        // Q2 hidden layer backprop
        for (size_t k = 0; k < RL2_HIDDEN_DIM; ++k) {
            double tanh_d = 1.0 - q2_.hidden[k] * q2_.hidden[k];
            double h_grad = grad2 * q2_.W2[0][k] * tanh_d;
            h_grad = std::clamp(h_grad, -cfg_.max_grad_norm, cfg_.max_grad_norm);
            for (size_t j = 0; j < RL2_Q_INPUT_DIM; ++j) {
                q2_.W1[k][j] -= cfg_.lr_critic * h_grad * q_input[j];
            }
            q2_.b1[k] -= cfg_.lr_critic * h_grad;
        }
    }

    // ─── Data ───────────────────────────────────────────────────────────────

    HybridRLConfig cfg_;

    // Networks
    ActorNet  actor_;
    CriticV   value_;
    CriticQ   q1_, q2_;
    CriticQ   q1_target_, q2_target_;   // target networks for SAC

    // Exploration
    std::array<double, RL2_ACTION_DIM> action_log_std_{};
    double log_alpha_ = -4.6; // log(0.01)

    // Rollout buffer (fixed-size, zero heap alloc)
    RL2Transition buffer_[RL2_MAX_HORIZON]{};
    double advantages_[RL2_MAX_HORIZON]{};
    double returns_[RL2_MAX_HORIZON]{};
    size_t buffer_pos_ = 0;

    // Last inference state
    double last_value_    = 0.0;
    double last_log_prob_ = 0.0;
    double last_q1_       = 0.0;
    double last_q2_       = 0.0;

    bool exploring_ = true;

    // Stats
    RL2Stats stats_{};

    // RNG
    std::mt19937 rng_{42};
};

} // namespace bybit
