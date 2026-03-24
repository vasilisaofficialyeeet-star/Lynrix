#pragma once

#include "../config/types.h"
#include "../feature_engine/advanced_feature_engine.h"
#include "../utils/clock.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace bybit {

// ─── GRU Cell ───────────────────────────────────────────────────────────────
// Single GRU cell: h_t = GRU(x_t, h_{t-1})
// Gates: z (update), r (reset), n (new)
// z_t = σ(W_z·x_t + U_z·h_{t-1} + b_z)
// r_t = σ(W_r·x_t + U_r·h_{t-1} + b_r)
// n_t = tanh(W_n·x_t + r_t ⊙ (U_n·h_{t-1}) + b_n)
// h_t = (1 - z_t) ⊙ n_t + z_t ⊙ h_{t-1}

template <size_t InputSize, size_t HiddenSize>
class GRUCell {
public:
    // Weight matrices: [gate][hidden][input] for W, [gate][hidden][hidden] for U
    // Gates: 0=z(update), 1=r(reset), 2=n(new)
    alignas(64) double W[3][HiddenSize][InputSize] = {};   // input weights
    alignas(64) double U[3][HiddenSize][HiddenSize] = {};  // recurrent weights
    alignas(64) double b_w[3][HiddenSize] = {};             // input bias
    alignas(64) double b_u[3][HiddenSize] = {};             // recurrent bias

    // Forward pass: compute h_t given x_t and h_{t-1}
    void forward(const double* x, const double* h_prev, double* h_out) const noexcept {
        alignas(64) double z[HiddenSize];
        alignas(64) double r[HiddenSize];
        alignas(64) double n[HiddenSize];
        alignas(64) double Uh[3][HiddenSize];

        // Compute U·h for all 3 gates
        for (size_t g = 0; g < 3; ++g) {
            for (size_t i = 0; i < HiddenSize; ++i) {
                double sum = 0.0;
                for (size_t j = 0; j < HiddenSize; ++j) {
                    sum += U[g][i][j] * h_prev[j];
                }
                Uh[g][i] = sum + b_u[g][i];
            }
        }

        // Compute W·x for z and r gates, apply sigmoid
        for (size_t i = 0; i < HiddenSize; ++i) {
            double wz = b_w[0][i], wr = b_w[1][i];
            for (size_t j = 0; j < InputSize; ++j) {
                wz += W[0][i][j] * x[j];
                wr += W[1][i][j] * x[j];
            }
            z[i] = sigmoid(wz + Uh[0][i]);
            r[i] = sigmoid(wr + Uh[1][i]);
        }

        // Compute new gate: n = tanh(W_n·x + r ⊙ U_n·h + b)
        for (size_t i = 0; i < HiddenSize; ++i) {
            double wn = b_w[2][i];
            for (size_t j = 0; j < InputSize; ++j) {
                wn += W[2][i][j] * x[j];
            }
            n[i] = fast_tanh(wn + r[i] * Uh[2][i]);
        }

        // h_t = (1 - z) ⊙ n + z ⊙ h_prev
        for (size_t i = 0; i < HiddenSize; ++i) {
            h_out[i] = (1.0 - z[i]) * n[i] + z[i] * h_prev[i];
        }
    }

private:
    static double sigmoid(double x) noexcept {
        if (x > 15.0) return 1.0;
        if (x < -15.0) return 0.0;
        return 1.0 / (1.0 + std::exp(-x));
    }

    static double fast_tanh(double x) noexcept {
        if (x > 7.0) return 1.0;
        if (x < -7.0) return -1.0;
        return std::tanh(x);
    }
};

// ─── Output Head ────────────────────────────────────────────────────────────
// Linear layer: hidden -> 3 classes per horizon (up, down, flat)

template <size_t HiddenSize, size_t NumHorizons>
class OutputHead {
public:
    static constexpr size_t CLASSES = 3; // up, down, flat
    static constexpr size_t OUTPUT_SIZE = NumHorizons * CLASSES;

    alignas(64) double W[OUTPUT_SIZE][HiddenSize] = {};
    alignas(64) double b[OUTPUT_SIZE] = {};

