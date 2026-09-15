#include <gtest/gtest.h>
#include "gestcam/CameraEnumerator.h"
#include "gestcam/MockCameraSource.h"
#include "gestcam/MFCameraCapture.h"
#include <chrono>
#include <iostream>

using namespace gestcam;

TEST(CameraTest, EnumeratorCanListDevices) {
    auto devices = CameraEnumerator::EnumerateDevices();
    std::cout << "[TEST] Found " << devices.size() << " video capture devices:\n";
    for (const auto& dev : devices) {
        std::cout << "  - [" << dev.index << "] " << dev.name << " (Link: " << dev.symbolic_link << ")\n";
    }
    // Không yêu cầu bắt buộc phải có webcam vật lý, nhưng hàm gọi không được crash
    SUCCEED();
}

TEST(CameraTest, MockCameraSource_OpenAndGrab) {
    MockCameraSource mock;
    EXPECT_FALSE(mock.IsOpened());

    bool ok = mock.Open(0, 1280, 720, 30);
    ASSERT_TRUE(ok);
    EXPECT_TRUE(mock.IsOpened());

    auto mode = mock.GetCurrentMode();
    EXPECT_EQ(mode.width, 1280);
    EXPECT_EQ(mode.height, 720);
    EXPECT_EQ(mode.fps, 30);
    EXPECT_EQ(mode.format, VideoPixelFormat::NV12);

    RawVideoFrame frame;
    int64_t last_ts = -1;

    for (int i = 0; i < 5; ++i) {
        bool grabbed = mock.GrabFrame(frame);
        ASSERT_TRUE(grabbed);
        EXPECT_EQ(frame.width, 1280);
        EXPECT_EQ(frame.height, 720);
        EXPECT_EQ(frame.format, VideoPixelFormat::NV12);
        EXPECT_EQ(frame.data.size(), 1280 * 720 * 3 / 2);
        EXPECT_GT(frame.timestamp_us, last_ts);
        last_ts = frame.timestamp_us;
    }

    mock.Close();
    EXPECT_FALSE(mock.IsOpened());
}

TEST(CameraTest, MockCameraSource_Pacing) {
    MockCameraSource mock;
    ASSERT_TRUE(mock.Open(0, 1280, 720, 60)); // 60 FPS -> ~16.6ms / frame

    RawVideoFrame frame;
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 6; ++i) {
        ASSERT_TRUE(mock.GrabFrame(frame));
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    ).count();

    // 6 frames tại 60 FPS phải mất ít nhất ~80 ms
    EXPECT_GE(elapsed, 70);
    mock.Close();
}

TEST(CameraTest, MFCameraCapture_LifecycleIfHardwarePresent) {
    auto devices = CameraEnumerator::EnumerateDevices();
    if (devices.empty()) {
        std::cout << "[INFO] No physical webcam found on machine. Skipping hardware capture test.\n";
        SUCCEED();
        return;
    }

    std::cout << "[INFO] Testing physical camera: " << devices[0].name << "\n";
    MFCameraCapture cam;
    bool ok = cam.Open(devices[0].index, 1280, 720, 30);
    ASSERT_TRUE(ok) << "Failed to open camera index 0 via Media Foundation.";
    EXPECT_TRUE(cam.IsOpened());

    auto mode = cam.GetCurrentMode();
    std::cout << "[INFO] Negotiated Format: " << mode.width << "x" << mode.height 
              << " @ " << mode.fps << " FPS (" << VideoPixelFormatToString(mode.format) << ")\n";

    EXPECT_GT(mode.width, 0);
    EXPECT_GT(mode.height, 0);

    RawVideoFrame frame;
    // Thử lấy 3 frame thực tế từ camera
    bool got_any = false;
    for (int i = 0; i < 3; ++i) {
        if (cam.GrabFrame(frame, 2000)) {
            got_any = true;
            EXPECT_GT(frame.data.size(), 0u);
            EXPECT_EQ(frame.width, mode.width);
            EXPECT_EQ(frame.height, mode.height);
        }
    }

    cam.Close();
    EXPECT_FALSE(cam.IsOpened());
    EXPECT_TRUE(got_any) << "Could not grab any frame from physical camera.";
}
