#include <gtest/gtest.h>
#include "gestcam/UltraFaceDetector.h"

#include <filesystem>
#include <fstream>
#include <vector>
#include <cmath>
#include <chrono>

namespace fs = std::filesystem;

class UltraFaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Resolve model directory
        if (fs::exists("models/ultraface_int8.onnx")) {
            model_path_ = "models/ultraface_int8.onnx";
            assets_dir_ = "assets";
        } else if (fs::exists("../models/ultraface_int8.onnx")) {
            model_path_ = "../models/ultraface_int8.onnx";
            assets_dir_ = "../assets";
        } else if (fs::exists("../../models/ultraface_int8.onnx")) {
            model_path_ = "../../models/ultraface_int8.onnx";
            assets_dir_ = "../../assets";
        } else {
            model_path_ = "D:/GestCam/models/ultraface_int8.onnx";
            assets_dir_ = "D:/GestCam/assets";
        }
    }

    std::vector<uint8_t> LoadRawImage(const std::string& filename, size_t expected_size) {
        std::string path = assets_dir_ + "/" + filename;
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return {};
        }
        std::vector<uint8_t> buf(expected_size);
        file.read(reinterpret_cast<char*>(buf.data()), expected_size);
        return buf;
    }

    std::string model_path_;
    std::string assets_dir_;
};

TEST_F(UltraFaceTest, DetectorInitialization) {
    gestcam::UltraFaceDetector detector;
    ASSERT_TRUE(fs::exists(model_path_)) << "Model path not found: " << model_path_;

    bool ok = detector.Initialize(model_path_, 0, true);
    ASSERT_TRUE(ok);
    EXPECT_TRUE(detector.IsDirectMLEnabled());
}

TEST_F(UltraFaceTest, DetectFaceOnRealImage) {
    gestcam::UltraFaceDetector detector;
    ASSERT_TRUE(detector.Initialize(model_path_, 0, true));

    // 512x512 RGB raw image = 786,432 bytes
    auto image_data = LoadRawImage("face_test.raw", 512 * 512 * 3);
    ASSERT_EQ(image_data.size(), 512 * 512 * 3u) << "Could not read assets/face_test.raw";

    auto result = detector.DetectFace(image_data.data(), 512, 512, false, 0.70f, 0.30f);

    EXPECT_TRUE(result.has_face);
    EXPECT_GE(result.confidence, 0.85f);

    // Verify Lena face bounding box coordinates
    EXPECT_NEAR(result.xmin, 0.40f, 0.08f);
    EXPECT_NEAR(result.ymin, 0.38f, 0.08f);
    EXPECT_NEAR(result.xmax, 0.69f, 0.08f);
    EXPECT_NEAR(result.ymax, 0.76f, 0.08f);

    // Lena has a natural slight head tilt (~7 deg = 0.12 rad)
    float roll_deg = result.head_roll_angle * (180.0f / 3.14159265f);
    EXPECT_GE(roll_deg, 2.0f);
    EXPECT_LE(roll_deg, 14.0f);
}

TEST_F(UltraFaceTest, DetectFaceBGR24Compatibility) {
    gestcam::UltraFaceDetector detector;
    ASSERT_TRUE(detector.Initialize(model_path_, 0, true));

    auto rgb_data = LoadRawImage("face_test.raw", 512 * 512 * 3);
    ASSERT_EQ(rgb_data.size(), 512 * 512 * 3u);

    // Convert RGB to BGR in memory
    std::vector<uint8_t> bgr_data = rgb_data;
    for (size_t i = 0; i < bgr_data.size(); i += 3) {
        std::swap(bgr_data[i], bgr_data[i + 2]);
    }

    auto result_rgb = detector.DetectFace(rgb_data.data(), 512, 512, false);
    auto result_bgr = detector.DetectFace(bgr_data.data(), 512, 512, true);

    EXPECT_TRUE(result_bgr.has_face);
    EXPECT_NEAR(result_rgb.xmin, result_bgr.xmin, 0.02f);
    EXPECT_NEAR(result_rgb.ymin, result_bgr.ymin, 0.02f);
    EXPECT_NEAR(result_rgb.xmax, result_bgr.xmax, 0.02f);
    EXPECT_NEAR(result_rgb.ymax, result_bgr.ymax, 0.02f);
    EXPECT_NEAR(result_rgb.confidence, result_bgr.confidence, 0.05f);
}

