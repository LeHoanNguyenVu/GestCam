#include <gtest/gtest.h>
#include "gestcam/OnnxDirectMLEngine.h"

#include <filesystem>
#include <vector>
#include <numeric>

namespace fs = std::filesystem;

class OnnxDirectMLEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Find models directory relative to source root or binary
        if (fs::exists("models/ultraface_int8.onnx")) {
            models_dir_ = "models";
        } else if (fs::exists("../models/ultraface_int8.onnx")) {
            models_dir_ = "../models";
        } else if (fs::exists("../../models/ultraface_int8.onnx")) {
            models_dir_ = "../../models";
        } else {
            models_dir_ = "D:/GestCam/models";
        }
    }

    std::string models_dir_;
};

TEST_F(OnnxDirectMLEngineTest, UltraFaceDirectMLInitialization) {
    gestcam::OnnxDirectMLEngine engine;
    std::string model_path = models_dir_ + "/ultraface_int8.onnx";

    ASSERT_TRUE(fs::exists(model_path)) << "Model file not found at: " << model_path;

    bool loaded = engine.LoadModel(model_path, 0, true);
    ASSERT_TRUE(loaded) << "Failed to load model: " << engine.GetLastError();

    EXPECT_TRUE(engine.IsDirectMLEnabled());
    EXPECT_EQ(engine.GetExecutionProviderName(), "DirectML");
}

TEST_F(OnnxDirectMLEngineTest, UltraFaceMetadataAndShapes) {
    gestcam::OnnxDirectMLEngine engine;
    std::string model_path = models_dir_ + "/ultraface_int8.onnx";
    ASSERT_TRUE(engine.LoadModel(model_path, 0, true));

    EXPECT_EQ(engine.GetInputCount(), 1u);
    EXPECT_EQ(engine.GetOutputCount(), 2u);

    const auto& in_names = engine.GetInputNames();
    ASSERT_FALSE(in_names.empty());
    EXPECT_EQ(in_names[0], "input");

    const auto& in_shape = engine.GetInputShape(0);
    ASSERT_EQ(in_shape.size(), 4u);
    EXPECT_EQ(in_shape[0], 1);
    EXPECT_EQ(in_shape[1], 3);
    EXPECT_EQ(in_shape[2], 240);
    EXPECT_EQ(in_shape[3], 320);

    size_t expected_elements = 1 * 3 * 240 * 320;
    EXPECT_EQ(engine.GetInputElementsCount(0), expected_elements);
    EXPECT_EQ(engine.GetInputBufferSize(0), expected_elements);

    const auto& out_names = engine.GetOutputNames();
    ASSERT_EQ(out_names.size(), 2u);
    EXPECT_EQ(out_names[0], "scores");
    EXPECT_EQ(out_names[1], "boxes");
}

TEST_F(OnnxDirectMLEngineTest, ZeroAllocationInference) {
    gestcam::OnnxDirectMLEngine engine;
    std::string model_path = models_dir_ + "/ultraface_int8.onnx";
    ASSERT_TRUE(engine.LoadModel(model_path, 0, true));

    float* input_buf = engine.GetInputBuffer(0);
    ASSERT_NE(input_buf, nullptr);

    // Populate buffer without any dynamic memory allocations
    size_t num_elements = engine.GetInputBufferSize(0);
    std::fill_n(input_buf, num_elements, 0.5f);

    // Run inference
    bool ok = engine.Run();
    ASSERT_TRUE(ok) << "Inference failed: " << engine.GetLastError();

    EXPECT_GT(engine.GetLastLatencyMs(), 0.0f);
    EXPECT_LT(engine.GetLastLatencyMs(), 100.0f); // Generous timeout, usually < 12ms

    // Check outputs
    const float* scores_data = engine.GetOutputData(0);
    const float* boxes_data = engine.GetOutputData(1);
    ASSERT_NE(scores_data, nullptr);
    ASSERT_NE(boxes_data, nullptr);

    auto scores_shape = engine.GetOutputShape(0);
    auto boxes_shape = engine.GetOutputShape(1);

    ASSERT_EQ(scores_shape.size(), 3u);
    EXPECT_EQ(scores_shape[0], 1);
    EXPECT_EQ(scores_shape[1], 4420);
    EXPECT_EQ(scores_shape[2], 2);

    ASSERT_EQ(boxes_shape.size(), 3u);
    EXPECT_EQ(boxes_shape[0], 1);
    EXPECT_EQ(boxes_shape[1], 4420);
    EXPECT_EQ(boxes_shape[2], 4);
}