    void forward(const double* hidden, double* output) const noexcept {
        for (size_t i = 0; i < OUTPUT_SIZE; ++i) {
            double sum = b[i];
            for (size_t j = 0; j < HiddenSize; ++j) {
                sum += W[i][j] * hidden[j];
            }
            output[i] = sum;
        }

        // Apply softmax per horizon
        for (size_t h = 0; h < NumHorizons; ++h) {
            softmax(&output[h * CLASSES], CLASSES);
        }
    }

private:
    static void softmax(double* x, size_t n) noexcept {
        double max_val = x[0];
        for (size_t i = 1; i < n; ++i) {
            if (x[i] > max_val) max_val = x[i];
        }
        double sum = 0.0;
        for (size_t i = 0; i < n; ++i) {
            x[i] = std::exp(x[i] - max_val);
            sum += x[i];
        }
        if (sum > 1e-12) {
            for (size_t i = 0; i < n; ++i) x[i] /= sum;
        }
    }
};

// ─── GRU Model Engine ───────────────────────────────────────────────────────
// Full GRU model: processes feature sequences, outputs multi-horizon predictions.
// Inference target: < 1ms for 100-step sequence with 25 features.

class GRUModelEngine {
public:
    using Cell = GRUCell<FEATURE_COUNT, GRU_HIDDEN_SIZE>;
    using Head = OutputHead<GRU_HIDDEN_SIZE, NUM_HORIZONS>;

    GRUModelEngine() noexcept {
        reset_hidden();
        init_random_weights(); // Initialize with small random weights
    }

    // Run inference on a feature sequence.
    // seq: [seq_len x FEATURE_COUNT] row-major
    // Returns ModelOutput with multi-horizon predictions.
    ModelOutput predict(const FeatureRingBuffer& history) noexcept {
        uint64_t start_ns = Clock::now_ns();
        ModelOutput out;

        if (history.size() < 10) {
            out.timestamp_ns = start_ns;
            return out;
        }

        // Fill sequence buffer from history
        size_t seq_len = std::min(history.size(), FEATURE_SEQ_LEN);
        alignas(64) double seq[FEATURE_SEQ_LEN * FEATURE_COUNT];
        history.fill_sequence(seq, seq_len);

        // Normalize features
        normalize_sequence(seq, seq_len);

        // Reset hidden state for fresh inference (stateless mode)
        reset_hidden();

        // Process sequence through GRU
        for (size_t t = 0; t < seq_len; ++t) {
            const double* x_t = &seq[t * FEATURE_COUNT];
            gru_.forward(x_t, hidden_, hidden_);
        }

        // Output head: hidden -> predictions
        alignas(64) double raw_output[Head::OUTPUT_SIZE];
        head_.forward(hidden_, raw_output);

        // Parse output into ModelOutput
        for (size_t h = 0; h < NUM_HORIZONS; ++h) {
            auto& hp = out.horizons[h];
            hp.prob_up   = raw_output[h * 3 + 0];
            hp.prob_down = raw_output[h * 3 + 1];
            hp.prob_flat = raw_output[h * 3 + 2];
            hp.predicted_move_bps = (hp.prob_up - hp.prob_down) * 10.0; // rough estimate
        }

        // Primary prediction: 500ms horizon (index 1)
        out.probability_up   = out.horizons[1].prob_up;
        out.probability_down = out.horizons[1].prob_down;

        // Model confidence: max probability across classes
        double max_prob = std::max({out.probability_up, out.probability_down,
                                    out.horizons[1].prob_flat});
        out.model_confidence = (max_prob - 0.333) / 0.667; // normalize [0.333, 1] -> [0, 1]
        out.model_confidence = std::clamp(out.model_confidence, 0.0, 1.0);

        uint64_t end_ns = Clock::now_ns();
        out.inference_latency_ns = end_ns - start_ns;
        out.timestamp_ns = end_ns;

        return out;
    }

    // Load weights from binary file
    bool load_weights(const std::string& path) noexcept {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        // Read GRU weights
        file.read(reinterpret_cast<char*>(&gru_), sizeof(Cell));
        // Read output head
        file.read(reinterpret_cast<char*>(&head_), sizeof(Head));
        // Read normalization stats
        file.read(reinterpret_cast<char*>(feat_mean_.data()), sizeof(double) * FEATURE_COUNT);
        file.read(reinterpret_cast<char*>(feat_std_.data()), sizeof(double) * FEATURE_COUNT);

        loaded_ = file.good();
        return loaded_;
    }

