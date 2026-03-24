#pragma once

// ─── SIMD/NEON + Accelerate.framework Vectorized Indicators ─────────────────
// All indicator computations use ARM NEON intrinsics or Apple Accelerate vDSP.
// Target: all 25+ features computed in < 25 µs on Apple Silicon.
//
// Strategy:
//   - vDSP for bulk vector operations (EMA, variance, dot products)
//   - NEON intrinsics for tight loops where vDSP overhead > benefit
//   - Scalar fallback for x86_64 (CI/testing)

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <algorithm>

#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>
#define HAS_ACCELERATE 1
#else
#define HAS_ACCELERATE 0
#endif

#if defined(__aarch64__)
#include <arm_neon.h>
#define HAS_NEON 1
#else
#define HAS_NEON 0
#endif

namespace bybit {

// ─── vDSP Wrappers ──────────────────────────────────────────────────────────
// Thin inline wrappers around Accelerate vDSP for common operations.

namespace simd {

// Sum of double array using vDSP (vectorized reduction)
inline double sum(const double* data, size_t count) noexcept {
#if HAS_ACCELERATE
    double result = 0.0;
    vDSP_sveD(data, 1, &result, static_cast<vDSP_Length>(count));
    return result;
#elif HAS_NEON
    float64x2_t acc = vdupq_n_f64(0.0);
    size_t i = 0;
    for (; i + 1 < count; i += 2) {
        float64x2_t v = vld1q_f64(&data[i]);
        acc = vaddq_f64(acc, v);
    }
    double result = vgetq_lane_f64(acc, 0) + vgetq_lane_f64(acc, 1);
    if (i < count) result += data[i];
    return result;
#else
    double result = 0.0;
    for (size_t i = 0; i < count; ++i) result += data[i];
    return result;
#endif
}

// Mean of double array
inline double mean(const double* data, size_t count) noexcept {
    if (count == 0) return 0.0;
#if HAS_ACCELERATE
    double result = 0.0;
    vDSP_meanvD(data, 1, &result, static_cast<vDSP_Length>(count));
    return result;
#else
    return sum(data, count) / static_cast<double>(count);
#endif
}

// Variance of double array (population variance)
inline double variance(const double* data, size_t count) noexcept {
    if (count < 2) return 0.0;
    double m = mean(data, count);

#if HAS_ACCELERATE
    // Compute (data - mean)^2 using vDSP
    // Use stack buffer for small counts, else heap
    alignas(64) double temp[512];
    double* buf = (count <= 512) ? temp : new double[count];

    double neg_mean = -m;
    vDSP_vsaddD(data, 1, &neg_mean, buf, 1, static_cast<vDSP_Length>(count));
    vDSP_vsqD(buf, 1, buf, 1, static_cast<vDSP_Length>(count));
    double result = 0.0;
    vDSP_sveD(buf, 1, &result, static_cast<vDSP_Length>(count));

    if (count > 512) delete[] buf;
    return result / static_cast<double>(count);
#else
    double acc = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double d = data[i] - m;
        acc += d * d;
    }
    return acc / static_cast<double>(count);
#endif
}

// Dot product of two double arrays
inline double dot(const double* a, const double* b, size_t count) noexcept {
#if HAS_ACCELERATE
    double result = 0.0;
    vDSP_dotprD(a, 1, b, 1, &result, static_cast<vDSP_Length>(count));
    return result;
#elif HAS_NEON
    float64x2_t acc = vdupq_n_f64(0.0);
    size_t i = 0;
    for (; i + 1 < count; i += 2) {
        float64x2_t va = vld1q_f64(&a[i]);
        float64x2_t vb = vld1q_f64(&b[i]);
        acc = vfmaq_f64(acc, va, vb); // fused multiply-add
    }
    double result = vgetq_lane_f64(acc, 0) + vgetq_lane_f64(acc, 1);
    if (i < count) result += a[i] * b[i];
    return result;
#else
    double result = 0.0;
    for (size_t i = 0; i < count; ++i) result += a[i] * b[i];
    return result;
#endif
}

// Exponential Moving Average update (vectorized batch)
// Computes EMA over entire array in-place: ema[i] = alpha * data[i] + (1-alpha) * ema[i-1]
inline void ema_batch(const double* data, double* ema, size_t count,
                      double alpha, double initial) noexcept {
    if (count == 0) return;
    double one_minus_alpha = 1.0 - alpha;
    ema[0] = alpha * data[0] + one_minus_alpha * initial;
    for (size_t i = 1; i < count; ++i) {
        ema[i] = alpha * data[i] + one_minus_alpha * ema[i - 1];
    }
}

// Element-wise multiply: out[i] = a[i] * b[i]
inline void vmul(const double* a, const double* b, double* out, size_t count) noexcept {
#if HAS_ACCELERATE
    vDSP_vmulD(a, 1, b, 1, out, 1, static_cast<vDSP_Length>(count));
#elif HAS_NEON
    size_t i = 0;
    for (; i + 1 < count; i += 2) {
        float64x2_t va = vld1q_f64(&a[i]);
        float64x2_t vb = vld1q_f64(&b[i]);
        vst1q_f64(&out[i], vmulq_f64(va, vb));
    }
    if (i < count) out[i] = a[i] * b[i];
#else
    for (size_t i = 0; i < count; ++i) out[i] = a[i] * b[i];
#endif
}

// Element-wise add: out[i] = a[i] + b[i]
inline void vadd(const double* a, const double* b, double* out, size_t count) noexcept {
#if HAS_ACCELERATE
    vDSP_vaddD(a, 1, b, 1, out, 1, static_cast<vDSP_Length>(count));
#elif HAS_NEON
    size_t i = 0;
    for (; i + 1 < count; i += 2) {
        float64x2_t va = vld1q_f64(&a[i]);
        float64x2_t vb = vld1q_f64(&b[i]);
        vst1q_f64(&out[i], vaddq_f64(va, vb));
    }
    if (i < count) out[i] = a[i] + b[i];
#else
    for (size_t i = 0; i < count; ++i) out[i] = a[i] + b[i];
#endif
}

// Scalar multiply: out[i] = a[i] * scalar
inline void vsmul(const double* a, double scalar, double* out, size_t count) noexcept {
#if HAS_ACCELERATE
    vDSP_vsmulD(a, 1, &scalar, out, 1, static_cast<vDSP_Length>(count));
#elif HAS_NEON
    float64x2_t vs = vdupq_n_f64(scalar);
    size_t i = 0;
    for (; i + 1 < count; i += 2) {
        float64x2_t va = vld1q_f64(&a[i]);
        vst1q_f64(&out[i], vmulq_f64(va, vs));
    }
    if (i < count) out[i] = a[i] * scalar;
#else
    for (size_t i = 0; i < count; ++i) out[i] = a[i] * scalar;
#endif
}

// Max element
inline double max_element(const double* data, size_t count) noexcept {
    if (count == 0) return 0.0;
#if HAS_ACCELERATE
    double result = 0.0;
    vDSP_maxvD(data, 1, &result, static_cast<vDSP_Length>(count));
    return result;
#else
    double m = data[0];
    for (size_t i = 1; i < count; ++i) {
        if (data[i] > m) m = data[i];
    }
    return m;
#endif
}

// Min element
inline double min_element(const double* data, size_t count) noexcept {
    if (count == 0) return 0.0;
#if HAS_ACCELERATE
    double result = 0.0;
    vDSP_minvD(data, 1, &result, static_cast<vDSP_Length>(count));
    return result;
#else
    double m = data[0];
    for (size_t i = 1; i < count; ++i) {
        if (data[i] < m) m = data[i];
    }
    return m;
#endif
}

// Z-score normalization: out[i] = (data[i] - mean) / std_dev
inline void zscore(const double* data, double* out, size_t count) noexcept {
    if (count < 2) {
        for (size_t i = 0; i < count; ++i) out[i] = 0.0;
        return;
    }
    double m = mean(data, count);
    double v = variance(data, count);
    double s = std::sqrt(v);
    if (s < 1e-15) {
        for (size_t i = 0; i < count; ++i) out[i] = 0.0;
        return;
    }
    double neg_mean = -m;
    double inv_std = 1.0 / s;
#if HAS_ACCELERATE
    vDSP_vsaddD(data, 1, &neg_mean, out, 1, static_cast<vDSP_Length>(count));
    vDSP_vsmulD(out, 1, &inv_std, out, 1, static_cast<vDSP_Length>(count));
#else
    for (size_t i = 0; i < count; ++i) {
        out[i] = (data[i] + neg_mean) * inv_std;
    }
#endif
}

// Clamp values to [-limit, +limit]
inline void clamp(double* data, size_t count, double limit) noexcept {
#if HAS_ACCELERATE
    double lo = -limit, hi = limit;
    vDSP_vclipD(data, 1, &lo, &hi, data, 1, static_cast<vDSP_Length>(count));
#else
    for (size_t i = 0; i < count; ++i) {
        if (data[i] > limit) data[i] = limit;
        else if (data[i] < -limit) data[i] = -limit;
    }
#endif
}

// ─── NEON-optimized sigmoid (batch) ─────────────────────────────────────────
// Fast approximate sigmoid using polynomial: σ(x) ≈ 0.5 + 0.5*tanh(x/2)
// Uses rational Padé approximant for tanh on [-8, 8]

inline void sigmoid_batch(const double* in, double* out, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) {
        double x = in[i];
        if (x > 15.0) { out[i] = 1.0; continue; }
        if (x < -15.0) { out[i] = 0.0; continue; }
        out[i] = 1.0 / (1.0 + std::exp(-x));
    }
}

