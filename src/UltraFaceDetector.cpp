#include "gestcam/UltraFaceDetector.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace gestcam {

namespace {

float CalculateArea(const FaceBox& b) {
    return (b.xmax - b.xmin) * (b.ymax - b.ymin);
}

float CalculateIoU(const FaceBox& a, const FaceBox& b) {
    float inter_xmin = std::max(a.xmin, b.xmin);
    float inter_ymin = std::max(a.ymin, b.ymin);
    float inter_xmax = std::min(a.xmax, b.xmax);
    float inter_ymax = std::min(a.ymax, b.ymax);

    float inter_w = std::max(0.0f, inter_xmax - inter_xmin);
    float inter_h = std::max(0.0f, inter_ymax - inter_ymin);
    float inter_area = inter_w * inter_h;

    float union_area = CalculateArea(a) + CalculateArea(b) - inter_area;
    return union_area > 0.0f ? (inter_area / union_area) : 0.0f;
}

// Computes dark pupil/iris centroid within an eye region
void LocateEyeCentroid(
    const uint8_t* image_data,
    int width,
    int height,
    bool is_bgr,
    int x1, int x2,
    int y1, int y2,
    float& out_cx,
    float& out_cy
) {
    x1 = std::clamp(x1, 0, width - 1);
    x2 = std::clamp(x2, 0, width - 1);
    y1 = std::clamp(y1, 0, height - 1);
    y2 = std::clamp(y2, 0, height - 1);

    int roi_w = x2 - x1 + 1;
    int roi_h = y2 - y1 + 1;

    if (roi_w <= 0 || roi_h <= 0) {
        out_cx = static_cast<float>(x1);
        out_cy = static_cast<float>(y1);
        return;
    }

    // Pass 1: compute mean inverse intensity
    double sum_inv = 0.0;
    int pixel_count = roi_w * roi_h;

    for (int y = y1; y <= y2; ++y) {
        const uint8_t* row = image_data + y * width * 3;
        for (int x = x1; x <= x2; ++x) {
            uint8_t c0 = row[x * 3 + 0];
            uint8_t c1 = row[x * 3 + 1];
            uint8_t c2 = row[x * 3 + 2];
            uint8_t r = is_bgr ? c2 : c0;
            uint8_t g = c1;
            uint8_t b = is_bgr ? c0 : c2;
            float lum = 0.299f * r + 0.587f * g + 0.114f * b;
            sum_inv += (255.0f - lum);
        }
    }

    float mean_inv = static_cast<float>(sum_inv / pixel_count);

    // Pass 2: compute weighted centroid of pixels darker than mean
    double weight_sum = 0.0;
    double weighted_x = 0.0;
    double weighted_y = 0.0;

    for (int y = y1; y <= y2; ++y) {
        const uint8_t* row = image_data + y * width * 3;
        for (int x = x1; x <= x2; ++x) {
            uint8_t c0 = row[x * 3 + 0];
            uint8_t c1 = row[x * 3 + 1];
            uint8_t c2 = row[x * 3 + 2];
            uint8_t r = is_bgr ? c2 : c0;
            uint8_t g = c1;
            uint8_t b = is_bgr ? c0 : c2;
            float lum = 0.299f * r + 0.587f * g + 0.114f * b;
            float inv = 255.0f - lum;

            float w = std::max(0.0f, inv - mean_inv);
            // Power of 2 weighting to strongly isolate the darkest pupil center
            w = w * w;

            weight_sum += w;
            weighted_x += x * w;
            weighted_y += y * w;
        }
    }

    if (weight_sum > 1e-5) {
        out_cx = static_cast<float>(weighted_x / weight_sum);
        out_cy = static_cast<float>(weighted_y / weight_sum);
    } else {
        out_cx = (x1 + x2) * 0.5f;
        out_cy = (y1 + y2) * 0.5f;
    }
}

} // namespace