    // Save weights to binary file
    bool save_weights(const std::string& path) const noexcept {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        file.write(reinterpret_cast<const char*>(&gru_), sizeof(Cell));
        file.write(reinterpret_cast<const char*>(&head_), sizeof(Head));
        file.write(reinterpret_cast<const char*>(feat_mean_.data()), sizeof(double) * FEATURE_COUNT);
        file.write(reinterpret_cast<const char*>(feat_std_.data()), sizeof(double) * FEATURE_COUNT);

        return file.good();
    }

    bool loaded() const noexcept { return loaded_; }

    // Online learning: update weights with a single gradient step
    // label: 0=up, 1=down, 2=flat for each horizon
    void online_update(const FeatureRingBuffer& history,
                       const std::array<int, NUM_HORIZONS>& labels,
                       double learning_rate = 0.0001) noexcept {
        // Numerical gradient approximation (simple but effective for online learning)
        // In production, use proper BPTT — this is a simplified version
        ModelOutput base = predict(history);

        // Update output head biases with gradient of cross-entropy loss
        for (size_t h = 0; h < NUM_HORIZONS; ++h) {
            int label = labels[h];
            double probs[3] = {
                base.horizons[h].prob_up,
                base.horizons[h].prob_down,
                base.horizons[h].prob_flat
            };
            // Gradient of cross-entropy w.r.t. logits = prob - one_hot
            for (int c = 0; c < 3; ++c) {
                double grad = probs[c] - (c == label ? 1.0 : 0.0);
                head_.b[h * 3 + c] -= learning_rate * grad;
                // Also update last row of weights connected to most active hidden units
                for (size_t j = 0; j < GRU_HIDDEN_SIZE; ++j) {
                    head_.W[h * 3 + c][j] -= learning_rate * grad * hidden_[j];
                }
            }
        }
    }

    // Set normalization statistics from training data
    void set_normalization(const std::array<double, FEATURE_COUNT>& mean,
                          const std::array<double, FEATURE_COUNT>& std) noexcept {
        feat_mean_ = mean;
        feat_std_ = std;
    }

private:
    void reset_hidden() noexcept {
        std::memset(hidden_, 0, sizeof(hidden_));
    }

    void normalize_sequence(double* seq, size_t seq_len) const noexcept {
        for (size_t t = 0; t < seq_len; ++t) {
            for (size_t f = 0; f < FEATURE_COUNT; ++f) {
                double& val = seq[t * FEATURE_COUNT + f];
                double s = feat_std_[f];
                if (s > 1e-12) {
                    val = (val - feat_mean_[f]) / s;
                }
                // Clip to [-5, 5] to prevent extreme values
                val = std::clamp(val, -5.0, 5.0);
            }
        }
    }

    void init_random_weights() noexcept {
        // Xavier initialization: scale = sqrt(2 / (fan_in + fan_out))
        auto xavier = [](double* data, size_t size, size_t fan_in, size_t fan_out) {
            double scale = std::sqrt(2.0 / (fan_in + fan_out));
            // Simple deterministic pseudo-random (good enough for initialization)
            uint64_t seed = 42;
            for (size_t i = 0; i < size; ++i) {
                seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
                double u = static_cast<double>(seed >> 33) / static_cast<double>(1ULL << 31) - 1.0;
                data[i] = u * scale;
            }
        };

        // GRU input weights
        for (int g = 0; g < 3; ++g) {
            xavier(&gru_.W[g][0][0], GRU_HIDDEN_SIZE * FEATURE_COUNT,
                   FEATURE_COUNT, GRU_HIDDEN_SIZE);
            xavier(&gru_.U[g][0][0], GRU_HIDDEN_SIZE * GRU_HIDDEN_SIZE,
                   GRU_HIDDEN_SIZE, GRU_HIDDEN_SIZE);
        }

        // Output head
        xavier(&head_.W[0][0], Head::OUTPUT_SIZE * GRU_HIDDEN_SIZE,
               GRU_HIDDEN_SIZE, Head::OUTPUT_SIZE);

        // Default normalization: zero mean, unit std
        feat_mean_.fill(0.0);
        feat_std_.fill(1.0);
    }

    Cell gru_;
    Head head_;
    alignas(64) double hidden_[GRU_HIDDEN_SIZE] = {};

    std::array<double, FEATURE_COUNT> feat_mean_{};
    std::array<double, FEATURE_COUNT> feat_std_{};

    bool loaded_ = false;
};

} // namespace bybit
