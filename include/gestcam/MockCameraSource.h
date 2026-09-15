#pragma once

#include "gestcam/CameraTypes.h"
#include <chrono>

namespace gestcam {

class MockCameraSource : public ICameraSource {
public:
    MockCameraSource();
    ~MockCameraSource() override;

    bool Open(int device_index, int width = 1280, int height = 720, int target_fps = 30) override;
    void Close() override;
    bool IsOpened() const override { return is_opened_; }
    bool GrabFrame(RawVideoFrame& out_frame, int timeout_ms = 1000) override;
    CameraFormatMode GetCurrentMode() const override { return current_mode_; }

    void SetSyntheticPattern(bool animated) { animated_ = animated; }

private:
    bool is_opened_ = false;
    bool animated_ = true;
    CameraFormatMode current_mode_;
    std::chrono::steady_clock::time_point last_frame_time_;
    uint64_t frame_count_ = 0;
};

} // namespace gestcam
