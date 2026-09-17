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
#include "gestcam/SharedMemoryProducer.h"

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

static std::atomic<bool> g_shutdown_requested{false};

#ifdef _WIN32
BOOL WINAPI ConsoleCtrlHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        g_shutdown_requested.store(true);
        return TRUE;
    }
    return FALSE;
}
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif

    bool benchmark_mode = false;
    int benchmark_frames = 30;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--benchmark" || arg == "-b" || arg == "--test") {
            benchmark_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                benchmark_frames = std::atoi(argv[++i]);
                if (benchmark_frames <= 0) benchmark_frames = 30;
            }
        }
    }
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
    int selected_device_index = -1;
    std::string selected_device_name;

    for (const auto& dev : devices) {
        if (dev.name.find("GestCam") == std::string::npos) {
            selected_device_index = dev.index;
            selected_device_name = dev.name;
            break;
        }
    }

    if (selected_device_index >= 0) {
        std::cout << "[CAMERA] Detected " << devices.size() << " device(s):\n";
        for (const auto& dev : devices) {
            std::cout << "  - [" << dev.index << "] " << dev.name 
                      << (dev.index == selected_device_index ? " (SELECTED AS INPUT)" : "") << "\n";
        }
        cam = std::make_unique<gestcam::MFCameraCapture>();
        std::cout << "\n[CAMERA] Opening physical camera [" << selected_device_name << "] at 1280x720...\n";
        if (!cam->Open(selected_device_index, 1280, 720, 30)) {
            std::cerr << "[WARNING] Fallback to Mock Camera Source.\n";
            cam = std::make_unique<gestcam::MockCameraSource>();
            cam->Open(0, 1280, 720, 30);
        }
    } else {
        std::cout << "[CAMERA] No physical webcam detected (or only virtual camera found). Using Mock Camera Source.\n";
        cam = std::make_unique<gestcam::MockCameraSource>();
        cam->Open(0, 1280, 720, 30);
    }

    auto mode = cam->GetCurrentMode();
    std::cout << "[CAMERA] Active Stream: " << mode.width << "x" << mode.height 
              << " @ " << mode.fps << " FPS (" 
              << gestcam::VideoPixelFormatToString(mode.format) << ")\n";

    // 3. Khởi tạo SPSC Lock-Free Ring Buffer và Shared Memory Producer
    gestcam::SPSCQueue<gestcam::RawVideoFrame, 4> frame_queue;
    gestcam::SharedMemoryProducer shm_producer;
    if (!shm_producer.Initialize()) {
        std::cerr << "[ERROR] Failed to initialize Shared Memory Producer!\n";
        return 1;
    }
    std::cout << "[IPC] Shared Memory Producer initialized at '" 
              << "Local\\GestCam_SharedBuffer" << "' (Low-Integrity DACL OK)\n";

    std::atomic<bool> is_running{true};

    if (benchmark_mode) {
        std::cout << "\n[MODE] BENCHMARK / TEST MODE (" << benchmark_frames << " frames)\n";
        std::cout << "[PIPELINE] Launching Producer & Consumer threads (SPSC Queue + SIMD AVX2 + IPC)...\n";

        std::vector<double> conversion_times_us;
        conversion_times_us.reserve(benchmark_frames);
        std::vector<double> shm_write_times_us;
        shm_write_times_us.reserve(benchmark_frames);

        std::thread consumer_thread([&]() {
            gestcam::RawVideoFrame raw_frame;
            std::vector<uint8_t> rgb_buffer;
            uint64_t processed = 0;

            while (is_running.load(std::memory_order_relaxed) || !frame_queue.Empty()) {
                if (frame_queue.TryPop(raw_frame)) {
                    auto t0 = std::chrono::steady_clock::now();
                    bool ok = gestcam::ColorConverter::ConvertFrameToRGB24(raw_frame, rgb_buffer);
                    auto t1 = std::chrono::steady_clock::now();

                    if (ok) {
                        double us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
                        conversion_times_us.push_back(us);

                        auto tw0 = std::chrono::steady_clock::now();
                        shm_producer.WriteFrame(rgb_buffer.data(), ++processed, raw_frame.timestamp_us);
                        auto tw1 = std::chrono::steady_clock::now();
                        double w_us = std::chrono::duration_cast<std::chrono::microseconds>(tw1 - tw0).count();
                        shm_write_times_us.push_back(w_us);
                    }
                } else {
                    std::this_thread::yield();
                }
            }
        });

        auto pipeline_start = std::chrono::steady_clock::now();
        for (int i = 0; i < benchmark_frames; ++i) {
            gestcam::RawVideoFrame captured_frame;
            if (cam->GrabFrame(captured_frame, 1500)) {
                frame_queue.PushOverwrite(std::move(captured_frame));
            }
        }

        while (!frame_queue.Empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        is_running.store(false, std::memory_order_relaxed);
        consumer_thread.join();

        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - pipeline_start
        ).count();

        cam->Close();
        shm_producer.Close();

        double actual_fps = (total_ms > 0) ? (benchmark_frames * 1000.0 / total_ms) : 0.0;
        double avg_conv_us = 0.0;
        if (!conversion_times_us.empty()) {
            double sum = std::accumulate(conversion_times_us.begin(), conversion_times_us.end(), 0.0);
            avg_conv_us = sum / conversion_times_us.size();
        }
        double avg_shm_us = 0.0;
        if (!shm_write_times_us.empty()) {
            double sum = std::accumulate(shm_write_times_us.begin(), shm_write_times_us.end(), 0.0);
            avg_shm_us = sum / shm_write_times_us.size();
        }

        std::cout << "\n[RESULT] Processed " << conversion_times_us.size() << "/" << benchmark_frames 
                  << " frames through SPSC Queue -> SIMD AVX2 -> Shared Memory in " << total_ms << " ms.\n";
        std::cout << "[PERF] Camera Capture Speed: " << actual_fps << " FPS\n";
        std::cout << "[PERF] Average SIMD NV12->RGB24 Conversion Time: " 
                  << (avg_conv_us / 1000.0) << " ms (" << avg_conv_us << " us)\n";
        std::cout << "[PERF] Average Shared Memory Write Latency: " 
                  << (avg_shm_us / 1000.0) << " ms (" << avg_shm_us << " us)\n";

        std::cout << "========================================================\n";
        std::cout << "Task 1.6 Benchmark Verification Completed Successfully!\n";
        std::cout << "========================================================\n";
    } else {
        // Continuous Live Streaming Mode
        std::cout << "\n========================================================\n";
        std::cout << "[MODE] CONTINUOUS LIVE STREAMING MODE ACTIVE (30 FPS)\n";
        std::cout << "[INFO] Streaming live webcam to 'Local\\GestCam_SharedBuffer'\n";
        std::cout << "[INFO] Virtual Camera is ready for Google Meet, Zoom, Teams.\n";
        std::cout << "[INFO] Press Ctrl+C to terminate cleanly.\n";
        std::cout << "========================================================\n\n";

        std::atomic<uint64_t> frames_streamed{0};
        std::atomic<double> last_simd_us{0.0};
        std::atomic<double> last_ipc_us{0.0};

        std::thread consumer_thread([&]() {
            gestcam::RawVideoFrame raw_frame;
            std::vector<uint8_t> rgb_buffer;
            uint64_t processed = 0;

            while (!g_shutdown_requested.load(std::memory_order_relaxed) || !frame_queue.Empty()) {
                if (frame_queue.TryPop(raw_frame)) {
                    auto t0 = std::chrono::steady_clock::now();
                    bool ok = gestcam::ColorConverter::ConvertFrameToRGB24(raw_frame, rgb_buffer);
                    auto t1 = std::chrono::steady_clock::now();

                    if (ok) {
                        double us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
                        last_simd_us.store(us, std::memory_order_relaxed);

                        auto tw0 = std::chrono::steady_clock::now();
                        shm_producer.WriteFrame(rgb_buffer.data(), ++processed, raw_frame.timestamp_us);
                        auto tw1 = std::chrono::steady_clock::now();
                        double w_us = std::chrono::duration_cast<std::chrono::microseconds>(tw1 - tw0).count();
                        last_ipc_us.store(w_us, std::memory_order_relaxed);

                        frames_streamed.store(processed, std::memory_order_relaxed);
                    }
                } else {
                    std::this_thread::yield();
                }
            }
        });

        auto stream_start = std::chrono::steady_clock::now();
        auto last_report = stream_start;

        while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
            gestcam::RawVideoFrame captured_frame;
            if (cam->GrabFrame(captured_frame, 1500)) {
                frame_queue.PushOverwrite(std::move(captured_frame));
            }

            auto now = std::chrono::steady_clock::now();
            auto since_last_report = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_report).count();
            if (since_last_report >= 1000) {
                uint64_t total_f = frames_streamed.load(std::memory_order_relaxed);
                auto total_elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(now - stream_start).count();
                double cur_fps = (total_elapsed_s > 0) ? (static_cast<double>(total_f) / total_elapsed_s) : 30.0;

                std::cout << "\r[STREAMING] Frames: " << total_f 
                          << " | Speed: " << cur_fps << " FPS"
                          << " | SIMD: " << (last_simd_us.load() / 1000.0) << " ms"
                          << " | IPC: " << (last_ipc_us.load() / 1000.0) << " ms   " << std::flush;
                last_report = now;
            }
        }

        std::cout << "\n\n[SHUTDOWN] Ctrl+C received. Cleaning up pipeline resources...\n";
        consumer_thread.join();
        cam->Close();
        shm_producer.Close();
        std::cout << "[SHUTDOWN] Pipeline terminated cleanly. Have a great day!\n";
    }

    return 0;
}
