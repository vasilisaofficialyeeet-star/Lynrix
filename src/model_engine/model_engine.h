#pragma once

#include "../config/types.h"
#include <array>
#include <cmath>
#include <cstddef>

namespace bybit {

// Logistic regression model with static weights.
// Inference time target: < 50µs (trivially met for 11 features).
class ModelEngine {
public:
    ModelEngine() noexcept = default;

    void load_weights(const std::array<double, Features::COUNT>& weights, double bias) noexcept {
        weights_ = weights;
        bias_ = bias;
        loaded_ = true;
    }

    // Returns probability of price going up.
    // Input: feature vector (11 doubles, contiguous).
    ModelOutput predict(const Features& features) const noexcept {
        ModelOutput out;
        if (!loaded_) return out;

        const double* x = features.as_array();
        double z = bias_;

        // Dot product — compiler will auto-vectorize with -O3 -march=native
        for (size_t i = 0; i < Features::COUNT; ++i) {
            z += weights_[i] * x[i];
        }

        // Sigmoid with clamping to avoid overflow
        double prob = sigmoid(z);

        out.probability_up = prob;
        out.probability_down = 1.0 - prob;
        return out;
    }

    bool loaded() const noexcept { return loaded_; }

private:
    static double sigmoid(double x) noexcept {
        if (x > 20.0) return 1.0;
        if (x < -20.0) return 0.0;
        return 1.0 / (1.0 + std::exp(-x));
    }

    std::array<double, Features::COUNT> weights_{};
    double bias_ = 0.0;
    bool loaded_ = false;
};

} // namespace bybit
