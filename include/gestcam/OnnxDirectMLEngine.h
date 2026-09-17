#pragma once

#include "gestcam/SalCompat.h"

#include <vector>
#include <string>
#include <memory>
#include <cstdint>

#include <onnxruntime_cxx_api.h>

namespace gestcam {

/**
 * @brief Zero-allocation ONNX Runtime Engine with DirectML hardware acceleration.
 *
 * Manages Ort::Env, Ort::SessionOptions, and Ort::Session lifecycle.
 * Automatically enables DirectML execution provider on iGPU / dGPU,
 * pre-allocates static input tensors at initialization, and eliminates
 * dynamic malloc / memory allocations inside the real-time frame loop.
 */
class OnnxDirectMLEngine {
public:
    OnnxDirectMLEngine();
    ~OnnxDirectMLEngine();

    // Non-copyable, movable
    OnnxDirectMLEngine(const OnnxDirectMLEngine&) = delete;
    OnnxDirectMLEngine& operator=(const OnnxDirectMLEngine&) = delete;
    OnnxDirectMLEngine(OnnxDirectMLEngine&&) noexcept;
    OnnxDirectMLEngine& operator=(OnnxDirectMLEngine&&) noexcept;

    /**
     * @brief Loads an ONNX model and initializes DirectML session.
     * @param model_path Path to the ONNX model (UTF-8 string or wide string).
     * @param device_id GPU adapter index (default 0 for primary GPU).
     * @param enable_dml Whether to attempt enabling DirectML (fallback to CPU if false or fails).
     * @return true if session and pre-allocated tensors initialized successfully.
     */
    bool LoadModel(const std::string& model_path, int device_id = 0, bool enable_dml = true);
    bool LoadModel(const std::wstring& model_path, int device_id = 0, bool enable_dml = true);

    /**
     * @brief Checks whether DirectML execution provider is currently active.
     */
    bool IsDirectMLEnabled() const noexcept { return is_dml_enabled_; }

    /**
     * @brief Returns active execution provider name ("DirectML" or "CPU").
     */
    std::string GetExecutionProviderName() const;

    // Metadata Queries
    size_t GetInputCount() const noexcept { return input_names_.size(); }
    size_t GetOutputCount() const noexcept { return output_names_.size(); }
    const std::vector<std::string>& GetInputNames() const noexcept { return input_names_; }
    const std::vector<std::string>& GetOutputNames() const noexcept { return output_names_; }
    const std::vector<int64_t>& GetInputShape(size_t index = 0) const;
    size_t GetInputElementsCount(size_t index = 0) const;

    // Zero-Allocation Input Buffer Access
    /**
     * @brief Returns raw pointer to pre-allocated static input buffer.
     * Caller can copy normalized pixel data directly into this buffer.
     */
    float* GetInputBuffer(size_t index = 0);
    const float* GetInputBuffer(size_t index = 0) const;
    size_t GetInputBufferSize(size_t index = 0) const;

    /**
     * @brief Convenience copy method to set input data from array.
     */
    bool SetInputData(const float* data, size_t count, size_t index = 0);

    // Inference Execution
    /**
     * @brief Executes model inference on GPU/DirectML using pre-allocated inputs.
     * @return true if inference succeeded.
     */
    bool Run();

    // Output Access
    const std::vector<Ort::Value>& GetOutputs() const noexcept { return output_tensors_; }
    const Ort::Value& GetOutput(size_t index) const;
    const float* GetOutputData(size_t index) const;
    std::vector<int64_t> GetOutputShape(size_t index) const;

    // Performance & Diagnostics
    float GetLastLatencyMs() const noexcept { return last_latency_ms_; }
    const std::string& GetLastError() const noexcept { return last_error_; }

private:
    bool SetupSessionAndTensors(const std::wstring& model_path, int device_id, bool enable_dml);
    void Reset();

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::SessionOptions> session_options_;
    std::unique_ptr<Ort::Session> session_;
    std::unique_ptr<Ort::MemoryInfo> memory_info_;

    // Input metadata & pre-allocated static tensors
    std::vector<std::string> input_names_;
    std::vector<const char*> input_names_ptrs_;
    std::vector<std::vector<int64_t>> input_shapes_;
    std::vector<std::vector<float>> input_buffers_;
    std::vector<Ort::Value> input_tensors_;

    // Output metadata & results
    std::vector<std::string> output_names_;
    std::vector<const char*> output_names_ptrs_;
    std::vector<Ort::Value> output_tensors_;

    bool is_dml_enabled_ = false;
    float last_latency_ms_ = 0.0f;
    std::string last_error_;
};

} // namespace gestcam
