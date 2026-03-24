#pragma once

#include "../config/types.h"
#include "../feature_engine/advanced_feature_engine.h"
#include "../utils/clock.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
#include <memory>

// ─── ONNX Runtime C API ──────────────────────────────────────────────────────
// Forward-declare ONNX Runtime types to avoid header dependency when not available.
// At link time, onnxruntime must be available.

#ifdef HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace bybit {

// ─── ONNX Inference Engine ──────────────────────────────────────────────────
// High-performance ONNX Runtime wrapper for LSTM/GRU model inference.
//
// Expected ONNX model interface:
//   Input:  "features"  shape [1, seq_len, FEATURE_COUNT]  float32
//   Output: "probs"     shape [1, NUM_HORIZONS * 3]        float32
//           (up, down, flat per horizon, softmax-normalized)
//
// Inference target: < 1ms for 100-step sequence with 25 features.

class OnnxInferenceEngine {
public:
    OnnxInferenceEngine() noexcept = default;

    ~OnnxInferenceEngine() { unload(); }

    // Non-copyable
    OnnxInferenceEngine(const OnnxInferenceEngine&) = delete;
    OnnxInferenceEngine& operator=(const OnnxInferenceEngine&) = delete;

    enum class InferenceBackend : uint8_t {
        CPU = 0,
        CUDA = 1,
        TensorRT = 2,
        CoreML = 3,
    };

    // Load ONNX model from file. Returns true on success.
    // Automatically tries GPU backends (CUDA > TensorRT > CoreML) then falls back to CPU.
    bool load(const std::string& model_path, int intra_threads = 1,
              bool prefer_gpu = true) noexcept {
#ifdef HAS_ONNXRUNTIME
        try {
            env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "Lynrix");

            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(intra_threads);
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            opts.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

            // Disable memory pattern for deterministic latency
            opts.DisableMemPattern();

            // Try GPU providers in order of preference
            backend_ = InferenceBackend::CPU;
            if (prefer_gpu) {
                backend_ = try_gpu_providers(opts);
            }

            session_ = std::make_unique<Ort::Session>(*env_, model_path.c_str(), opts);
            allocator_ = std::make_unique<Ort::AllocatorWithDefaultOptions>();

            // Validate input/output shapes
            if (!validate_model()) {
                unload();
                return false;
            }

            model_path_ = model_path;
            loaded_ = true;

            // Pre-allocate input buffer to avoid heap allocation in hot path
            input_buffer_.resize(FEATURE_SEQ_LEN * FEATURE_COUNT, 0.0f);

            return true;
        } catch (const Ort::Exception& e) {
            last_error_ = e.what();
            // If GPU load failed, retry with CPU
            if (backend_ != InferenceBackend::CPU) {
                backend_ = InferenceBackend::CPU;
                return load(model_path, intra_threads, false);
            }
            return false;
        } catch (...) {
            last_error_ = "Unknown error loading ONNX model";
            return false;
        }
#else
        (void)model_path;
        (void)intra_threads;
        (void)prefer_gpu;
        last_error_ = "ONNX Runtime not available (compiled without HAS_ONNXRUNTIME)";
        return false;
#endif
    }

    void unload() noexcept {
#ifdef HAS_ONNXRUNTIME
        session_.reset();
        env_.reset();
        allocator_.reset();
#endif
        loaded_ = false;
    }

    // Run inference on feature sequence from ring buffer.
    // Returns ModelOutput with multi-horizon 3-class predictions.
    ModelOutput predict(const FeatureRingBuffer& history) noexcept {
        uint64_t start_ns = Clock::now_ns();
        ModelOutput out;

        if (!loaded_ || history.size() < 10) {
            out.timestamp_ns = start_ns;
            return out;
        }

#ifdef HAS_ONNXRUNTIME
        try {
            // Prepare input tensor: [1, seq_len, FEATURE_COUNT]
            size_t seq_len = std::min(history.size(), FEATURE_SEQ_LEN);

            // Fill feature buffer (float32 for ONNX) — pre-allocated, no heap alloc
            {
                alignas(64) double raw[FEATURE_SEQ_LEN * FEATURE_COUNT];
                history.fill_sequence(raw, seq_len);
                normalize_sequence(raw, seq_len);
                for (size_t i = 0; i < seq_len * FEATURE_COUNT; ++i) {
                    input_buffer_[i] = static_cast<float>(raw[i]);
                }
            }

            // Create input tensor from pre-allocated buffer
            std::array<int64_t, 3> input_shape = {1, static_cast<int64_t>(seq_len),
                                                    static_cast<int64_t>(FEATURE_COUNT)};
            auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                memory_info, input_buffer_.data(), seq_len * FEATURE_COUNT,
                input_shape.data(), input_shape.size());

            // Run inference
            const char* input_names[] = {"features"};
            const char* output_names[] = {"probs"};

            auto outputs = session_->Run(
                Ort::RunOptions{nullptr},
                input_names, &input_tensor, 1,
                output_names, 1);

            // Parse output: [1, NUM_HORIZONS * 3]
            const float* probs = outputs[0].GetTensorData<float>();
            size_t output_size = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();

            if (output_size >= NUM_HORIZONS * 3) {
                for (size_t h = 0; h < NUM_HORIZONS; ++h) {
                    auto& hp = out.horizons[h];
                    hp.prob_up   = static_cast<double>(probs[h * 3 + 0]);
                    hp.prob_down = static_cast<double>(probs[h * 3 + 1]);
                    hp.prob_flat = static_cast<double>(probs[h * 3 + 2]);
                    hp.predicted_move_bps = (hp.prob_up - hp.prob_down) * 10.0;
                }

                // Primary prediction: 500ms horizon (index 1)
                out.probability_up   = out.horizons[1].prob_up;
                out.probability_down = out.horizons[1].prob_down;

                // Model confidence
                double max_prob = std::max({out.probability_up, out.probability_down,
                                            out.horizons[1].prob_flat});
                out.model_confidence = std::clamp((max_prob - 0.333) / 0.667, 0.0, 1.0);
            }

        } catch (const Ort::Exception& e) {
            last_error_ = e.what();
            ++inference_errors_;
        } catch (...) {
            ++inference_errors_;
        }
