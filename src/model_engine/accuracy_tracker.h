#pragma once

#include "../config/types.h"
#include "../utils/clock.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace bybit {

// ─── Prediction Outcome ─────────────────────────────────────────────────────
// Records a single prediction for later evaluation against actual price movement.

struct PredictionRecord {
    uint64_t timestamp_ns  = 0;
    uint64_t eval_time_ns  = 0;     // when to evaluate (timestamp + horizon)
    double   mid_price     = 0.0;   // mid price at prediction time
    int      predicted_class = 2;   // 0=up, 1=down, 2=flat
    double   predicted_prob  = 0.0; // probability of predicted class
    int      actual_class    = -1;  // filled when evaluated, -1 = pending
    int      horizon_idx     = 1;   // which horizon (0=100ms, 1=500ms, 2=1s, 3=3s)
    bool     evaluated       = false;
};

// ─── Accuracy Metrics ───────────────────────────────────────────────────────

struct AccuracyMetrics {
    // Overall
    double accuracy          = 0.0;   // correct / total
    int    total_predictions = 0;
    int    correct_predictions = 0;

    // Per-class precision/recall (0=up, 1=down, 2=flat)
    struct ClassMetrics {
        double precision = 0.0;  // TP / (TP + FP)
        double recall    = 0.0;  // TP / (TP + FN)
        double f1_score  = 0.0;  // 2 * P * R / (P + R)
        int    tp = 0, fp = 0, fn = 0;
    };
    std::array<ClassMetrics, 3> per_class; // up, down, flat

    // Rolling window metrics
    double rolling_accuracy  = 0.0;   // accuracy over last N predictions
    int    rolling_window    = 200;
    int    rolling_correct   = 0;
    int    rolling_total     = 0;

    // Per-horizon accuracy
    std::array<double, NUM_HORIZONS> horizon_accuracy = {};
    std::array<int, NUM_HORIZONS>    horizon_total = {};
    std::array<int, NUM_HORIZONS>    horizon_correct = {};

    // Calibration: predicted probability vs actual frequency
    double calibration_error = 0.0;  // mean absolute calibration error

    // Timestamp
    uint64_t last_update_ns = 0;
};

// ─── Model Accuracy Tracker ─────────────────────────────────────────────────
// Real-time evaluation of model prediction quality.
//
// Usage:
//   1. Call record_prediction() after each model inference
//   2. Call evaluate_pending() on each tick with current mid price
//   3. Read metrics() for current accuracy stats
//
// Move thresholds (configurable):
//   - "up"   if price moved > +MOVE_THRESHOLD_BPS
//   - "down" if price moved < -MOVE_THRESHOLD_BPS
//   - "flat" otherwise

class ModelAccuracyTracker {
public:
    static constexpr size_t MAX_PENDING     = 4096;
    static constexpr size_t ROLLING_WINDOW  = 200;
    static constexpr double MOVE_THRESHOLD_BPS = 0.5; // 0.5 bps = significant move

    // Horizon evaluation delays in nanoseconds
    static constexpr uint64_t HORIZON_NS[NUM_HORIZONS] = {
        100'000'000ULL,     // 100ms
        500'000'000ULL,     // 500ms
        1'000'000'000ULL,   // 1s
        3'000'000'000ULL    // 3s
    };

    ModelAccuracyTracker() noexcept {
        rolling_outcomes_.fill(0);
    }

    // Record a new prediction for future evaluation.
    // predicted_class: 0=up, 1=down, 2=flat
    // predicted_prob: confidence of the predicted class
    void record_prediction(uint64_t timestamp_ns, double mid_price,
                           int predicted_class, double predicted_prob,
                           int horizon_idx = 1) noexcept {
        if (pending_count_ >= MAX_PENDING) return; // buffer full

        PredictionRecord& rec = pending_[pending_head_ % MAX_PENDING];
        rec.timestamp_ns = timestamp_ns;
        rec.eval_time_ns = timestamp_ns + HORIZON_NS[std::min(static_cast<size_t>(horizon_idx),
                                                               NUM_HORIZONS - 1)];
        rec.mid_price = mid_price;
        rec.predicted_class = predicted_class;
        rec.predicted_prob = predicted_prob;
        rec.horizon_idx = horizon_idx;
        rec.actual_class = -1;
        rec.evaluated = false;

        ++pending_head_;
        ++pending_count_;
    }

