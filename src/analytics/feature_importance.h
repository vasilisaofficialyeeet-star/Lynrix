#pragma once

#include "../config/types.h"
#include "../utils/clock.h"
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <random>
#include <string>

namespace bybit {

// ─── Feature Importance & Auto-Discovery System ─────────────────────────────
// Analyzes feature contributions to predictions using:
//   1. Permutation importance (model-agnostic)
//   2. Mutual information estimation
//   3. SHAP-like marginal contribution approximation
//   4. Automatic candidate feature generation and testing

struct FeatureImportanceScore {
    double permutation_importance = 0.0; // accuracy drop when feature permuted
    double mutual_information     = 0.0; // MI with target variable
    double shap_value            = 0.0;  // average marginal contribution
    double correlation           = 0.0;  // linear correlation with target
    bool   is_candidate          = false; // auto-generated candidate
    bool   statistically_significant = false;
};

struct FeatureImportanceSnapshot {
    std::array<FeatureImportanceScore, FEATURE_COUNT> scores{};
    std::array<int, FEATURE_COUNT> ranking{}; // sorted indices by importance
    double total_explained_variance = 0.0;
    int    active_features          = 0;
    int    candidates_tested        = 0;
    int    candidates_accepted      = 0;
    uint64_t last_update_ns         = 0;
};

class FeatureImportanceAnalyzer {
public:
    static constexpr size_t HISTORY_SIZE = 2048;
    static constexpr size_t MIN_SAMPLES  = 200;

    FeatureImportanceAnalyzer() noexcept { reset(); }

    void reset() noexcept {
        sample_count_ = 0;
        feature_head_ = 0;
        snapshot_ = {};
        for (auto& r : snapshot_.ranking) r = 0;
        for (size_t i = 0; i < FEATURE_COUNT; ++i) snapshot_.ranking[i] = static_cast<int>(i);
    }

    // Record a feature vector and its associated target (price move direction)
    // target: +1 = up, -1 = down, 0 = flat
    void record_sample(const Features& features, int target, double price_move_bps) noexcept {
        size_t idx = feature_head_ % HISTORY_SIZE;

        const double* f = features.as_array();
        for (size_t i = 0; i < FEATURE_COUNT; ++i) {
            feature_history_[idx][i] = f[i];
        }
        target_history_[idx] = target;
        move_history_[idx] = price_move_bps;

        feature_head_++;
        if (sample_count_ < HISTORY_SIZE) sample_count_++;
    }

    // E3: Load precomputed feature importance from offline artifact file.
    // File format: binary, FEATURE_COUNT * sizeof(FeatureImportanceScore).
    // This is the preferred production path — no expensive runtime computation.
    bool load_static(const std::string& path) noexcept {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        file.read(reinterpret_cast<char*>(snapshot_.scores.data()),
                  sizeof(FeatureImportanceScore) * FEATURE_COUNT);
        if (!file.good()) return false;

        // Rebuild ranking from loaded scores
        rerank_from_scores();
        snapshot_.last_update_ns = Clock::now_ns();
        static_loaded_ = true;
        return true;
    }

    // E3: Save current importance snapshot for offline use.
    bool save_static(const std::string& path) const noexcept {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        file.write(reinterpret_cast<const char*>(snapshot_.scores.data()),
                   sizeof(FeatureImportanceScore) * FEATURE_COUNT);
        return file.good();
    }

    // E3: Lightweight runtime update — correlation only, O(features × samples).
    // Permutation importance & SHAP are NOT computed at runtime.
    // If static data was loaded, only correlation is refreshed; PI/SHAP
    // retain their offline values.
    void compute() noexcept {
        if (sample_count_ < MIN_SAMPLES) return;

        size_t n = sample_count_;

        for (size_t f = 0; f < FEATURE_COUNT; ++f) {
            auto& score = snapshot_.scores[f];

            // Lightweight: correlation only (single O(n) pass per feature)
            score.correlation = compute_correlation(f, n);

            // E3: permutation_importance and shap_value are NOT recomputed
            // at runtime. They retain loaded static values (or zero if none).

            double threshold = 2.0 / std::sqrt(static_cast<double>(n));
            score.statistically_significant =
                std::abs(score.correlation) > threshold;
        }

        rerank_from_scores();
        snapshot_.last_update_ns = Clock::now_ns();
    }

