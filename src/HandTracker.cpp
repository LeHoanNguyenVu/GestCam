#include "gestcam/HandTracker.h"
#include "gestcam/BlazePalmAnchors.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>

namespace gestcam {

namespace {

inline float Sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-std::clamp(x, -20.0f, 20.0f)));
}

} // namespace

HandTracker::HandTracker() = default;
HandTracker::~HandTracker() = default;

bool HandTracker::Initialize(
    const std::string& palm_model_path,
    const std::string& hand_model_path,
    int device_id,
    bool enable_dml
) {
    if (!palm_engine_.LoadModel(palm_model_path, device_id, enable_dml)) {
        return false;
    }
    if (!landmark_engine_.LoadModel(hand_model_path, device_id, enable_dml)) {
        return false;
    }
    ResetState();
    return true;
}

bool HandTracker::Initialize(
    const std::wstring& palm_model_path,
    const std::wstring& hand_model_path,
    int device_id,
    bool enable_dml
) {
    if (!palm_engine_.LoadModel(palm_model_path, device_id, enable_dml)) {
        return false;
    }
    if (!landmark_engine_.LoadModel(hand_model_path, device_id, enable_dml)) {
        return false;
    }
    ResetState();
    return true;
}

void HandTracker::ResetState() {
    cached_roi_ = HandRoi{};
    did_use_temporal_cache_ = false;
    last_inference_time_ms_ = 0.0f;
}

void HandTracker::PreprocessFullFrameForPalm(
    const uint8_t* image_data,
    int width,
    int height,
    bool is_bgr
) {
    float* buf = palm_engine_.GetInputBuffer(0);
    if (!buf || width <= 0 || height <= 0) return;

    const int palm_size = 256;
    int channel_size = palm_size * palm_size;
    float* r_plane = buf;
    float* g_plane = buf + channel_size;
    float* b_plane = buf + 2 * channel_size;

    for (int y = 0; y < palm_size; ++y) {
        int src_y = (y * height) / palm_size;
        const uint8_t* src_row = image_data + src_y * width * 3;
        int dst_offset = y * palm_size;

        for (int x = 0; x < palm_size; ++x) {
            int src_x = (x * width) / palm_size;
            const uint8_t* p = src_row + src_x * 3;

            uint8_t c0 = p[0];
            uint8_t c1 = p[1];
            uint8_t c2 = p[2];

            float r = static_cast<float>(is_bgr ? c2 : c0) / 255.0f;
            float g = static_cast<float>(c1) / 255.0f;
            float b = static_cast<float>(is_bgr ? c0 : c2) / 255.0f;

            int idx = dst_offset + x;
            r_plane[idx] = r;
            g_plane[idx] = g;
            b_plane[idx] = b;
        }
    }
}

void HandTracker::CropRoiToLandmarkInput(
    const uint8_t* image_data,
    int width,
    int height,
    bool is_bgr,
    const HandRoi& roi
) {
    float* buf = landmark_engine_.GetInputBuffer(0);
    if (!buf || width <= 0 || height <= 0) return;

    const int hand_size = 256;
    int channel_size = hand_size * hand_size;
    float* r_plane = buf;
    float* g_plane = buf + channel_size;
    float* b_plane = buf + 2 * channel_size;

    float rx_min = roi.xmin * width;
    float ry_min = roi.ymin * height;
    float rx_max = roi.xmax * width;
    float ry_max = roi.ymax * height;
    float rw = std::max(1.0f, rx_max - rx_min);
    float rh = std::max(1.0f, ry_max - ry_min);

    for (int y = 0; y < hand_size; ++y) {
        float src_yf = ry_min + (y * rh) / static_cast<float>(hand_size);
        int src_y = std::clamp(static_cast<int>(src_yf), 0, height - 1);
        const uint8_t* src_row = image_data + src_y * width * 3;
        int dst_offset = y * hand_size;

        for (int x = 0; x < hand_size; ++x) {
            float src_xf = rx_min + (x * rw) / static_cast<float>(hand_size);
            int src_x = std::clamp(static_cast<int>(src_xf), 0, width - 1);
            const uint8_t* p = src_row + src_x * 3;

            uint8_t c0 = p[0];
            uint8_t c1 = p[1];
            uint8_t c2 = p[2];

            float r = static_cast<float>(is_bgr ? c2 : c0) / 255.0f;
            float g = static_cast<float>(c1) / 255.0f;
            float b = static_cast<float>(is_bgr ? c0 : c2) / 255.0f;

            int idx = dst_offset + x;
            r_plane[idx] = r;
            g_plane[idx] = g;
            b_plane[idx] = b;
        }
    }
}

