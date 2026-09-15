#include <gtest/gtest.h>
#include "gestcam/ColorConverter.h"
#include "gestcam/SPSCQueue.h"
#include "gestcam/MockCameraSource.h"
#include <chrono>
#include <thread>
#include <vector>
#include <cmath>
#include <iostream>

using namespace gestcam;

// ==========================================
// 1. CÁC BÀI TEST CHUYỂN ĐỔI COLOR SPACE SIMD
// ==========================================

TEST(ColorConverterTest, NV12_ColorAccuracy) {
    const int w = 4;
    const int h = 2;
    std::vector<uint8_t> nv12(w * h * 3 / 2, 128);

    // Điểm ảnh đầu tiên: Màu trắng chuẩn (Y=235, U=128, V=128)
    nv12[0] = 235; // Y
    nv12[w * h + 0] = 128; // U
    nv12[w * h + 1] = 128; // V

    // Điểm ảnh thứ hai: Màu đen chuẩn (Y=16, U=128, V=128)
    nv12[1] = 16;  // Y

    std::vector<uint8_t> rgb(w * h * 3, 0);
    ColorConverter::NV12_to_RGB24_Scalar(nv12.data(), rgb.data(), w, h);

    // Kiểm tra pixel 0 (Gần trắng: ~235)
    EXPECT_GE(rgb[0], 230);
    EXPECT_GE(rgb[1], 230);
    EXPECT_GE(rgb[2], 230);

    // Kiểm tra pixel 1 (Gần đen: ~16)
    EXPECT_LE(rgb[3], 20);
    EXPECT_LE(rgb[4], 20);
    EXPECT_LE(rgb[5], 20);
}

TEST(ColorConverterTest, NV12_AVX2_Matches_Scalar) {
    const int w = 1280;
    const int h = 720;
    std::vector<uint8_t> nv12(w * h * 3 / 2);

    // Tạo dữ liệu gradient đa dạng trên toàn dải [0, 255]
    for (size_t i = 0; i < nv12.size(); ++i) {
        nv12[i] = static_cast<uint8_t>((i * 7 + 13) % 256);
    }

    std::vector<uint8_t> rgb_scalar(w * h * 3, 0);
    std::vector<uint8_t> rgb_avx2(w * h * 3, 0);

    ColorConverter::NV12_to_RGB24_Scalar(nv12.data(), rgb_scalar.data(), w, h);
    ColorConverter::NV12_to_RGB24_AVX2(nv12.data(), rgb_avx2.data(), w, h);

    // So sánh từng pixel (cho phép sai số làm tròn tối đa 1 LSB)
    int max_diff = 0;
    for (size_t i = 0; i < rgb_scalar.size(); ++i) {
        int diff = std::abs(static_cast<int>(rgb_scalar[i]) - static_cast<int>(rgb_avx2[i]));
        if (diff > max_diff) max_diff = diff;
    }

    EXPECT_LE(max_diff, 1) << "AVX2 output differs from Scalar reference by more than 1 LSB!";
}

TEST(ColorConverterTest, YUY2_AVX2_Matches_Scalar) {
    const int w = 1280;
    const int h = 720;
    std::vector<uint8_t> yuy2(w * h * 2);

    for (size_t i = 0; i < yuy2.size(); ++i) {
        yuy2[i] = static_cast<uint8_t>((i * 11 + 37) % 256);
    }

    std::vector<uint8_t> rgb_scalar(w * h * 3, 0);
    std::vector<uint8_t> rgb_avx2(w * h * 3, 0);

    ColorConverter::YUY2_to_RGB24_Scalar(yuy2.data(), rgb_scalar.data(), w, h);
    ColorConverter::YUY2_to_RGB24_AVX2(yuy2.data(), rgb_avx2.data(), w, h);

    int max_diff = 0;
    for (size_t i = 0; i < rgb_scalar.size(); ++i) {
        int diff = std::abs(static_cast<int>(rgb_scalar[i]) - static_cast<int>(rgb_avx2[i]));
        if (diff > max_diff) max_diff = diff;
    }

    EXPECT_LE(max_diff, 1) << "YUY2 AVX2 output differs from Scalar reference!";
}

