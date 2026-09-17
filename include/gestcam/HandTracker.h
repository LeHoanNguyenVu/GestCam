#pragma once

#include "gestcam/OnnxDirectMLEngine.h"

#include <vector>
#include <string>
#include <cstdint>

namespace gestcam {

struct HandJoint {
    float x = 0.0f; // Normalized [0.0, 1.0] in original frame
    float y = 0.0f;
    float z = 0.0f; // Relative depth
};

struct HandDetectionResult {
    bool has_hand = false;
    HandJoint joints[21];
    float confidence = 0.0f;
    float handedness = 0.0f; // > 0.5 for right hand, <= 0.5 for left hand
    bool is_right_hand = false;
    float xmin = 0.0f;
    float ymin = 0.0f;
    float xmax = 0.0f;
    float ymax = 0.0f;
};

struct HandRoi {
    bool is_valid = false;
    float xmin = 0.0f;
    float ymin = 0.0f;
    float xmax = 0.0f;
    float ymax = 0.0f;
    float confidence = 0.0f;
};

/**
 * @brief Two-Stage Hand Tracking Pipeline (BlazePalm + BlazeHand)
 * with Temporal ROI Caching state machine and DirectML iGPU acceleration.
 */
class HandTracker {
public:
    HandTracker();
    ~HandTracker();

    /**
     * @brief Initializes both BlazePalm and BlazeHand models on DirectML.
     */
    bool Initialize(
        const std::string& palm_model_path,
        const std::string& hand_model_path,
        int device_id = 0,
        bool enable_dml = true
    );
    bool Initialize(
        const std::wstring& palm_model_path,
        const std::wstring& hand_model_path,
        int device_id = 0,
        bool enable_dml = true
    );

    /**
     * @brief Tracks hand in the frame. Uses temporal cache when available,
     * otherwise falls back to Stage 1 (BlazePalm) palm detection.
     */
    HandDetectionResult TrackHand(
        const uint8_t* image_data,
        int width,
        int height,
        bool is_bgr = false
    );

    /**
     * @brief Resets temporal tracking cache.
     */
    void ResetState();

    bool IsTemporalCacheValid() const noexcept { return cached_roi_.is_valid; }
    bool DidUseTemporalCache() const noexcept { return did_use_temporal_cache_; }
    const HandRoi& GetCachedRoi() const noexcept { return cached_roi_; }
    float GetLastInferenceTimeMs() const noexcept { return last_inference_time_ms_; }
    float GetStage1ModelLatencyMs() const noexcept { return palm_engine_.GetLastLatencyMs(); }
    float GetStage2ModelLatencyMs() const noexcept { return landmark_engine_.GetLastLatencyMs(); }
    bool IsDirectMLEnabled() const noexcept {
        return palm_engine_.IsDirectMLEnabled() && landmark_engine_.IsDirectMLEnabled();
    }

private:
    void PreprocessFullFrameForPalm(const uint8_t* image_data, int width, int height, bool is_bgr);
    void CropRoiToLandmarkInput(
        const uint8_t* image_data,
        int width,
        int height,
        bool is_bgr,
        const HandRoi& roi
    );

    HandRoi DetectPalmRoi(int width, int height);
    void ParseLandmarks(const HandRoi& roi, HandDetectionResult& result);
    void UpdateCachedRoi(const HandDetectionResult& result, int width, int height);

    OnnxDirectMLEngine palm_engine_;
    OnnxDirectMLEngine landmark_engine_;

    HandRoi cached_roi_;
    bool did_use_temporal_cache_ = false;
    float last_inference_time_ms_ = 0.0f;
};

} // namespace gestcam
