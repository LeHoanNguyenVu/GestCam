#include "gestcam/OnnxDirectMLEngine.h"

#include <dml_provider_factory.h>

#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <windows.h>

namespace gestcam {

namespace {
std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int num_chars = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
    if (num_chars <= 0) return std::wstring();
    std::wstring result(num_chars, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &result[0], num_chars);
    return result;
}
} // namespace

OnnxDirectMLEngine::OnnxDirectMLEngine() {
    try {
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "GestCamDirectML");
        memory_info_ = std::make_unique<Ort::MemoryInfo>(
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)
        );
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to initialize Ort::Env: ") + e.what();
    }
}

OnnxDirectMLEngine::~OnnxDirectMLEngine() = default;

OnnxDirectMLEngine::OnnxDirectMLEngine(OnnxDirectMLEngine&&) noexcept = default;
OnnxDirectMLEngine& OnnxDirectMLEngine::operator=(OnnxDirectMLEngine&&) noexcept = default;

void OnnxDirectMLEngine::Reset() {
    input_tensors_.clear();
    input_buffers_.clear();
    input_shapes_.clear();
    input_names_.clear();
    input_names_ptrs_.clear();

    output_tensors_.clear();
    output_names_.clear();
    output_names_ptrs_.clear();

    session_.reset();
    session_options_.reset();
    is_dml_enabled_ = false;
    last_latency_ms_ = 0.0f;
    last_error_.clear();
}

bool OnnxDirectMLEngine::LoadModel(const std::string& model_path, int device_id, bool enable_dml) {
    return LoadModel(Utf8ToWide(model_path), device_id, enable_dml);
}

bool OnnxDirectMLEngine::LoadModel(const std::wstring& model_path, int device_id, bool enable_dml) {
    return SetupSessionAndTensors(model_path, device_id, enable_dml);
}

bool OnnxDirectMLEngine::SetupSessionAndTensors(const std::wstring& model_path, int device_id, bool enable_dml) {
    Reset();

    if (!env_ || !memory_info_) {
        last_error_ = "Ort::Env or MemoryInfo is uninitialized!";
        return false;
    }

    try {
        session_options_ = std::make_unique<Ort::SessionOptions>();
        session_options_->SetIntraOpNumThreads(1);
        session_options_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        if (enable_dml) {
            OrtStatus* dml_status = OrtSessionOptionsAppendExecutionProvider_DML(*session_options_, device_id);
            if (dml_status != nullptr) {
                const char* msg = Ort::GetApi().GetErrorMessage(dml_status);
                last_error_ = std::string("DirectML warning (falling back to CPU): ") + (msg ? msg : "unknown");
                std::cerr << "[OnnxDirectMLEngine] " << last_error_ << std::endl;
                Ort::GetApi().ReleaseStatus(dml_status);
                is_dml_enabled_ = false;
            } else {
                is_dml_enabled_ = true;
            }
        }

        try {
            session_ = std::make_unique<Ort::Session>(*env_, model_path.c_str(), *session_options_);
        } catch (const Ort::Exception& e) {
            if (is_dml_enabled_) {
                std::cerr << "[OnnxDirectMLEngine] DirectML session creation failed: " << e.what()
                          << ". Retrying with CPU execution provider..." << std::endl;
                session_options_ = std::make_unique<Ort::SessionOptions>();
                session_options_->SetIntraOpNumThreads(2);
                session_options_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
                is_dml_enabled_ = false;
                session_ = std::make_unique<Ort::Session>(*env_, model_path.c_str(), *session_options_);
            } else {
                throw;
            }
        }

        Ort::AllocatorWithDefaultOptions allocator;

        // 1. Inputs metadata and pre-allocated static tensors
        size_t num_inputs = session_->GetInputCount();
        input_names_.reserve(num_inputs);
        input_names_ptrs_.reserve(num_inputs);
        input_shapes_.reserve(num_inputs);
        input_buffers_.reserve(num_inputs);
        input_tensors_.reserve(num_inputs);

        for (size_t i = 0; i < num_inputs; ++i) {
            auto name_ptr = session_->GetInputNameAllocated(i, allocator);
            input_names_.push_back(name_ptr.get());

            auto type_info = session_->GetInputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            std::vector<int64_t> shape = tensor_info.GetShape();

            // Handle dynamic batch dimension (-1 or 0 -> 1)
            for (auto& dim : shape) {
                if (dim <= 0) dim = 1;
            }

            int64_t total_elements = 1;
            for (auto dim : shape) {
                total_elements *= dim;
            }

            input_shapes_.push_back(shape);
            input_buffers_.emplace_back(total_elements, 0.0f);
        }

        for (const auto& name : input_names_) {
            input_names_ptrs_.push_back(name.c_str());
        }

        // Create pre-allocated Ort::Value tensors pointing to static input_buffers_
        for (size_t i = 0; i < num_inputs; ++i) {
            Ort::Value tensor = Ort::Value::CreateTensor<float>(
                *memory_info_,
                input_buffers_[i].data(),
                input_buffers_[i].size(),
                input_shapes_[i].data(),
                input_shapes_[i].size()
            );
            input_tensors_.push_back(std::move(tensor));
        }

        // 2. Outputs metadata
        size_t num_outputs = session_->GetOutputCount();
        output_names_.reserve(num_outputs);
        output_names_ptrs_.reserve(num_outputs);

        for (size_t i = 0; i < num_outputs; ++i) {
            auto name_ptr = session_->GetOutputNameAllocated(i, allocator);
            output_names_.push_back(name_ptr.get());
        }

        for (const auto& name : output_names_) {
            output_names_ptrs_.push_back(name.c_str());
        }

        return true;
    } catch (const std::exception& e) {
        std::string err = std::string("Model loading failed: ") + e.what();
        Reset();
        last_error_ = err;
        return false;
    }
}

