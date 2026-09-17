#pragma once

#include "gestcam/OnnxDirectMLEngine.h"

#include <vector>
#include <string>
#include <cstdint>

namespace gestcam {

struct FaceBox {
    float xmin = 0.0f;
    float ymin = 0.0f;
    float xmax = 0.0f;
    float ymax = 0.0f;
    float confidence = 0.0f;
    float left_eye_x = 0.0f;
    float left_eye_y = 0.0f;
    float right_eye_x = 0.0f;
    float right_eye_y = 0.0f;
    float head_roll_angle = 0.0f; // Radians (-PI to +PI)
};

struct FaceDetectionResult {
    bool has_face = false;
    float xmin = 0.0f;
    float ymin = 0.0f;
    float xmax = 0.0f;
    float ymax = 0.0f; // Chuẩn hóa [0.0, 1.0]
    float head_roll_angle = 0.0f; // Góc radian (-PI đến +PI)
    float confidence = 0.0f;
};

/**
 * @brief High-performance Face Detector and Head Roll Angle Extractor
 * using UltraFace-320 with DirectML hardware acceleration.
 */
class UltraFaceDetector {
public:
    UltraFaceDetector();
    ~UltraFaceDetector();

    /**
     * @brief Initializes UltraFace ONNX model and sets up DirectML session.
     * @param model_path Path to ultraface_int8.onnx.
     * @param device_id GPU adapter index (default 0).
     * @param enable_dml Whether to use DirectML iGPU acceleration.
     * @return true on success.
     */
    bool Initialize(const std::string& model_path, int device_id = 0, bool enable_dml = true);
    bool Initialize(const std::wstring& model_path, int device_id = 0, bool enable_dml = true);

    /**
     * @brief Detects the primary (most confident) face in the image and calculates head roll angle.
     * @param image_data Pointer to RGB24 or BGR24 image pixels.
     * @param width Frame width.
     * @param height Frame height.
     * @param is_bgr true if image_data is BGR24, false if RGB24.
     * @param conf_threshold Minimum face confidence (default 0.70f).
     * @param iou_threshold NMS IoU overlap threshold (default 0.30f).
     * @return FaceDetectionResult containing coordinates, roll angle, and confidence.
     */
    FaceDetectionResult DetectFace(
        const uint8_t* image_data,
        int width,
        int height,
        bool is_bgr = false,
        float conf_threshold = 0.70f,
        float iou_threshold = 0.30f
    );

    /**
     * @brief Detects all faces in the frame.
     */
    std::vector<FaceBox> DetectAllFaces(
        const uint8_t* image_data,
        int width,
        int height,
        bool is_bgr = false,
        float conf_threshold = 0.70f,
        float iou_threshold = 0.30f
    );

    float GetLastInferenceTimeMs() const noexcept { return engine_.GetLastLatencyMs(); }
    bool IsDirectMLEnabled() const noexcept { return engine_.IsDirectMLEnabled(); }

private:
    void PreprocessFrame(const uint8_t* image_data, int width, int height, bool is_bgr);
    void ExtractEyesAndHeadRoll(
        const uint8_t* image_data,
        int width,
        int height,
        bool is_bgr,
        FaceBox& face
    );

    OnnxDirectMLEngine engine_;
    int input_width_ = 320;
    int input_height_ = 240;
};

} // namespace gestcam
