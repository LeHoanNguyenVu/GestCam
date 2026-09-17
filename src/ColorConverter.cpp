#include "gestcam/ColorConverter.h"
#include "gestcam/Version.h"
#include <algorithm>
#include <cstring>

#if defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))
#include <immintrin.h>
#define COMPILED_WITH_AVX2 1
#else
#define COMPILED_WITH_AVX2 0
#endif

namespace gestcam {

namespace {

inline uint8_t ClampByte(int val) {
    return static_cast<uint8_t>(std::clamp(val, 0, 255));
}

// Thuật toán chuyển đổi YUV BT.601 full-range
// Được phân rã để không bao giờ tràn số 16-bit signed integer (Max: 25146 <= 32767)
inline void YUV2RGB(int y, int u, int v, uint8_t& r, uint8_t& g, uint8_t& b) {
    int d = u - 128;
    int e = v - 128;

    int r_val = y + e + ((103 * e + 128) >> 8);
    int g_val = y - (e + ((88 * d - 73 * e - 128) >> 8));
    int b_val = y + d + ((198 * d + 128) >> 8);

    r = ClampByte(r_val);
    g = ClampByte(g_val);
    b = ClampByte(b_val);
}

} // namespace

void ColorConverter::NV12_to_RGB24_Scalar(const uint8_t* nv12, uint8_t* rgb, int width, int height) {
    if (!nv12 || !rgb || width <= 0 || height <= 0) return;

    const uint8_t* y_plane = nv12;
    const uint8_t* uv_plane = nv12 + (width * height);

    for (int y = 0; y < height; ++y) {
        const uint8_t* y_row = y_plane + (y * width);
        const uint8_t* uv_row = uv_plane + ((y / 2) * width);
        uint8_t* rgb_row = rgb + (y * width * 3);

        for (int x = 0; x < width; ++x) {
            int y_val = y_row[x];
            int uv_idx = (x / 2) * 2;
            int u_val = uv_row[uv_idx];
            int v_val = uv_row[uv_idx + 1];

            YUV2RGB(y_val, u_val, v_val, rgb_row[x * 3 + 2], rgb_row[x * 3 + 1], rgb_row[x * 3]);
        }
    }
}

void ColorConverter::NV12_to_RGB24_AVX2(const uint8_t* nv12, uint8_t* rgb, int width, int height) {
#if COMPILED_WITH_AVX2
    if (!nv12 || !rgb || width <= 0 || height <= 0) return;

    const uint8_t* y_plane = nv12;
    const uint8_t* uv_plane = nv12 + (width * height);

    const __m256i v_128 = _mm256_set1_epi16(128);
    const __m256i v_c103 = _mm256_set1_epi16(103);
    const __m256i v_c198 = _mm256_set1_epi16(198);
    const __m256i v_c88  = _mm256_set1_epi16(88);
    const __m256i v_c73  = _mm256_set1_epi16(73);

    for (int y = 0; y < height; ++y) {
        const uint8_t* y_row = y_plane + (y * width);
        const uint8_t* uv_row = uv_plane + ((y / 2) * width);
        uint8_t* rgb_row = rgb + (y * width * 3);

        int x = 0;
        for (; x <= width - 16; x += 16) {
            __m128i y_raw = _mm_loadu_si128(reinterpret_cast<const __m128i*>(y_row + x));
            __m256i y16 = _mm256_cvtepu8_epi16(y_raw);

            __m128i uv_raw = _mm_loadu_si128(reinterpret_cast<const __m128i*>(uv_row + x));

            alignas(16) uint8_t uv_buf[16];
            _mm_storeu_si128(reinterpret_cast<__m128i*>(uv_buf), uv_raw);

            alignas(32) int16_t u_exp[16];
            alignas(32) int16_t v_exp[16];
            for (int k = 0; k < 8; ++k) {
                int16_t u = static_cast<int16_t>(uv_buf[k * 2]);
                int16_t v = static_cast<int16_t>(uv_buf[k * 2 + 1]);
                u_exp[k * 2]     = u;
                u_exp[k * 2 + 1] = u;
                v_exp[k * 2]     = v;
                v_exp[k * 2 + 1] = v;
            }

            __m256i u16 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(u_exp));
            __m256i v16 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(v_exp));

            __m256i d_u = _mm256_sub_epi16(u16, v_128);
            __m256i d_v = _mm256_sub_epi16(v16, v_128);

            // R = Y + e + ((103 * e + 128) >> 8)
            __m256i r_term = _mm256_add_epi16(d_v, _mm256_srai_epi16(_mm256_add_epi16(_mm256_mullo_epi16(v_c103, d_v), v_128), 8));
            __m256i r16 = _mm256_add_epi16(y16, r_term);

            // G = Y - (e + ((88 * d - 73 * e - 128) >> 8))
            __m256i g_inner = _mm256_sub_epi16(_mm256_sub_epi16(_mm256_mullo_epi16(v_c88, d_u), _mm256_mullo_epi16(v_c73, d_v)), v_128);
            __m256i g_term = _mm256_add_epi16(d_v, _mm256_srai_epi16(g_inner, 8));
            __m256i g16 = _mm256_sub_epi16(y16, g_term);

            // B = Y + d + ((198 * d + 128) >> 8)
            __m256i b_term = _mm256_add_epi16(d_u, _mm256_srai_epi16(_mm256_add_epi16(_mm256_mullo_epi16(v_c198, d_u), v_128), 8));
            __m256i b16 = _mm256_add_epi16(y16, b_term);

            alignas(32) int16_t r_arr[16], g_arr[16], b_arr[16];
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(r_arr), r16);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(g_arr), g16);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(b_arr), b16);

            // Windows DIB MEDIASUBTYPE_RGB24 expects B, G, R byte order
            for (int k = 0; k < 16; ++k) {
                int px = (x + k) * 3;
                rgb_row[px]     = ClampByte(b_arr[k]); // B
                rgb_row[px + 1] = ClampByte(g_arr[k]); // G
                rgb_row[px + 2] = ClampByte(r_arr[k]); // R
            }
        }

        for (; x < width; ++x) {
            int y_val = y_row[x];
            int uv_idx = (x / 2) * 2;
            int u_val = uv_row[uv_idx];
            int v_val = uv_row[uv_idx + 1];
            YUV2RGB(y_val, u_val, v_val, rgb_row[x * 3 + 2], rgb_row[x * 3 + 1], rgb_row[x * 3]);
        }
    }