    // Convenience: record from ModelOutput (records 500ms horizon prediction)
    void record_prediction(const ModelOutput& pred, double mid_price) noexcept {
        if (pred.timestamp_ns == 0) return;

        // Determine predicted class from 500ms horizon
        const auto& h = pred.horizons[1]; // 500ms
        int predicted_class = 2; // flat
        double max_prob = h.prob_flat;

        if (h.prob_up > max_prob) {
            predicted_class = 0;
            max_prob = h.prob_up;
        }
        if (h.prob_down > max_prob) {
            predicted_class = 1;
            max_prob = h.prob_down;
        }

        record_prediction(pred.timestamp_ns, mid_price, predicted_class, max_prob, 1);
    }

    // Evaluate pending predictions against current price.
    // Call on every tick.
    void evaluate_pending(uint64_t now_ns, double current_mid) noexcept {
        size_t evaluated = 0;

        for (size_t i = 0; i < pending_count_ && evaluated < 64; ++i) {
            size_t idx = (pending_head_ - pending_count_ + i) % MAX_PENDING;
            PredictionRecord& rec = pending_[idx];

            if (rec.evaluated) continue;
            if (now_ns < rec.eval_time_ns) continue; // not yet time

            // Compute actual price movement
            double move_bps = 0.0;
            if (rec.mid_price > 1e-12) {
                move_bps = ((current_mid - rec.mid_price) / rec.mid_price) * 10000.0;
            }

            // Classify actual movement
            if (move_bps > MOVE_THRESHOLD_BPS) {
                rec.actual_class = 0; // up
            } else if (move_bps < -MOVE_THRESHOLD_BPS) {
                rec.actual_class = 1; // down
            } else {
                rec.actual_class = 2; // flat
            }

            rec.evaluated = true;
            ++evaluated;

            // Update metrics
            update_metrics(rec);
        }

        // Garbage collect evaluated records from the tail
        while (pending_count_ > 0) {
            size_t tail_idx = (pending_head_ - pending_count_) % MAX_PENDING;
            if (!pending_[tail_idx].evaluated) break;
            --pending_count_;
        }

        metrics_.last_update_ns = now_ns;
    }

    const AccuracyMetrics& metrics() const noexcept { return metrics_; }

