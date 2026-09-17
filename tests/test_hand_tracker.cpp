#include <gtest/gtest.h>
#include "gestcam/HandTracker.h"

#include <filesystem>
#include <fstream>
#include <vector>
#include <cmath>
#include <chrono>

namespace fs = std::filesystem;

class HandTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Resolve model directory
        if (fs::exists("models/blazepalm_int8.onnx")) {
            palm_model_ = "models/blazepalm_int8.onnx";
            hand_model_ = "models/blazehand_int8.onnx";
            assets_dir_ = "assets";
        } else if (fs::exists("../models/blazepalm_int8.onnx")) {
            palm_model_ = "../models/blazepalm_int8.onnx";
            hand_model_ = "../models/blazehand_int8.onnx";
            assets_dir_ = "../assets";
        } else if (fs::exists("../../models/blazepalm_int8.onnx")) {
            palm_model_ = "../../models/blazepalm_int8.onnx";
            hand_model_ = "../../models/blazehand_int8.onnx";
            assets_dir_ = "../../assets";
        } else {
            palm_model_ = "D:/GestCam/models/blazepalm_int8.onnx";
            hand_model_ = "D:/GestCam/models/blazehand_int8.onnx";
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

    std::string palm_model_;
    std::string hand_model_;
    std::string assets_dir_;
};

TEST_F(HandTrackerTest, Initialization) {
    gestcam::HandTracker tracker;
    ASSERT_TRUE(fs::exists(palm_model_)) << "Palm model not found: " << palm_model_;
    ASSERT_TRUE(fs::exists(hand_model_)) << "Hand model not found: " << hand_model_;

    bool ok = tracker.Initialize(palm_model_, hand_model_, 0, true);
    ASSERT_TRUE(ok);
    EXPECT_TRUE(tracker.IsDirectMLEnabled());
    EXPECT_FALSE(tracker.IsTemporalCacheValid());
}

TEST_F(HandTrackerTest, DetectHandFromScratch) {
    gestcam::HandTracker tracker;
    ASSERT_TRUE(tracker.Initialize(palm_model_, hand_model_, 0, true));

    // 640x426 RGB raw image = 817,920 bytes
    const int w = 640, h = 426;
    auto image_data = LoadRawImage("person_hand.raw", w * h * 3);
    ASSERT_EQ(image_data.size(), static_cast<size_t>(w * h * 3));

    // Frame 1: Stage 1 (BlazePalm) + Stage 2 (BlazeHand)
    auto result = tracker.TrackHand(image_data.data(), w, h, false);

    EXPECT_TRUE(result.has_hand);
    EXPECT_GE(result.confidence, 0.70f);
    EXPECT_FALSE(tracker.DidUseTemporalCache());
    EXPECT_TRUE(tracker.IsTemporalCacheValid());

    // Check detected hand bounding box
    EXPECT_GE(result.xmin, 0.10f);
    EXPECT_LE(result.xmax, 0.90f);
    EXPECT_GE(result.ymin, 0.10f);
    EXPECT_LE(result.ymax, 0.90f);
}

TEST_F(HandTrackerTest, TemporalCachingSpeedup) {
    gestcam::HandTracker tracker;
    ASSERT_TRUE(tracker.Initialize(palm_model_, hand_model_, 0, true));

    const int w = 640, h = 426;
    auto image_data = LoadRawImage("person_hand.raw", w * h * 3);
    ASSERT_EQ(image_data.size(), static_cast<size_t>(w * h * 3));

    // Frame 1: Detect from scratch (Cold cache)
    auto res1 = tracker.TrackHand(image_data.data(), w, h, false);
    ASSERT_TRUE(res1.has_hand);
    ASSERT_FALSE(tracker.DidUseTemporalCache());
    ASSERT_TRUE(tracker.IsTemporalCacheValid());

    // Frame 2: Temporal cache hit! Stage 1 is skipped, runs Stage 2 only
    auto res2 = tracker.TrackHand(image_data.data(), w, h, false);
    EXPECT_TRUE(res2.has_hand);
    EXPECT_GE(res2.confidence, 0.70f);
    EXPECT_TRUE(tracker.DidUseTemporalCache()) << "Expected Stage 1 to be skipped via temporal cache!";

    // Coordinate stability between consecutive identical frames
    EXPECT_NEAR(res1.xmin, res2.xmin, 0.15f);
    EXPECT_NEAR(res1.ymin, res2.ymin, 0.15f);
    EXPECT_NEAR(res1.xmax, res2.xmax, 0.15f);
    EXPECT_NEAR(res1.ymax, res2.ymax, 0.15f);
}