    // E3: Full offline compute — call from a batch script, NOT from production.
    // Computes permutation importance, MI, and SHAP approximation.
    void compute_offline() noexcept {
        if (sample_count_ < MIN_SAMPLES) return;
        size_t n = sample_count_;

        for (size_t f = 0; f < FEATURE_COUNT; ++f) {
            auto& score = snapshot_.scores[f];
            score.correlation = compute_correlation(f, n);
            score.mutual_information = estimate_mutual_information(f, n);
            score.permutation_importance = compute_permutation_importance(f, n);
            score.shap_value = approximate_shap(f, n);

            double threshold = 2.0 / std::sqrt(static_cast<double>(n));
            score.statistically_significant =
                std::abs(score.correlation) > threshold;
        }

        rerank_from_scores();
        snapshot_.last_update_ns = Clock::now_ns();
    }

    bool static_loaded() const noexcept { return static_loaded_; }

    // Get feature name by index
    static const char* feature_name(size_t idx) noexcept {
        static const char* names[FEATURE_COUNT] = {
            "imbalance_1", "imbalance_5", "imbalance_20", "ob_slope",
            "depth_conc", "cancel_spike", "liq_wall",
            "aggr_ratio", "avg_trade_sz", "trade_vel", "trade_accel", "vol_accel",
            "microprice", "spread_bps", "spread_chg", "mid_momentum", "volatility",
            "mp_dev", "st_pressure", "bid_depth", "ask_depth",
            "d_imb_dt", "d2_imb_dt2", "d_vol_dt", "d_mom_dt"
        };
        return idx < FEATURE_COUNT ? names[idx] : "unknown";
    }

    const FeatureImportanceSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    // E3: Shared ranking logic used by both compute() and load_static()
    void rerank_from_scores() noexcept {
        std::array<double, FEATURE_COUNT> composite{};
        for (size_t f = 0; f < FEATURE_COUNT; ++f) {
            auto& s = snapshot_.scores[f];
            composite[f] = std::abs(s.permutation_importance) * 0.3 +
                           s.mutual_information * 0.3 +
                           std::abs(s.shap_value) * 0.2 +
                           std::abs(s.correlation) * 0.2;
        }
        std::iota(snapshot_.ranking.begin(), snapshot_.ranking.end(), 0);
        std::sort(snapshot_.ranking.begin(), snapshot_.ranking.end(),
                  [&](int a, int b) { return composite[a] > composite[b]; });

        snapshot_.active_features = 0;
        for (size_t f = 0; f < FEATURE_COUNT; ++f) {
            if (snapshot_.scores[f].statistically_significant) {
                snapshot_.active_features++;
            }
        }
    }

    double compute_correlation(size_t feat_idx, size_t n) const noexcept {
        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0, sum_y2 = 0;

        for (size_t i = 0; i < n; ++i) {
            size_t idx = (feature_head_ - n + i) % HISTORY_SIZE;
            double x = feature_history_[idx][feat_idx];
            double y = move_history_[idx];
            sum_x += x;
            sum_y += y;
            sum_xy += x * y;
            sum_x2 += x * x;
            sum_y2 += y * y;
        }

        double dn = static_cast<double>(n);
        double num = dn * sum_xy - sum_x * sum_y;
        double den = std::sqrt((dn * sum_x2 - sum_x * sum_x) *
                               (dn * sum_y2 - sum_y * sum_y));
        return den > 1e-12 ? num / den : 0.0;
    }