HandRoi HandTracker::DetectPalmRoi(int width, int height) {
    if (palm_engine_.GetOutputCount() < 2) return HandRoi{};

    const float* regressors = palm_engine_.GetOutputData(0); // [1, 2944, 18]
    const float* classificators = palm_engine_.GetOutputData(1); // [1, 2944, 1]

    float best_score = 0.0f;
    int best_idx = -1;

    for (int i = 0; i < kBlazePalmNumAnchors; ++i) {
        float score = Sigmoid(classificators[i]);
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    if (best_idx < 0 || best_score < 0.65f) {
        return HandRoi{};
    }

    const float* box = regressors + best_idx * 18;
    const float* anc = kBlazePalmAnchors[best_idx];

    float xc = box[0] / 256.0f * anc[2] + anc[0];
    float yc = box[1] / 256.0f * anc[3] + anc[1];
    float w = box[2] / 256.0f * anc[2];
    float h = box[3] / 256.0f * anc[3];

    // Keypoint 0 (wrist) and Keypoint 2 (middle finger MCP)
    float kp0_x = box[4] / 256.0f * anc[2] + anc[0];
    float kp0_y = box[5] / 256.0f * anc[3] + anc[1];
    float kp2_x = box[8] / 256.0f * anc[2] + anc[0];
    float kp2_y = box[9] / 256.0f * anc[3] + anc[1];

    float dx = (kp2_x - kp0_x);
    float dy = (kp2_y - kp0_y);
    float palm_len = std::hypot(dx, dy);
    float box_size = std::max(std::max(w, h), palm_len) * 2.6f;

    // Shift center towards fingers along palm vector
    float cx_shifted = xc + dx * 0.4f;
    float cy_shifted = yc + dy * 0.4f;

    float px_c = cx_shifted * static_cast<float>(width);
    float py_c = cy_shifted * static_cast<float>(height);
    float p_size = box_size * static_cast<float>(std::max(width, height));

    HandRoi roi;
    roi.xmin = std::clamp((px_c - p_size * 0.5f) / static_cast<float>(width), 0.0f, 1.0f);
    roi.ymin = std::clamp((py_c - p_size * 0.5f) / static_cast<float>(height), 0.0f, 1.0f);
    roi.xmax = std::clamp((px_c + p_size * 0.5f) / static_cast<float>(width), 0.0f, 1.0f);
    roi.ymax = std::clamp((py_c + p_size * 0.5f) / static_cast<float>(height), 0.0f, 1.0f);
    roi.confidence = best_score;
    roi.is_valid = (roi.xmax > roi.xmin && roi.ymax > roi.ymin);

    return roi;
}

void HandTracker::ParseLandmarks(const HandRoi& roi, HandDetectionResult& result) {
    if (landmark_engine_.GetOutputCount() < 3) return;

    const float* flag_data = landmark_engine_.GetOutputData(0);   // [1]
    const float* hand_data = landmark_engine_.GetOutputData(1);   // [1]
    const float* lm_data = landmark_engine_.GetOutputData(2);     // [1, 21, 3]

    float flag_score = flag_data[0];
    result.confidence = flag_score;
    result.has_hand = (flag_score >= 0.50f);
    result.handedness = hand_data[0];
    result.is_right_hand = (hand_data[0] >= 0.50f);

    if (!result.has_hand) return;

    float roi_w = roi.xmax - roi.xmin;
    float roi_h = roi.ymax - roi.ymin;

    float xmin = 1.0f, ymin = 1.0f, xmax = 0.0f, ymax = 0.0f;

    for (int i = 0; i < 21; ++i) {
        float lx = lm_data[i * 3 + 0];
        float ly = lm_data[i * 3 + 1];
        float lz = lm_data[i * 3 + 2];

        // Map from cropped ROI to original full frame coordinates
        float gx = std::clamp(roi.xmin + lx * roi_w, 0.0f, 1.0f);
        float gy = std::clamp(roi.ymin + ly * roi_h, 0.0f, 1.0f);

        result.joints[i].x = gx;
        result.joints[i].y = gy;
        result.joints[i].z = lz;

        xmin = std::min(xmin, gx);
        ymin = std::min(ymin, gy);
        xmax = std::max(xmax, gx);
        ymax = std::max(ymax, gy);
    }

    result.xmin = xmin;
    result.ymin = ymin;
    result.xmax = xmax;
    result.ymax = ymax;
}

void HandTracker::UpdateCachedRoi(const HandDetectionResult& result, int width, int height) {
    if (width <= 0 || height <= 0) return;

    float px_min = result.xmin * static_cast<float>(width);
    float px_max = result.xmax * static_cast<float>(width);
    float py_min = result.ymin * static_cast<float>(height);
    float py_max = result.ymax * static_cast<float>(height);

    float pw = px_max - px_min;
    float ph = py_max - py_min;
    float pcx = (px_min + px_max) * 0.5f;
    float pcy = (py_min + py_max) * 0.5f;

    // Expand bounding box by 25% in pixel space to guarantee full hand coverage
    float p_size = std::max(pw, ph) * 1.25f;

    cached_roi_.xmin = std::clamp((pcx - p_size * 0.5f) / static_cast<float>(width), 0.0f, 1.0f);
    cached_roi_.xmax = std::clamp((pcx + p_size * 0.5f) / static_cast<float>(width), 0.0f, 1.0f);
    cached_roi_.ymin = std::clamp((pcy - p_size * 0.5f) / static_cast<float>(height), 0.0f, 1.0f);
    cached_roi_.ymax = std::clamp((pcy + p_size * 0.5f) / static_cast<float>(height), 0.0f, 1.0f);
    cached_roi_.confidence = result.confidence;
    cached_roi_.is_valid = true;
}

HandDetectionResult HandTracker::TrackHand(
    const uint8_t* image_data,
    int width,
    int height,
    bool is_bgr
) {
    HandDetectionResult result;
    if (!image_data || width <= 0 || height <= 0) return result;

    auto t_start = std::chrono::high_resolution_clock::now();

    // 1. Temporal ROI Caching: If frame N-1 has confidence >= 0.80, run Stage 2 directly!
    if (cached_roi_.is_valid && cached_roi_.confidence >= 0.80f) {
        CropRoiToLandmarkInput(image_data, width, height, is_bgr, cached_roi_);
        if (landmark_engine_.Run()) {
            ParseLandmarks(cached_roi_, result);
            if (result.has_hand && result.confidence >= 0.50f) {
                // Tracking maintained without Stage 1!
                UpdateCachedRoi(result, width, height);
                did_use_temporal_cache_ = true;

                auto t_end = std::chrono::high_resolution_clock::now();
                last_inference_time_ms_ = std::chrono::duration<float, std::milli>(t_end - t_start).count();
                return result;
            }
        }
        // Confidence dropped below 0.50 -> cache invalidated, fallback to Stage 1
        cached_roi_.is_valid = false;
    }

    did_use_temporal_cache_ = false;

    // 2. Stage 1: BlazePalm Detector
    PreprocessFullFrameForPalm(image_data, width, height, is_bgr);
    if (!palm_engine_.Run()) {
        auto t_end = std::chrono::high_resolution_clock::now();
        last_inference_time_ms_ = std::chrono::duration<float, std::milli>(t_end - t_start).count();
        return result;
    }

    HandRoi palm_roi = DetectPalmRoi(width, height);
    if (!palm_roi.is_valid) {
        cached_roi_.is_valid = false;
        auto t_end = std::chrono::high_resolution_clock::now();
        last_inference_time_ms_ = std::chrono::duration<float, std::milli>(t_end - t_start).count();
        return result;
    }

    // 3. Stage 2: BlazeHand Landmarks on newly detected Palm ROI
    CropRoiToLandmarkInput(image_data, width, height, is_bgr, palm_roi);
    if (!landmark_engine_.Run()) {
        auto t_end = std::chrono::high_resolution_clock::now();
        last_inference_time_ms_ = std::chrono::duration<float, std::milli>(t_end - t_start).count();
        return result;
    }

    ParseLandmarks(palm_roi, result);
    if (result.has_hand && result.confidence >= 0.70f) {
        UpdateCachedRoi(result, width, height);
    } else {
        cached_roi_.is_valid = false;
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    last_inference_time_ms_ = std::chrono::duration<float, std::milli>(t_end - t_start).count();
    return result;
}

} // namespace gestcam