    // Reset all tracking state
    void reset() noexcept {
        metrics_ = AccuracyMetrics{};
        pending_count_ = 0;
        pending_head_ = 0;
        rolling_head_ = 0;
        rolling_count_ = 0;
        rolling_outcomes_.fill(0);
        calib_bins_.fill({0, 0.0});
    }

private:
    void update_metrics(const PredictionRecord& rec) noexcept {
        int pred = rec.predicted_class;
        int actual = rec.actual_class;
        bool correct = (pred == actual);

        // ── Overall accuracy ──────────────────────────────────────────────
        ++metrics_.total_predictions;
        if (correct) ++metrics_.correct_predictions;
        metrics_.accuracy = static_cast<double>(metrics_.correct_predictions)
                          / metrics_.total_predictions;

        // ── Per-class precision/recall ─────────────────────────────────────
        // For each class c:
        //   TP: predicted c AND actual c
        //   FP: predicted c AND actual != c
        //   FN: predicted != c AND actual c
        for (int c = 0; c < 3; ++c) {
            if (pred == c && actual == c) {
                ++metrics_.per_class[c].tp;
            } else if (pred == c && actual != c) {
                ++metrics_.per_class[c].fp;
            } else if (pred != c && actual == c) {
                ++metrics_.per_class[c].fn;
            }

            auto& cm = metrics_.per_class[c];
            int tp_fp = cm.tp + cm.fp;
            int tp_fn = cm.tp + cm.fn;
            cm.precision = (tp_fp > 0) ? static_cast<double>(cm.tp) / tp_fp : 0.0;
            cm.recall    = (tp_fn > 0) ? static_cast<double>(cm.tp) / tp_fn : 0.0;
            double pr_sum = cm.precision + cm.recall;
            cm.f1_score = (pr_sum > 1e-12) ? 2.0 * cm.precision * cm.recall / pr_sum : 0.0;
        }

        // ── Rolling window accuracy ───────────────────────────────────────
        size_t roll_idx = rolling_head_ % ROLLING_WINDOW;

        // Remove old value if window is full
        if (rolling_count_ >= ROLLING_WINDOW) {
            // The old value at this position is being overwritten
            // We track sum separately
        }

        rolling_outcomes_[roll_idx] = correct ? 1 : 0;
        ++rolling_head_;
        if (rolling_count_ < ROLLING_WINDOW) ++rolling_count_;

        // Recompute rolling accuracy
        int rolling_sum = 0;
        for (size_t i = 0; i < rolling_count_; ++i) {
            rolling_sum += rolling_outcomes_[(rolling_head_ - 1 - i) % ROLLING_WINDOW];
        }
        metrics_.rolling_correct = rolling_sum;
        metrics_.rolling_total = static_cast<int>(rolling_count_);
        metrics_.rolling_accuracy = (rolling_count_ > 0)
            ? static_cast<double>(rolling_sum) / rolling_count_ : 0.0;

        // ── Per-horizon accuracy ──────────────────────────────────────────
        int h = rec.horizon_idx;
        if (h >= 0 && h < static_cast<int>(NUM_HORIZONS)) {
            ++metrics_.horizon_total[h];
            if (correct) ++metrics_.horizon_correct[h];
            metrics_.horizon_accuracy[h] = static_cast<double>(metrics_.horizon_correct[h])
                                         / metrics_.horizon_total[h];
        }

        // ── Calibration error ─────────────────────────────────────────────
        // Bin predicted probabilities and track actual hit rate
        int bin = static_cast<int>(rec.predicted_prob * NUM_CALIB_BINS);
        bin = std::clamp(bin, 0, static_cast<int>(NUM_CALIB_BINS - 1));
        ++calib_bins_[bin].count;
        calib_bins_[bin].actual_sum += correct ? 1.0 : 0.0;

        // Recompute mean calibration error
        double calib_sum = 0.0;
        int calib_n = 0;
        for (size_t b = 0; b < NUM_CALIB_BINS; ++b) {
            if (calib_bins_[b].count > 0) {
                double expected_prob = (b + 0.5) / NUM_CALIB_BINS;
                double actual_freq = calib_bins_[b].actual_sum / calib_bins_[b].count;
                calib_sum += std::abs(expected_prob - actual_freq);
                ++calib_n;
            }
        }
        metrics_.calibration_error = (calib_n > 0) ? calib_sum / calib_n : 0.0;
    }

    AccuracyMetrics metrics_;

    // Pending predictions ring buffer
    std::array<PredictionRecord, MAX_PENDING> pending_{};
    size_t pending_head_  = 0;
    size_t pending_count_ = 0;

    // Rolling window
    std::array<int, ROLLING_WINDOW> rolling_outcomes_{};
    size_t rolling_head_  = 0;
    size_t rolling_count_ = 0;

    // Calibration bins
    static constexpr size_t NUM_CALIB_BINS = 10;
    struct CalibBin {
        int count = 0;
        double actual_sum = 0.0;
    };
    std::array<CalibBin, NUM_CALIB_BINS> calib_bins_{};
};

} // namespace bybit