#endif

        uint64_t end_ns = Clock::now_ns();
        out.inference_latency_ns = end_ns - start_ns;
        out.timestamp_ns = end_ns;
        ++inference_count_;

        return out;
    }

    // Set normalization statistics (mean/std from training data)
    void set_normalization(const std::array<double, FEATURE_COUNT>& mean,
                          const std::array<double, FEATURE_COUNT>& std_dev) noexcept {
        feat_mean_ = mean;
        feat_std_ = std_dev;
    }

    bool loaded() const noexcept { return loaded_; }
    const std::string& model_path() const noexcept { return model_path_; }
    const std::string& last_error() const noexcept { return last_error_; }
    uint64_t inference_count() const noexcept { return inference_count_; }
    uint64_t inference_errors() const noexcept { return inference_errors_; }
    InferenceBackend backend() const noexcept { return backend_; }
    bool using_gpu() const noexcept { return backend_ != InferenceBackend::CPU; }

    static const char* backend_name(InferenceBackend b) noexcept {
        switch (b) {
            case InferenceBackend::CPU:      return "CPU";
            case InferenceBackend::CUDA:     return "CUDA";
            case InferenceBackend::TensorRT: return "TensorRT";
            case InferenceBackend::CoreML:   return "CoreML";
        }
        return "Unknown";
    }

private:
    void normalize_sequence(double* seq, size_t seq_len) const noexcept {
        for (size_t t = 0; t < seq_len; ++t) {
            for (size_t f = 0; f < FEATURE_COUNT; ++f) {
                double& val = seq[t * FEATURE_COUNT + f];
                double s = feat_std_[f];
                if (s > 1e-12) {
                    val = (val - feat_mean_[f]) / s;
                }
                val = std::clamp(val, -5.0, 5.0);
            }
        }
    }

#ifdef HAS_ONNXRUNTIME
    InferenceBackend try_gpu_providers(Ort::SessionOptions& opts) {
        // Try CUDA first
#ifdef USE_CUDA
        try {
            OrtCUDAProviderOptions cuda_opts{};
            cuda_opts.device_id = 0;
            cuda_opts.arena_extend_strategy = 0;
            cuda_opts.gpu_mem_limit = 512 * 1024 * 1024; // 512MB
            cuda_opts.cudnn_conv_algo_search = OrtCudnnDefaultConvAlgoSearch;
            opts.AppendExecutionProvider_CUDA(cuda_opts);
            return InferenceBackend::CUDA;
        } catch (...) {}

        // Try TensorRT
        try {
            OrtTensorRTProviderOptions trt_opts{};
            trt_opts.device_id = 0;
            trt_opts.trt_fp16_enable = 1;
            opts.AppendExecutionProvider_TensorRT(trt_opts);
            return InferenceBackend::TensorRT;
        } catch (...) {}
#endif

        // Try CoreML on macOS
#ifdef __APPLE__
        try {
            // CoreML EP available in ONNX Runtime 1.13+
            uint32_t coreml_flags = 0;
            opts.AppendExecutionProvider("CoreML", {});
            return InferenceBackend::CoreML;
        } catch (...) {}
#endif

        return InferenceBackend::CPU;
    }

    bool validate_model() {
        if (!session_) return false;

        // Check input count and name
        size_t num_inputs = session_->GetInputCount();
        if (num_inputs < 1) {
            last_error_ = "Model must have at least 1 input";
            return false;
        }

        // Check output count
        size_t num_outputs = session_->GetOutputCount();
        if (num_outputs < 1) {
            last_error_ = "Model must have at least 1 output";
            return false;
        }

        // Validate output shape contains enough elements for NUM_HORIZONS * 3
        auto output_info = session_->GetOutputTypeInfo(0);
        auto tensor_info = output_info.GetTensorTypeAndShapeInfo();
        auto shape = tensor_info.GetShape();

        // Output should be [1, NUM_HORIZONS*3] or [1, 12]
        int64_t total = 1;
        for (auto s : shape) {
            if (s > 0) total *= s;
        }
        if (total < static_cast<int64_t>(NUM_HORIZONS * 3)) {
            last_error_ = "Output tensor too small for " +
                          std::to_string(NUM_HORIZONS) + " horizons × 3 classes";
            return false;
        }

        return true;
    }

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::unique_ptr<Ort::AllocatorWithDefaultOptions> allocator_;
#endif

    std::string model_path_;
    std::string last_error_;
    bool loaded_ = false;
    InferenceBackend backend_ = InferenceBackend::CPU;

    std::array<double, FEATURE_COUNT> feat_mean_{};
    std::array<double, FEATURE_COUNT> feat_std_{};

    // Pre-allocated input buffer (avoids heap allocation in hot path)
    std::vector<float> input_buffer_;

    uint64_t inference_count_ = 0;
    uint64_t inference_errors_ = 0;
};

} // namespace bybit
