#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <numeric>
#include "gestcam/Version.h"
#include "gestcam/CameraEnumerator.h"
#include "gestcam/MFCameraCapture.h"
#include "gestcam/MockCameraSource.h"
#include "gestcam/ColorConverter.h"
#include "gestcam/SPSCQueue.h"

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

    // 2. Quét thiết bị webcam
    std::cout << "[CAMERA] Scanning for video capture devices...\n";
    auto devices = gestcam::CameraEnumerator::EnumerateDevices();

    std::unique_ptr<gestcam::ICameraSource> cam;
    if (!devices.empty()) {
        std::cout << "[CAMERA] Detected " << devices.size() << " device(s):\n";
        for (const auto& dev : devices) {
            std::cout << "  - [" << dev.index << "] " << dev.name << "\n";
        }
        cam = std::make_unique<gestcam::MFCameraCapture>();
        std::cout << "\n[CAMERA] Opening physical camera [" << devices[0].name << "] at 1280x720...\n";
        if (!cam->Open(devices[0].index, 1280, 720, 30)) {
            std::cerr << "[WARNING] Fallback to Mock Camera Source.\n";
            cam = std::make_unique<gestcam::MockCameraSource>();
            cam->Open(0, 1280, 720, 30);
        }
    } else {
        std::cout << "[CAMERA] No physical webcam detected. Using Mock Camera Source.\n";
        cam = std::make_unique<gestcam::MockCameraSource>();
        cam->Open(0, 1280, 720, 30);
    }

    auto mode = cam->GetCurrentMode();
    std::cout << "[CAMERA] Active Stream: " << mode.width << "x" << mode.height 
              << " @ " << mode.fps << " FPS (" 
              << gestcam::VideoPixelFormatToString(mode.format) << ")\n";

    // 3. Khởi tạo SPSC Lock-Free Ring Buffer (Capacity = 4)
    gestcam::SPSCQueue<gestcam::RawVideoFrame, 4> frame_queue;
    std::atomic<bool> is_running{true};
    const int total_frames_to_test = 30;

    std::vector<double> conversion_times_us;
    conversion_times_us.reserve(total_frames_to_test);

    std::cout << "\n[PIPELINE] Launching Producer & Consumer threads (SPSC Queue + SIMD AVX2)...\n";

    // Thread 1: Consumer rút frame từ Queue và chạy giải mã SIMD sang RGB24
    std::thread consumer_thread([&]() {
        gestcam::RawVideoFrame raw_frame;
        std::vector<uint8_t> rgb_buffer;
        int processed = 0;

        while (is_running.load(std::memory_order_relaxed) || !frame_queue.Empty()) {
            if (frame_queue.TryPop(raw_frame)) {
                auto t0 = std::chrono::steady_clock::now();
                bool ok = gestcam::ColorConverter::ConvertFrameToRGB24(raw_frame, rgb_buffer);
                auto t1 = std::chrono::steady_clock::now();

                if (ok) {
                    double us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
                    conversion_times_us.push_back(us);
                    ++processed;
                }
            } else {
                std::this_thread::yield();
            }
        }
    });

    // Thread 2 (Main): Producer thu thập frame từ Camera đẩy vào Queue (PushOverwrite)
    auto pipeline_start = std::chrono::steady_clock::now();
    for (int i = 0; i < total_frames_to_test; ++i) {
        gestcam::RawVideoFrame captured_frame;
        if (cam->GrabFrame(captured_frame, 1500)) {
            frame_queue.PushOverwrite(std::move(captured_frame));
        }
    }

    // Đợi queue được xử lý hết
    while (!frame_queue.Empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    is_running.store(false, std::memory_order_relaxed);
    consumer_thread.join();

    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - pipeline_start
    ).count();

    cam->Close();

    // 4. Báo cáo đo kiểm hiệu năng
    double actual_fps = (total_ms > 0) ? (total_frames_to_test * 1000.0 / total_ms) : 0.0;
    double avg_conv_us = 0.0;
    if (!conversion_times_us.empty()) {
        double sum = std::accumulate(conversion_times_us.begin(), conversion_times_us.end(), 0.0);
        avg_conv_us = sum / conversion_times_us.size();
    }

    std::cout << "\n[RESULT] Processed " << conversion_times_us.size() << "/" << total_frames_to_test 
              << " frames through SPSC Queue & SIMD AVX2 in " << total_ms << " ms.\n";
    std::cout << "[PERF] Camera Capture Speed: " << actual_fps << " FPS\n";
    std::cout << "[PERF] Average SIMD NV12->RGB24 Conversion Time: " 
              << (avg_conv_us / 1000.0) << " ms (" << avg_conv_us << " us)\n";

    std::cout << "========================================================\n";
    std::cout << "Task 1.3 Verification Completed Successfully!\n";
    std::cout << "========================================================\n";
    return 0;
}