TEST_F(OnnxDirectMLEngineTest, BlazePalmInitializationAndInference) {
    gestcam::OnnxDirectMLEngine engine;
    std::string model_path = models_dir_ + "/blazepalm_int8.onnx";

    ASSERT_TRUE(fs::exists(model_path)) << "Model file not found at: " << model_path;
    ASSERT_TRUE(engine.LoadModel(model_path, 0, true)) << engine.GetLastError();

    EXPECT_EQ(engine.GetInputCount(), 1u);
    const auto& in_shape = engine.GetInputShape(0);
    ASSERT_EQ(in_shape.size(), 4u);
    EXPECT_EQ(in_shape[1], 3);
    EXPECT_EQ(in_shape[2], 256);
    EXPECT_EQ(in_shape[3], 256);

    EXPECT_EQ(engine.GetOutputCount(), 2u);
    EXPECT_EQ(engine.GetOutputNames()[0], "regressors");
    EXPECT_EQ(engine.GetOutputNames()[1], "classificators");

    // Test zero-allocation run
    float* in_buf = engine.GetInputBuffer(0);
    std::fill_n(in_buf, engine.GetInputBufferSize(0), 0.0f);

    EXPECT_TRUE(engine.Run()) << engine.GetLastError();
    EXPECT_GT(engine.GetLastLatencyMs(), 0.0f);
}

TEST_F(OnnxDirectMLEngineTest, BlazeHandInitializationAndInference) {
    gestcam::OnnxDirectMLEngine engine;
    std::string model_path = models_dir_ + "/blazehand_int8.onnx";

    ASSERT_TRUE(fs::exists(model_path)) << "Model file not found at: " << model_path;
    ASSERT_TRUE(engine.LoadModel(model_path, 0, true)) << engine.GetLastError();

    EXPECT_EQ(engine.GetInputCount(), 1u);
    const auto& in_shape = engine.GetInputShape(0);
    ASSERT_EQ(in_shape.size(), 4u);
    EXPECT_EQ(in_shape[1], 3);
    EXPECT_EQ(in_shape[2], 256);
    EXPECT_EQ(in_shape[3], 256);

    EXPECT_EQ(engine.GetOutputCount(), 3u);
    EXPECT_EQ(engine.GetOutputNames()[0], "hand_flag");
    EXPECT_EQ(engine.GetOutputNames()[1], "handedness");
    EXPECT_EQ(engine.GetOutputNames()[2], "landmarks");

    // Test zero-allocation run
    float* in_buf = engine.GetInputBuffer(0);
    std::fill_n(in_buf, engine.GetInputBufferSize(0), 0.0f);

    EXPECT_TRUE(engine.Run()) << engine.GetLastError();
    EXPECT_GT(engine.GetLastLatencyMs(), 0.0f);

    auto landmarks_shape = engine.GetOutputShape(2);
    ASSERT_EQ(landmarks_shape.size(), 3u);
    EXPECT_EQ(landmarks_shape[1], 21); // 21 hand joints
    EXPECT_EQ(landmarks_shape[2], 3);  // 3D (x, y, z)
}

TEST_F(OnnxDirectMLEngineTest, ErrorHandlingNonExistentModel) {
    gestcam::OnnxDirectMLEngine engine;
    EXPECT_FALSE(engine.LoadModel("non_existent_path.onnx", 0, true));
    EXPECT_FALSE(engine.GetLastError().empty());
    EXPECT_FALSE(engine.Run());
}
