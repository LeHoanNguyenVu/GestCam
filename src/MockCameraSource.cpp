#include "gestcam/MockCameraSource.h"
#include <thread>
#include <algorithm>

namespace gestcam {

MockCameraSource::MockCameraSource() = default;

MockCameraSource::~MockCameraSource() {
    Close();
}

bool MockCameraSource::Open(int /*device_index*/, int width, int height, int target_fps) {
    current_mode_.width = (width > 0) ? width : 1280;
    current_mode_.height = (height > 0) ? height : 720;
    current_mode_.fps = (target_fps > 0) ? target_fps : 30;
    current_mode_.format = VideoPixelFormat::NV12;

    is_opened_ = true;
    frame_count_ = 0;
    last_frame_time_ = std::chrono::steady_clock::now();
    return true;
}

void MockCameraSource::Close() {
    is_opened_ = false;
    frame_count_ = 0;
}

bool MockCameraSource::GrabFrame(RawVideoFrame& out_frame, int /*timeout_ms*/) {
    if (!is_opened_) return false;

    // Giữ nhịp FPS thực tế (Pacing)
    int target_frame_time_ms = 1000 / current_mode_.fps;
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_time_).count();

    if (elapsed_ms < target_frame_time_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(target_frame_time_ms - elapsed_ms));
    }
    last_frame_time_ = std::chrono::steady_clock::now();

    int w = current_mode_.width;
    int h = current_mode_.height;
    size_t y_size = static_cast<size_t>(w * h);
    size_t uv_size = y_size / 2;
    size_t total_size = y_size + uv_size;

    out_frame.width = w;
    out_frame.height = h;
    out_frame.stride = w;
    out_frame.format = VideoPixelFormat::NV12;
    out_frame.timestamp_us = static_cast<int64_t>(frame_count_ * (1000000 / current_mode_.fps));
    out_frame.data.resize(total_size);

    uint8_t* y_plane = out_frame.data.data();
    uint8_t* uv_plane = y_plane + y_size;

    // Màu SMPTE chuẩn cho 8 dải dọc
    struct YUVColor { uint8_t y, u, v; };
    static const YUVColor kBars[8] = {
        {235, 128, 128}, // White
        {210, 16,  146}, // Yellow
        {170, 166, 16 }, // Cyan
        {145, 54,  34 }, // Green
        {106, 202, 222}, // Magenta
        {81,  90,  240}, // Red
        {41,  240, 110}, // Blue
        {16,  128, 128}  // Black
    };

    int bar_width = w / 8;
    int anim_offset = animated_ ? static_cast<int>(frame_count_ % 8) : 0;

    // Sinh Y plane
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int bar_idx = ((x / bar_width) + anim_offset) % 8;
            y_plane[y * w + x] = kBars[bar_idx].y;
        }
    }

    // Sinh UV plane (UV interleaved)
    int uv_h = h / 2;
    int uv_w = w / 2;
    for (int y = 0; y < uv_h; ++y) {
        for (int x = 0; x < uv_w; ++x) {
            int orig_x = x * 2;
            int bar_idx = ((orig_x / bar_width) + anim_offset) % 8;
            size_t uv_idx = static_cast<size_t>(y * w + x * 2);
            uv_plane[uv_idx]     = kBars[bar_idx].u; // U
            uv_plane[uv_idx + 1] = kBars[bar_idx].v; // V
        }
    }

    ++frame_count_;
    return true;
}

} // namespace gestcam