TEST_F(HandTrackerTest, CacheInvalidationOnBlankFrame) {
    gestcam::HandTracker tracker;
    ASSERT_TRUE(tracker.Initialize(palm_model_, hand_model_, 0, true));

    const int w = 640, h = 426;
    auto image_data = LoadRawImage("person_hand.raw", w * h * 3);
    ASSERT_EQ(image_data.size(), static_cast<size_t>(w * h * 3));

    // Establish tracking
    auto res1 = tracker.TrackHand(image_data.data(), w, h, false);
    ASSERT_TRUE(res1.has_hand);
    ASSERT_TRUE(tracker.IsTemporalCacheValid());

    // Send solid black frame (hand disappears)
    std::vector<uint8_t> black_frame(w * h * 3, 0);
    auto res_black = tracker.TrackHand(black_frame.data(), w, h, false);

    EXPECT_FALSE(res_black.has_hand);
    EXPECT_FALSE(tracker.IsTemporalCacheValid()) << "Cache must be invalidated when hand is lost!";
}

TEST_F(HandTrackerTest, HandJointsAnatomicalStructure) {
    gestcam::HandTracker tracker;
    ASSERT_TRUE(tracker.Initialize(palm_model_, hand_model_, 0, true));

    const int w = 640, h = 426;
    auto image_data = LoadRawImage("person_hand.raw", w * h * 3);
    ASSERT_EQ(image_data.size(), static_cast<size_t>(w * h * 3));

    auto res = tracker.TrackHand(image_data.data(), w, h, false);
    ASSERT_TRUE(res.has_hand);

    // Verify all 21 keypoints are bounded in [0.0, 1.0]
    for (int i = 0; i < 21; ++i) {
        EXPECT_GE(res.joints[i].x, 0.0f);
        EXPECT_LE(res.joints[i].x, 1.0f);
        EXPECT_GE(res.joints[i].y, 0.0f);
        EXPECT_LE(res.joints[i].y, 1.0f);
    }

    // Wrist is joint 0
    float wrist_x = res.joints[0].x;
    float wrist_y = res.joints[0].y;

    auto dist = [](float x1, float y1, float x2, float y2) {
        return std::hypot(x2 - x1, y2 - y1);
    };

    // Index fingertip (8) must be further from wrist than index MCP knuckle (5)
    float dist_index_tip = dist(wrist_x, wrist_y, res.joints[8].x, res.joints[8].y);
    float dist_index_mcp = dist(wrist_x, wrist_y, res.joints[5].x, res.joints[5].y);
    EXPECT_GT(dist_index_tip, dist_index_mcp);

    // Middle fingertip (12) must be further from wrist than middle MCP knuckle (9)
    float dist_middle_tip = dist(wrist_x, wrist_y, res.joints[12].x, res.joints[12].y);
    float dist_middle_mcp = dist(wrist_x, wrist_y, res.joints[9].x, res.joints[9].y);
    EXPECT_GT(dist_middle_tip, dist_middle_mcp);
}

TEST_F(HandTrackerTest, Stage2CachedLatencyBenchmark) {
    gestcam::HandTracker tracker;
    ASSERT_TRUE(tracker.Initialize(palm_model_, hand_model_, 0, true));

    const int w = 640, h = 426;
    auto image_data = LoadRawImage("person_hand.raw", w * h * 3);
    ASSERT_EQ(image_data.size(), static_cast<size_t>(w * h * 3));

    // Warm up and prime cache
    tracker.TrackHand(image_data.data(), w, h, false);
    ASSERT_TRUE(tracker.IsTemporalCacheValid());

    // 15 consecutive frames
    const int iterations = 15;
    int cache_hits = 0;
    double total_ms = 0.0;
    double total_stage2_model_ms = 0.0;

    for (int i = 0; i < iterations; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto res = tracker.TrackHand(image_data.data(), w, h, false);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        EXPECT_TRUE(res.has_hand);

        if (tracker.DidUseTemporalCache()) {
            cache_hits++;
            total_ms += ms;
            total_stage2_model_ms += tracker.GetStage2ModelLatencyMs();
        }
    }

    ASSERT_GE(cache_hits, 10) << "Temporal cache should hit on majority of consecutive frames!";
    double avg_pipeline_ms = total_ms / cache_hits;
    double avg_model_ms = total_stage2_model_ms / cache_hits;

    std::cout << "[BENCHMARK] Stage 2 Cached Pipeline Latency (Crop + DML + Parse): "
              << avg_pipeline_ms << " ms / frame" << std::endl;
    std::cout << "[BENCHMARK] Stage 2 Pure DirectML Model Latency: "
              << avg_model_ms << " ms / frame" << std::endl;

    // Full pipeline cached latency budget
    EXPECT_LE(avg_pipeline_ms, 25.0);
    EXPECT_GT(avg_pipeline_ms, 0.0);
}
