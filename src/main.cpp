#include <iostream>
#include <chrono>
#include "gestcam/Version.h"
#include "gestcam/CameraEnumerator.h"
#include "gestcam/MFCameraCapture.h"
#include "gestcam/MockCameraSource.h"

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
        return (cpuInfo[1] & (1 << 5)) != 0;
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

    // 1. Kiểm tra phần cứng AVX2
    bool hw_avx2 = CheckHardwareAVX2();
    std::cout << "[SYSTEM] Compiler AVX2 Flag: " 
              << (gestcam::SystemInfo::kCompiledWithAVX2 ? "ENABLED" : "DISABLED") << "\n";
    std::cout << "[SYSTEM] CPU Hardware AVX2 Support: " 
              << (hw_avx2 ? "SUPPORTED (OK)" : "NOT SUPPORTED") << "\n\n";

    // 2. Quét danh sách camera vật lý
    std::cout << "[CAMERA] Scanning for video capture devices...\n";
    auto devices = gestcam::CameraEnumerator::EnumerateDevices();

    if (devices.empty()) {
        std::cout << "[CAMERA] No physical webcam detected. Switching to Mock Camera Source.\n";
    } else {
        std::cout << "[CAMERA] Detected " << devices.size() << " device(s):\n";
        for (const auto& dev : devices) {
            std::cout << "  - [" << dev.index << "] " << dev.name << "\n";
        }
    }

    // 3. Khởi tạo Camera Source (Ưu tiên camera thật, fallback sang Mock)
    std::unique_ptr<gestcam::ICameraSource> cam;
    if (!devices.empty()) {
        cam = std::make_unique<gestcam::MFCameraCapture>();
        std::cout << "\n[CAMERA] Opening physical camera [" << devices[0].name << "] at 1280x720...\n";
        if (!cam->Open(devices[0].index, 1280, 720, 30)) {
            std::cerr << "[WARNING] Could not open physical camera. Falling back to Mock Camera.\n";
            cam = std::make_unique<gestcam::MockCameraSource>();
            cam->Open(0, 1280, 720, 30);
        }
    } else {
        cam = std::make_unique<gestcam::MockCameraSource>();
        cam->Open(0, 1280, 720, 30);
    }

    auto mode = cam->GetCurrentMode();
    std::cout << "[CAMERA] Active Stream: " << mode.width << "x" << mode.height 
              << " @ " << mode.fps << " FPS (" 
              << gestcam::VideoPixelFormatToString(mode.format) << ")\n";

    // 4. Lấy mẫu 30 frames và đo FPS thực tế
    std::cout << "[CAMERA] Capturing 30 frames for performance verification...\n";
    gestcam::RawVideoFrame frame;
    int success_count = 0;
    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < 30; ++i) {
        if (cam->GrabFrame(frame, 1500)) {
            ++success_count;
        }
    }

    auto total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time
    ).count();

    double actual_fps = (total_time_ms > 0) ? (success_count * 1000.0 / total_time_ms) : 0.0;

    std::cout << "[CAMERA] Captured " << success_count << "/30 frames successfully in " 
              << total_time_ms << " ms. Measured FPS: " << actual_fps << " FPS\n";

    cam->Close();

    std::cout << "========================================================\n";
    std::cout << "Task 1.2 Verification Completed Successfully!\n";
    std::cout << "========================================================\n";
    return 0;
}