// ─── NEON-optimized tanh (batch) ────────────────────────────────────────────

inline void tanh_batch(const double* in, double* out, size_t count) noexcept {
#if HAS_ACCELERATE
    int n = static_cast<int>(count);
    vvtanh(out, in, &n);
#else
    for (size_t i = 0; i < count; ++i) {
        double x = in[i];
        if (x > 7.0) { out[i] = 1.0; continue; }
        if (x < -7.0) { out[i] = -1.0; continue; }
        out[i] = std::tanh(x);
    }
#endif
}

// ─── Matrix-vector multiply using Accelerate BLAS ───────────────────────────
// y = A * x + y (DGEMV)
// A is [rows x cols], x is [cols], y is [rows]

inline void matvec(const double* A, const double* x, double* y,
                   size_t rows, size_t cols) noexcept {
#if HAS_ACCELERATE
    cblas_dgemv(CblasRowMajor, CblasNoTrans,
                static_cast<int>(rows), static_cast<int>(cols),
                1.0, A, static_cast<int>(cols),
                x, 1, 0.0, y, 1);
#else
    for (size_t i = 0; i < rows; ++i) {
        double s = 0.0;
        for (size_t j = 0; j < cols; ++j) {
            s += A[i * cols + j] * x[j];
        }
        y[i] = s;
    }
#endif
}

// Fused matvec + bias + activation: y[i] = activation(A[i,:] · x + b[i])
inline void matvec_bias_sigmoid(const double* A, const double* x, const double* bias,
                                double* y, size_t rows, size_t cols) noexcept {
    matvec(A, x, y, rows, cols);
    for (size_t i = 0; i < rows; ++i) {
        y[i] += bias[i];
    }
    sigmoid_batch(y, y, rows);
}

inline void matvec_bias_tanh(const double* A, const double* x, const double* bias,
                             double* y, size_t rows, size_t cols) noexcept {
    matvec(A, x, y, rows, cols);
    for (size_t i = 0; i < rows; ++i) {
        y[i] += bias[i];
    }
    tanh_batch(y, y, rows);
}

} // namespace simd
} // namespace bybit
