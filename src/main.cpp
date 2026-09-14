#include <iostream>
#include "gestcam/Version.h"

#ifdef _WIN32
    #include <windows.h>
    #include <mfapi.h>
    #ifdef _MSC_VER
        #include <intrin.h>
    #else
        #include <cpuid.h>
    #endif
#endif

bool CheckHardwareAVX2() {
#if defined(_MSC_VER)
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 0);
    int nIds = cpuInfo[0];
    if (nIds >= 7) {
        __cpuidex(cpuInfo, 7, 0);
        return (cpuInfo[1] & (1 << 5)) != 0; // Bit 5 của EBX là cờ AVX2
    }
    return false;
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "          GestCam Core - Version " << gestcam::SystemInfo::kVersion << "\n";
    std::cout << "========================================================\n";

    // 1. Kiểm tra C++ Standard
#if defined(_MSVC_LANG)
    std::cout << "[INFO] C++ Standard (MSVC): " << _MSVC_LANG << "\n";
#else
    std::cout << "[INFO] C++ Standard (__cplusplus): " << __cplusplus << "\n";
#endif

    // 2. Kiểm tra phần cứng AVX2
    bool hw_avx2 = CheckHardwareAVX2();
    std::cout << "[INFO] Compiler AVX2 Flag: " 
              << (gestcam::SystemInfo::kCompiledWithAVX2 ? "ENABLED" : "DISABLED") << "\n";
    std::cout << "[INFO] CPU Hardware AVX2 Support: " 
              << (hw_avx2 ? "SUPPORTED (OK)" : "NOT SUPPORTED (FALLBACK REQUIRED)") << "\n";

    // 3. Khởi tạo thử Media Foundation API
    HRESULT hr = MFStartup(MF_VERSION);
    if (SUCCEEDED(hr)) {
        std::cout << "[INFO] Windows Media Foundation: INITIALIZED SUCCESSFULLY\n";
        MFShutdown();
    } else {
        std::cerr << "[ERROR] Windows Media Foundation failed to initialize. HRESULT: 0x" 
                  << std::hex << hr << "\n";
        return 1;
    }

    std::cout << "========================================================\n";
    std::cout << "GestCam Build System & Runtime Environment Verified OK!\n";
    std::cout << "========================================================\n";
    return 0;
}