std::string OnnxDirectMLEngine::GetExecutionProviderName() const {
    return is_dml_enabled_ ? "DirectML" : "CPU";
}

const std::vector<int64_t>& OnnxDirectMLEngine::GetInputShape(size_t index) const {
    if (index >= input_shapes_.size()) {
        throw std::out_of_range("Input shape index out of range");
    }
    return input_shapes_[index];
}

size_t OnnxDirectMLEngine::GetInputElementsCount(size_t index) const {
    if (index >= input_buffers_.size()) {
        throw std::out_of_range("Input buffer index out of range");
    }
    return input_buffers_[index].size();
}

float* OnnxDirectMLEngine::GetInputBuffer(size_t index) {
    if (index >= input_buffers_.size()) {
        throw std::out_of_range("Input buffer index out of range");
    }
    return input_buffers_[index].data();
}

const float* OnnxDirectMLEngine::GetInputBuffer(size_t index) const {
    if (index >= input_buffers_.size()) {
        throw std::out_of_range("Input buffer index out of range");
    }
    return input_buffers_[index].data();
}

size_t OnnxDirectMLEngine::GetInputBufferSize(size_t index) const {
    if (index >= input_buffers_.size()) {
        throw std::out_of_range("Input buffer index out of range");
    }
    return input_buffers_[index].size();
}

bool OnnxDirectMLEngine::SetInputData(const float* data, size_t count, size_t index) {
    if (!data || index >= input_buffers_.size()) return false;
    if (count > input_buffers_[index].size()) return false;
    std::copy(data, data + count, input_buffers_[index].data());
    return true;
}

bool OnnxDirectMLEngine::Run() {
    if (!session_ || input_tensors_.empty()) {
        last_error_ = "Engine session not initialized or missing inputs!";
        return false;
    }

    try {
        auto start = std::chrono::high_resolution_clock::now();

        output_tensors_ = session_->Run(
            Ort::RunOptions{nullptr},
            input_names_ptrs_.data(),
            input_tensors_.data(),
            input_tensors_.size(),
            output_names_ptrs_.data(),
            output_names_ptrs_.size()
        );

        auto end = std::chrono::high_resolution_clock::now();
        last_latency_ms_ = std::chrono::duration<float, std::milli>(end - start).count();

        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Inference failed: ") + e.what();
        return false;
    }
}

const Ort::Value& OnnxDirectMLEngine::GetOutput(size_t index) const {
    if (index >= output_tensors_.size()) {
        throw std::out_of_range("Output tensor index out of range");
    }
    return output_tensors_[index];
}

const float* OnnxDirectMLEngine::GetOutputData(size_t index) const {
    return GetOutput(index).GetTensorData<float>();
}

std::vector<int64_t> OnnxDirectMLEngine::GetOutputShape(size_t index) const {
    return GetOutput(index).GetTensorTypeAndShapeInfo().GetShape();
}

} // namespace gestcam