#else
    NV12_to_RGB24_Scalar(nv12, rgb, width, height);
#endif
}

void ColorConverter::YUY2_to_RGB24_Scalar(const uint8_t* yuy2, uint8_t* rgb, int width, int height) {
    if (!yuy2 || !rgb || width <= 0 || height <= 0) return;

    for (int y = 0; y < height; ++y) {
        const uint8_t* yuy2_row = yuy2 + (y * width * 2);
        uint8_t* rgb_row = rgb + (y * width * 3);

        for (int x = 0; x < width; x += 2) {
            int y0 = yuy2_row[x * 2];
            int u  = yuy2_row[x * 2 + 1];
            int y1 = yuy2_row[x * 2 + 2];
            int v  = yuy2_row[x * 2 + 3];

            YUV2RGB(y0, u, v, rgb_row[x * 3 + 2], rgb_row[x * 3 + 1], rgb_row[x * 3]);
            YUV2RGB(y1, u, v, rgb_row[(x + 1) * 3 + 2], rgb_row[(x + 1) * 3 + 1], rgb_row[(x + 1) * 3]);
        }
    }
}

void ColorConverter::YUY2_to_RGB24_AVX2(const uint8_t* yuy2, uint8_t* rgb, int width, int height) {
#if COMPILED_WITH_AVX2
    if (!yuy2 || !rgb || width <= 0 || height <= 0) return;

    const __m256i v_128 = _mm256_set1_epi16(128);
    const __m256i v_c103 = _mm256_set1_epi16(103);
    const __m256i v_c198 = _mm256_set1_epi16(198);
    const __m256i v_c88  = _mm256_set1_epi16(88);
    const __m256i v_c73  = _mm256_set1_epi16(73);

    for (int y = 0; y < height; ++y) {
        const uint8_t* yuy2_row = yuy2 + (y * width * 2);
        uint8_t* rgb_row = rgb + (y * width * 3);

        int x = 0;
        for (; x <= width - 16; x += 16) {
            __m256i raw = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(yuy2_row + x * 2));
            alignas(32) uint8_t buf[32];
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(buf), raw);

            alignas(32) int16_t y_exp[16];
            alignas(32) int16_t u_exp[16];
            alignas(32) int16_t v_exp[16];

            for (int k = 0; k < 8; ++k) {
                int base = k * 4;
                int16_t y0 = buf[base];
                int16_t u  = buf[base + 1];
                int16_t y1 = buf[base + 2];
                int16_t v  = buf[base + 3];

                y_exp[k * 2]     = y0;
                y_exp[k * 2 + 1] = y1;
                u_exp[k * 2]     = u;
                u_exp[k * 2 + 1] = u;
                v_exp[k * 2]     = v;
                v_exp[k * 2 + 1] = v;
            }

            __m256i y16 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(y_exp));
            __m256i u16 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(u_exp));
            __m256i v16 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(v_exp));

            __m256i d_u = _mm256_sub_epi16(u16, v_128);
            __m256i d_v = _mm256_sub_epi16(v16, v_128);

            // R = Y + e + ((103 * e + 128) >> 8)
            __m256i r_term = _mm256_add_epi16(d_v, _mm256_srai_epi16(_mm256_add_epi16(_mm256_mullo_epi16(v_c103, d_v), v_128), 8));
            __m256i r16 = _mm256_add_epi16(y16, r_term);

            // G = Y - (e + ((88 * d - 73 * e - 128) >> 8))
            __m256i g_inner = _mm256_sub_epi16(_mm256_sub_epi16(_mm256_mullo_epi16(v_c88, d_u), _mm256_mullo_epi16(v_c73, d_v)), v_128);
            __m256i g_term = _mm256_add_epi16(d_v, _mm256_srai_epi16(g_inner, 8));
            __m256i g16 = _mm256_sub_epi16(y16, g_term);

            // B = Y + d + ((198 * d + 128) >> 8)
            __m256i b_term = _mm256_add_epi16(d_u, _mm256_srai_epi16(_mm256_add_epi16(_mm256_mullo_epi16(v_c198, d_u), v_128), 8));
            __m256i b16 = _mm256_add_epi16(y16, b_term);

            alignas(32) int16_t r_arr[16], g_arr[16], b_arr[16];
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(r_arr), r16);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(g_arr), g16);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(b_arr), b16);

            // Windows DIB MEDIASUBTYPE_RGB24 expects B, G, R byte order
            for (int k = 0; k < 16; ++k) {
                int px = (x + k) * 3;
                rgb_row[px]     = ClampByte(b_arr[k]); // B
                rgb_row[px + 1] = ClampByte(g_arr[k]); // G
                rgb_row[px + 2] = ClampByte(r_arr[k]); // R
            }
        }

        for (; x < width; x += 2) {
            int y0 = yuy2_row[x * 2];
            int u  = yuy2_row[x * 2 + 1];
            int y1 = yuy2_row[x * 2 + 2];
            int v  = yuy2_row[x * 2 + 3];

            YUV2RGB(y0, u, v, rgb_row[x * 3 + 2], rgb_row[x * 3 + 1], rgb_row[x * 3]);
            YUV2RGB(y1, u, v, rgb_row[(x + 1) * 3 + 2], rgb_row[(x + 1) * 3 + 1], rgb_row[(x + 1) * 3]);
        }
    }
