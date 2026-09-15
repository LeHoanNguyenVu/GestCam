#pragma once

#include "gestcam/CameraTypes.h"

struct IMFSourceReader;
struct IMFMediaSource;

namespace gestcam {

class MFCameraCapture : public ICameraSource {
public:
    MFCameraCapture();
    ~MFCameraCapture() override;

    bool Open(int device_index, int width = 1280, int height = 720, int target_fps = 30) override;
    void Close() override;
    bool IsOpened() const override { return is_opened_; }
    bool GrabFrame(RawVideoFrame& out_frame, int timeout_ms = 1000) override;
    CameraFormatMode GetCurrentMode() const override { return current_mode_; }

private:
    bool is_opened_ = false;
    bool co_initialized_ = false;
    int device_index_ = -1;
    CameraFormatMode current_mode_;

    IMFSourceReader* reader_ = nullptr;
    IMFMediaSource* source_ = nullptr;
};

} // namespace gestcam