UltraFaceDetector::UltraFaceDetector() = default;
UltraFaceDetector::~UltraFaceDetector() = default;

bool UltraFaceDetector::Initialize(const std::string& model_path, int device_id, bool enable_dml) {
    if (!engine_.LoadModel(model_path, device_id, enable_dml)) {
        return false;
    }

    if (engine_.GetInputCount() > 0) {
        const auto& shape = engine_.GetInputShape(0);
        if (shape.size() == 4) {
            input_height_ = static_cast<int>(shape[2]);
            input_width_ = static_cast<int>(shape[3]);
        }
    }

    return true;
}

bool UltraFaceDetector::Initialize(const std::wstring& model_path, int device_id, bool enable_dml) {
    if (!engine_.LoadModel(model_path, device_id, enable_dml)) {
        return false;
    }

    if (engine_.GetInputCount() > 0) {
        const auto& shape = engine_.GetInputShape(0);
        if (shape.size() == 4) {
            input_height_ = static_cast<int>(shape[2]);
            input_width_ = static_cast<int>(shape[3]);
        }
    }

    return true;
}

void UltraFaceDetector::PreprocessFrame(const uint8_t* image_data, int width, int height, bool is_bgr) {
    float* buf = engine_.GetInputBuffer(0);
    if (!buf || width <= 0 || height <= 0) return;

    int channel_size = input_width_ * input_height_;
    float* r_plane = buf;
    float* g_plane = buf + channel_size;
    float* b_plane = buf + 2 * channel_size;

    // Fast bilinear / nearest-neighbor sampling directly into static planar buffer
    for (int y = 0; y < input_height_; ++y) {
        int src_y = (y * height) / input_height_;
        const uint8_t* src_row = image_data + src_y * width * 3;
        int dst_row_offset = y * input_width_;

        for (int x = 0; x < input_width_; ++x) {
            int src_x = (x * width) / input_width_;
            const uint8_t* p = src_row + src_x * 3;

            uint8_t c0 = p[0];
            uint8_t c1 = p[1];
            uint8_t c2 = p[2];

            float r = static_cast<float>(is_bgr ? c2 : c0);
            float g = static_cast<float>(c1);
            float b = static_cast<float>(is_bgr ? c0 : c2);

            int dst_idx = dst_row_offset + x;
            r_plane[dst_idx] = (r - 127.0f) / 128.0f;
            g_plane[dst_idx] = (g - 127.0f) / 128.0f;
            b_plane[dst_idx] = (b - 127.0f) / 128.0f;
        }
    }
}

void UltraFaceDetector::ExtractEyesAndHeadRoll(
    const uint8_t* image_data,
    int width,
    int height,
    bool is_bgr,
    FaceBox& face
) {
    int px_min = static_cast<int>(face.xmin * width);
    int py_min = static_cast<int>(face.ymin * height);
    int px_max = static_cast<int>(face.xmax * width);
    int py_max = static_cast<int>(face.ymax * height);

    int fw = std::max(1, px_max - px_min);
    int fh = std::max(1, py_max - py_min);

    // Anthropometric eye search windows inside face bounding box:
    int lx1 = px_min + static_cast<int>(0.15f * fw);
    int lx2 = px_min + static_cast<int>(0.48f * fw);
    int ly1 = py_min + static_cast<int>(0.20f * fh);
    int ly2 = py_min + static_cast<int>(0.60f * fh);

    int rx1 = px_min + static_cast<int>(0.52f * fw);
    int rx2 = px_min + static_cast<int>(0.85f * fw);
    int ry1 = py_min + static_cast<int>(0.20f * fh);
    int ry2 = py_min + static_cast<int>(0.60f * fh);

    float cx_l = 0.0f, cy_l = 0.0f;
    float cx_r = 0.0f, cy_r = 0.0f;

    LocateEyeCentroid(image_data, width, height, is_bgr, lx1, lx2, ly1, ly2, cx_l, cy_l);
    LocateEyeCentroid(image_data, width, height, is_bgr, rx1, rx2, ry1, ry2, cx_r, cy_r);

    face.left_eye_x = cx_l / width;
    face.left_eye_y = cy_l / height;
    face.right_eye_x = cx_r / width;
    face.right_eye_y = cy_r / height;

    float dx = cx_r - cx_l;
    float dy = cy_r - cy_l;

    // Head Roll Angle theta = atan2(dy, dx)
    // Upright head: y_R == y_L -> theta = 0 rad
    // Tilt right: y_R > y_L -> theta > 0
    // Tilt left:  y_R < y_L -> theta < 0
    face.head_roll_angle = std::atan2(dy, dx);
}