#else
    YUY2_to_RGB24_Scalar(yuy2, rgb, width, height);
#endif
}

bool ColorConverter::ConvertFrameToRGB24(const RawVideoFrame& in_frame, std::vector<uint8_t>& out_rgb24) {
    if (in_frame.data.empty() || in_frame.width <= 0 || in_frame.height <= 0) return false;

    size_t rgb_size = static_cast<size_t>(in_frame.width * in_frame.height * 3);
    out_rgb24.resize(rgb_size);

    if (in_frame.format == VideoPixelFormat::NV12) {
#if COMPILED_WITH_AVX2
        NV12_to_RGB24_AVX2(in_frame.data.data(), out_rgb24.data(), in_frame.width, in_frame.height);
#else
        NV12_to_RGB24_Scalar(in_frame.data.data(), out_rgb24.data(), in_frame.width, in_frame.height);
#endif
        return true;
    }

    if (in_frame.format == VideoPixelFormat::YUY2) {
#if COMPILED_WITH_AVX2
        YUY2_to_RGB24_AVX2(in_frame.data.data(), out_rgb24.data(), in_frame.width, in_frame.height);
#else
        YUY2_to_RGB24_Scalar(in_frame.data.data(), out_rgb24.data(), in_frame.width, in_frame.height);
#endif
        return true;
    }

    if (in_frame.format == VideoPixelFormat::RGB24) {
        std::memcpy(out_rgb24.data(), in_frame.data.data(), rgb_size);
        return true;
    }

    return false;
}

} // namespace gestcam
