#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace gestcam {

enum class VideoPixelFormat {
    UNKNOWN = 0,
    NV12,
    YUY2,
    MJPEG,
    RGB24
};

inline const char* VideoPixelFormatToString(VideoPixelFormat format) {
    switch (format) {
        case VideoPixelFormat::NV12:  return "NV12";
        case VideoPixelFormat::YUY2:  return "YUY2";
        case VideoPixelFormat::MJPEG: return "MJPEG";
        case VideoPixelFormat::RGB24: return "RGB24";
        default:                      return "UNKNOWN";
    }
}

struct CameraDeviceInfo {
    int index = -1;
    std::string name;
    std::string symbolic_link;
};

struct CameraFormatMode {
    int width = 0;
    int height = 0;
    int fps = 0;
    VideoPixelFormat format = VideoPixelFormat::UNKNOWN;
};

struct RawVideoFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    VideoPixelFormat format = VideoPixelFormat::UNKNOWN;
    int64_t timestamp_us = 0;
    std::vector<uint8_t> data;
};

class ICameraSource {
public:
    virtual ~ICameraSource() = default;

    virtual bool Open(int device_index, int width = 1280, int height = 720, int target_fps = 30) = 0;
    virtual void Close() = 0;
    virtual bool IsOpened() const = 0;
    virtual bool GrabFrame(RawVideoFrame& out_frame, int timeout_ms = 1000) = 0;
    virtual CameraFormatMode GetCurrentMode() const = 0;
};

} // namespace gestcam