std::vector<FaceBox> UltraFaceDetector::DetectAllFaces(
    const uint8_t* image_data,
    int width,
    int height,
    bool is_bgr,
    float conf_threshold,
    float iou_threshold
) {
    if (!image_data || width <= 0 || height <= 0 || engine_.GetInputCount() == 0) {
        return {};
    }

    // 1. Zero-allocation frame downsampling & normalization
    PreprocessFrame(image_data, width, height, is_bgr);

    // 2. DirectML GPU Inference
    if (!engine_.Run()) {
        return {};
    }

    if (engine_.GetOutputCount() < 2) {
        return {};
    }

    const float* scores = engine_.GetOutputData(0); // [1, 4420, 2]
    const float* boxes = engine_.GetOutputData(1);  // [1, 4420, 4]
    size_t num_priors = engine_.GetOutputShape(0)[1];

    // 3. Filter candidates by confidence threshold
    std::vector<FaceBox> candidates;
    candidates.reserve(32);

    for (size_t i = 0; i < num_priors; ++i) {
        float conf = scores[i * 2 + 1];
        if (conf >= conf_threshold) {
            FaceBox fb;
            fb.confidence = conf;
            fb.xmin = std::clamp(boxes[i * 4 + 0], 0.0f, 1.0f);
            fb.ymin = std::clamp(boxes[i * 4 + 1], 0.0f, 1.0f);
            fb.xmax = std::clamp(boxes[i * 4 + 2], 0.0f, 1.0f);
            fb.ymax = std::clamp(boxes[i * 4 + 3], 0.0f, 1.0f);

            if (fb.xmax > fb.xmin && fb.ymax > fb.ymin) {
                candidates.push_back(fb);
            }
        }
    }

    if (candidates.empty()) {
        return {};
    }

    // 4. Hard Non-Maximum Suppression (NMS)
    std::sort(candidates.begin(), candidates.end(), [](const FaceBox& a, const FaceBox& b) {
        return a.confidence > b.confidence;
    });

    std::vector<FaceBox> picked_faces;
    picked_faces.reserve(candidates.size());

    for (const auto& cand : candidates) {
        bool suppress = false;
        for (const auto& picked : picked_faces) {
            if (CalculateIoU(cand, picked) > iou_threshold) {
                suppress = true;
                break;
            }
        }
        if (!suppress) {
            picked_faces.push_back(cand);
        }
    }

    // 5. Extract eyes and head roll angle for picked faces
    for (auto& face : picked_faces) {
        ExtractEyesAndHeadRoll(image_data, width, height, is_bgr, face);
    }

    return picked_faces;
}

FaceDetectionResult UltraFaceDetector::DetectFace(
    const uint8_t* image_data,
    int width,
    int height,
    bool is_bgr,
    float conf_threshold,
    float iou_threshold
) {
    std::vector<FaceBox> faces = DetectAllFaces(image_data, width, height, is_bgr, conf_threshold, iou_threshold);
    if (faces.empty()) {
        return FaceDetectionResult{false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    }

    const auto& best = faces[0];
    return FaceDetectionResult{
        true,
        best.xmin,
        best.ymin,
        best.xmax,
        best.ymax,
        best.head_roll_angle,
        best.confidence
    };
}

} // namespace gestcam