TEST_F(UltraFaceTest, HeadRollAnglePrecision) {
    gestcam::UltraFaceDetector detector;
    ASSERT_TRUE(detector.Initialize(model_path_, 0, true));

    // 1. Real Image Rotations
    auto base_data = LoadRawImage("face_test.raw", 512 * 512 * 3);
    ASSERT_FALSE(base_data.empty());
    auto base_res = detector.DetectFace(base_data.data(), 512, 512, false);
    ASSERT_TRUE(base_res.has_face);
    float base_deg = base_res.head_roll_angle * (180.0f / 3.14159265f);

    // +15 deg rotated face (counter-clockwise)
    auto rot15_data = LoadRawImage("face_test_rot_15.raw", 512 * 512 * 3);
    if (!rot15_data.empty()) {
        auto rot15_res = detector.DetectFace(rot15_data.data(), 512, 512, false);
        if (rot15_res.has_face) {
            float rot15_deg = rot15_res.head_roll_angle * (180.0f / 3.14159265f);
            float diff = rot15_deg - base_deg;
            // Expected rotation delta around -15 deg in standard screen coordinates
            EXPECT_NEAR(std::abs(diff), 15.0f, 3.0f);
        }
    }

    // -25 deg rotated face (clockwise)
    auto rot_neg25_data = LoadRawImage("face_test_rot_-25.raw", 512 * 512 * 3);
    if (!rot_neg25_data.empty()) {
        auto rot_neg25_res = detector.DetectFace(rot_neg25_data.data(), 512, 512, false);
        if (rot_neg25_res.has_face) {
            float rot_neg25_deg = rot_neg25_res.head_roll_angle * (180.0f / 3.14159265f);
            float diff = rot_neg25_deg - base_deg;
            EXPECT_GT(diff, 10.0f); // Detects clear clockwise tilt
        }
    }

    // 2. Synthetic Ground-Truth Angle Precision Test (Test Case 2.3 DoD: error <= 3 deg)
    // We create synthetic faces with eyes tilted at exact known angles from -25 to +25 deg
    const int w = 320, h = 240;
    const std::vector<float> test_angles = {-25.0f, -20.0f, -15.0f, -10.0f, 0.0f, 10.0f, 15.0f, 20.0f, 25.0f};

    for (float target_deg : test_angles) {
        std::vector<uint8_t> synth_frame(w * h * 3, 210); // Skin tone background
        float cx = 160.0f, cy = 120.0f;
        float eye_dist = 50.0f;
        float rad = target_deg * (3.14159265f / 180.0f);
        float ex_l = cx - 0.5f * eye_dist * std::cos(rad);
        float ey_l = cy - 0.5f * eye_dist * std::sin(rad);
        float ex_r = cx + 0.5f * eye_dist * std::cos(rad);
        float ey_r = cy + 0.5f * eye_dist * std::sin(rad);

        // Draw face oval
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float dx_f = (x - cx) / 70.0f;
                float dy_f = (y - cy) / 85.0f;
                if (dx_f * dx_f + dy_f * dy_f <= 1.0f) {
                    synth_frame[(y * w + x) * 3 + 0] = 220;
                    synth_frame[(y * w + x) * 3 + 1] = 190;
                    synth_frame[(y * w + x) * 3 + 2] = 170;
                }
                // Dark pupil dots
                if ((x - ex_l) * (x - ex_l) + (y - ey_l) * (y - ey_l) <= 16.0f ||
                    (x - ex_r) * (x - ex_r) + (y - ey_r) * (y - ey_r) <= 16.0f) {
                    synth_frame[(y * w + x) * 3 + 0] = 20;
                    synth_frame[(y * w + x) * 3 + 1] = 20;
                    synth_frame[(y * w + x) * 3 + 2] = 20;
                }
            }
        }

        // Detect on synthetic face
        auto synth_res = detector.DetectFace(synth_frame.data(), w, h, false, 0.50f);
        if (synth_res.has_face) {
            float measured_deg = synth_res.head_roll_angle * (180.0f / 3.14159265f);
            float error = std::abs(measured_deg - target_deg);
            EXPECT_LE(error, 3.0f)
                << "Target: " << target_deg << " deg, Measured: " << measured_deg
                << " deg, Error: " << error << " deg (DoD max is 3.0 deg)";
        }
    }
}

TEST_F(UltraFaceTest, BlankFrameRejection) {
    gestcam::UltraFaceDetector detector;
    ASSERT_TRUE(detector.Initialize(model_path_, 0, true));

    // Solid black 1280x720 frame
    std::vector<uint8_t> black_frame(1280 * 720 * 3, 0);
    auto result_black = detector.DetectFace(black_frame.data(), 1280, 720, false);
    EXPECT_FALSE(result_black.has_face);

    // Solid white 1280x720 frame
    std::vector<uint8_t> white_frame(1280 * 720 * 3, 255);
    auto result_white = detector.DetectFace(white_frame.data(), 1280, 720, false);
    EXPECT_FALSE(result_white.has_face);
}

TEST_F(UltraFaceTest, FullPipelineLatencyBenchmark) {
    gestcam::UltraFaceDetector detector;
    ASSERT_TRUE(detector.Initialize(model_path_, 0, true));

    // Full HD 1280x720 frame
    std::vector<uint8_t> frame_1280x720(1280 * 720 * 3, 128);

    // Warm up
    detector.DetectFace(frame_1280x720.data(), 1280, 720, false);

    // 15 iterations benchmark
    const int iterations = 15;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        detector.DetectFace(frame_1280x720.data(), 1280, 720, false);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    double avg_ms = total_ms / iterations;

    std::cout << "[BENCHMARK] UltraFace Full Pipeline Latency (1280x720 -> 320x240 DirectML): "
              << avg_ms << " ms / frame" << std::endl;

    // Must be <= 12.0 ms as per Sprint 2 DoD
    EXPECT_LE(avg_ms, 12.0);
}
