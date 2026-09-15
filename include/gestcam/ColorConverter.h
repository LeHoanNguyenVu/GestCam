#pragma once

#include "gestcam/CameraTypes.h"
#include <cstdint>
#include <vector>

namespace gestcam {

class ColorConverter {
public:
    // NV12 -> RGB24 (Scalar Reference & AVX2 Vectorized)
    static void NV12_to_RGB24_Scalar(const uint8_t* nv12, uint8_t* rgb, int width, int height);
    static void NV12_to_RGB24_AVX2(const uint8_t* nv12, uint8_t* rgb, int width, int height);

    // YUY2 -> RGB24 (Scalar Reference & AVX2 Vectorized)
    static void YUY2_to_RGB24_Scalar(const uint8_t* yuy2, uint8_t* rgb, int width, int height);
    static void YUY2_to_RGB24_AVX2(const uint8_t* yuy2, uint8_t* rgb, int width, int height);

    // Tự động chọn thuật toán tối ưu nhất theo phần cứng hiện tại
    static bool ConvertFrameToRGB24(const RawVideoFrame& in_frame, std::vector<uint8_t>& out_rgb24);
};

} // namespace gestcam