    double estimate_mutual_information(size_t feat_idx, size_t n) const noexcept {
        // Binned MI estimate: discretize feature into K bins
        constexpr int K = 10;
        std::array<std::array<int, 3>, K> joint{}; // [bin][class]
        std::array<int, K> feat_count{};
        std::array<int, 3> class_count{};

        // Find feature range
        double fmin = 1e30, fmax = -1e30;
        for (size_t i = 0; i < n; ++i) {
            size_t idx = (feature_head_ - n + i) % HISTORY_SIZE;
            double v = feature_history_[idx][feat_idx];
            fmin = std::min(fmin, v);
            fmax = std::max(fmax, v);
        }

        double range = fmax - fmin;
        if (range < 1e-12) return 0.0;

        for (size_t i = 0; i < n; ++i) {
            size_t idx = (feature_head_ - n + i) % HISTORY_SIZE;
            double v = feature_history_[idx][feat_idx];
            int bin = std::clamp(static_cast<int>((v - fmin) / range * K), 0, K - 1);
            int cls = target_history_[idx] + 1; // -1,0,1 -> 0,1,2
            cls = std::clamp(cls, 0, 2);

            joint[bin][cls]++;
            feat_count[bin]++;
            class_count[cls]++;
        }

        // Compute MI
        double mi = 0.0;
        double dn = static_cast<double>(n);
        for (int b = 0; b < K; ++b) {
            for (int c = 0; c < 3; ++c) {
                if (joint[b][c] > 0 && feat_count[b] > 0 && class_count[c] > 0) {
                    double p_xy = joint[b][c] / dn;
                    double p_x = feat_count[b] / dn;
                    double p_y = class_count[c] / dn;
                    mi += p_xy * std::log(p_xy / (p_x * p_y));
                }
            }
        }
        return std::max(mi, 0.0);
    }

    double compute_permutation_importance(size_t feat_idx, size_t n) const noexcept {
        // Simplified: measure accuracy drop when feature is randomized
        // Use sign(correlation) as baseline prediction accuracy
        double base_corr = std::abs(compute_correlation(feat_idx, n));

        // Compute accuracy of other features combined (leave-one-out proxy)
        double other_corr_sum = 0.0;
        for (size_t f = 0; f < FEATURE_COUNT; ++f) {
            if (f != feat_idx) {
                other_corr_sum += std::abs(compute_correlation(f, n));
            }
        }
        double avg_other = other_corr_sum / (FEATURE_COUNT - 1);

        // Importance = how much this feature adds over average
        return base_corr - avg_other;
    }

    double approximate_shap(size_t feat_idx, size_t n) const noexcept {
        // Marginal contribution: E[f(x) * target | feature_high] - E[f(x) * target | feature_low]
        double sum_high = 0, sum_low = 0;
        int count_high = 0, count_low = 0;

        // Compute median
        std::vector<double> vals(n);
        for (size_t i = 0; i < n; ++i) {
            size_t idx = (feature_head_ - n + i) % HISTORY_SIZE;
            vals[i] = feature_history_[idx][feat_idx];
        }
        std::sort(vals.begin(), vals.end());
        double median = vals[n / 2];

        for (size_t i = 0; i < n; ++i) {
            size_t idx = (feature_head_ - n + i) % HISTORY_SIZE;
            double v = feature_history_[idx][feat_idx];
            double target = move_history_[idx];

            if (v >= median) {
                sum_high += target;
                count_high++;
            } else {
                sum_low += target;
                count_low++;
            }
        }

        double mean_high = count_high > 0 ? sum_high / count_high : 0.0;
        double mean_low = count_low > 0 ? sum_low / count_low : 0.0;

        return mean_high - mean_low;
    }

    // Feature history buffer
    std::array<std::array<double, FEATURE_COUNT>, HISTORY_SIZE> feature_history_{};
    std::array<int, HISTORY_SIZE> target_history_{};
    std::array<double, HISTORY_SIZE> move_history_{};
    size_t feature_head_ = 0;
    size_t sample_count_ = 0;

    FeatureImportanceSnapshot snapshot_;
    bool static_loaded_ = false;  // E3: true if offline importance data was loaded
};

} // namespace bybit
