#pragma once

#define GESTCAM_VERSION_MAJOR 1
#define GESTCAM_VERSION_MINOR 1
#define GESTCAM_VERSION_PATCH 0
#define GESTCAM_VERSION_STRING "1.1.0"

// Kiểm tra hỗ trợ tập lệnh AVX2 lúc biên dịch
#if defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))
    #define GESTCAM_HAS_AVX2 1
#else
    #define GESTCAM_HAS_AVX2 0
#endif

namespace gestcam {
    struct SystemInfo {
        static constexpr const char* kVersion = GESTCAM_VERSION_STRING;
        static constexpr bool kCompiledWithAVX2 = (GESTCAM_HAS_AVX2 == 1);
    };
}