TEST(ColorConverterTest, Benchmark_NV12_AVX2) {
    const int w = 1280;
    const int h = 720;
    std::vector<uint8_t> nv12(w * h * 3 / 2, 128);
    std::vector<uint8_t> rgb(w * h * 3, 0);

    const int iterations = 100;
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < iterations; ++i) {
        ColorConverter::NV12_to_RGB24_AVX2(nv12.data(), rgb.data(), w, h);
    }

    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start
    ).count();

    double avg_ms = (elapsed_us / 1000.0) / iterations;
    std::cout << "[BENCHMARK] Average NV12->RGB24 AVX2 conversion time: " 
              << avg_ms << " ms / frame (Target <= 1.0 ms)\n";

    EXPECT_LE(avg_ms, 1.0);
}

// ==========================================
// 2. CÁC BÀI TEST SPSC LOCK-FREE RING BUFFER
// ==========================================

TEST(SPSCQueueTest, SingleThread_FIFO) {
    SPSCQueue<int, 4> queue;
    EXPECT_TRUE(queue.Empty());
    EXPECT_EQ(queue.Size(), 0u);

    EXPECT_TRUE(queue.TryPush(10));
    EXPECT_TRUE(queue.TryPush(20));
    EXPECT_TRUE(queue.TryPush(30));
    EXPECT_TRUE(queue.TryPush(40));

    // Đã đầy (Capacity = 4)
    EXPECT_FALSE(queue.TryPush(50));
    EXPECT_EQ(queue.Size(), 4u);

    int val = 0;
    EXPECT_TRUE(queue.TryPop(val)); EXPECT_EQ(val, 10);
    EXPECT_TRUE(queue.TryPop(val)); EXPECT_EQ(val, 20);
    EXPECT_TRUE(queue.TryPop(val)); EXPECT_EQ(val, 30);
    EXPECT_TRUE(queue.TryPop(val)); EXPECT_EQ(val, 40);

    // Đã rỗng
    EXPECT_FALSE(queue.TryPop(val));
    EXPECT_TRUE(queue.Empty());
}

TEST(SPSCQueueTest, DropOldest_ZeroLatency) {
    SPSCQueue<int, 4> queue;

    // Đẩy vượt quá sức chứa bằng PushOverwrite
    for (int i = 1; i <= 8; ++i) {
        queue.PushOverwrite(i);
    }

    EXPECT_EQ(queue.Size(), 4u);

    // Chỉ còn 4 phần tử mới nhất: 5, 6, 7, 8
    int val = 0;
    EXPECT_TRUE(queue.TryPop(val)); EXPECT_EQ(val, 5);
    EXPECT_TRUE(queue.TryPop(val)); EXPECT_EQ(val, 6);
    EXPECT_TRUE(queue.TryPop(val)); EXPECT_EQ(val, 7);
    EXPECT_TRUE(queue.TryPop(val)); EXPECT_EQ(val, 8);
    EXPECT_TRUE(queue.Empty());
}

TEST(SPSCQueueTest, Concurrency_Stress_500k) {
    SPSCQueue<uint64_t, 8> queue;
    const uint64_t total_items = 500'000;

    // Thread 1: Producer đẩy 500k phần tử liên tục
    std::thread producer([&]() {
        for (uint64_t i = 1; i <= total_items; ++i) {
            while (!queue.TryPush(i)) {
                std::this_thread::yield();
            }
        }
    });

    // Thread 2: Consumer rút 500k phần tử và kiểm tra thứ tự
    uint64_t last_val = 0;
    uint64_t consumed_count = 0;

    std::thread consumer([&]() {
        uint64_t val = 0;
        while (consumed_count < total_items) {
            if (queue.TryPop(val)) {
                EXPECT_EQ(val, last_val + 1) << "FIFO ordering violated or item corrupted!";
                last_val = val;
                ++consumed_count;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(consumed_count, total_items);
    EXPECT_TRUE(queue.Empty());
}
