#include <gtest/gtest.h>
#include "gestcam/SharedMemoryProducer.h"
#include "gestcam/SharedMemoryConsumer.h"
#include <sddl.h>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <numeric>

using namespace gestcam;

// Test 1: Verify SDDL String and Security Descriptor Generation
TEST(SharedMemoryTest, SecurityDescriptor_Valid) {
    PSECURITY_DESCRIPTOR pSD = nullptr;
    BOOL res = ConvertStringSecurityDescriptorToSecurityDescriptorW(
        GESTCAM_SDDL_PERMISSIVE,
        SDDL_REVISION_1,
        &pSD,
        nullptr
    );
    EXPECT_TRUE(res);
    EXPECT_NE(pSD, nullptr);

    if (pSD) {
        LocalFree(pSD);
    }
}

// Test 2: Producer Lifecycle and Header Initialization
TEST(SharedMemoryTest, Producer_Lifecycle) {
    const std::wstring test_shm_name = L"Local\\GestCam_Test_Lifecycle";
    SharedMemoryProducer producer;

    EXPECT_FALSE(producer.IsInitialized());
    EXPECT_TRUE(producer.Initialize(test_shm_name));
    EXPECT_TRUE(producer.IsInitialized());

    producer.Close();
    EXPECT_FALSE(producer.IsInitialized());
}

// Test 3: Bit-exact Transmission of a Single 720p Frame
TEST(SharedMemoryTest, ProducerConsumer_SingleFrame_BitExact) {
    const std::wstring test_shm_name = L"Local\\GestCam_Test_BitExact";
    SharedMemoryProducer producer;
    ASSERT_TRUE(producer.Initialize(test_shm_name));

    SharedMemoryConsumer consumer;
    ASSERT_TRUE(consumer.Open(test_shm_name));
    EXPECT_EQ(consumer.GetWidth(), GESTCAM_WIDTH);
    EXPECT_EQ(consumer.GetHeight(), GESTCAM_HEIGHT);

    // Generate distinctive test pattern
    std::vector<uint8_t> test_frame(GESTCAM_SLOT_SIZE);
    for (size_t i = 0; i < GESTCAM_SLOT_SIZE; ++i) {
        test_frame[i] = static_cast<uint8_t>((i * 13 + 37) & 0xFF);
    }

    uint64_t sent_index = 42;
    uint64_t sent_timestamp = 123456789;
    EXPECT_TRUE(producer.WriteFrame(test_frame.data(), sent_index, sent_timestamp));

    std::vector<uint8_t> read_buffer(GESTCAM_SLOT_SIZE);
    uint64_t recv_index = 0;
    uint64_t recv_timestamp = 0;

    EXPECT_TRUE(consumer.ReadLatestFrame(read_buffer.data(), recv_index, recv_timestamp));
    EXPECT_EQ(recv_index, sent_index);
    EXPECT_EQ(recv_timestamp, sent_timestamp);

    // Assert 100% bit-exact match across all 2,764,800 bytes
    EXPECT_EQ(std::memcmp(test_frame.data(), read_buffer.data(), GESTCAM_SLOT_SIZE), 0);

    consumer.Close();
    producer.Close();
}

// Test 4: Triple-Buffering Zero-Tearing Concurrency Stress Test
TEST(SharedMemoryTest, TripleBuffering_ZeroTearing_Stress) {
    const std::wstring test_shm_name = L"Local\\GestCam_Test_Stress";
    SharedMemoryProducer producer;
    ASSERT_TRUE(producer.Initialize(test_shm_name));

    SharedMemoryConsumer consumer;
    ASSERT_TRUE(consumer.Open(test_shm_name));

    constexpr int TOTAL_FRAMES = 2000;
    std::atomic<bool> producer_done{false};
    std::atomic<int> tearing_count{0};
    std::atomic<int> frames_read{0};

    // Producer Thread
    std::thread producer_thread([&]() {
        std::vector<uint8_t> frame(GESTCAM_SLOT_SIZE);
        for (uint64_t f = 1; f <= TOTAL_FRAMES; ++f) {
            uint8_t marker = static_cast<uint8_t>(f & 0xFF);
            // Fill whole frame with same marker byte to detect tearing easily
            std::memset(frame.data(), marker, GESTCAM_SLOT_SIZE);
            producer.WriteFrame(frame.data(), f, f * 33333);
            // Small yield to simulate 30/60 FPS pacing
            if (f % 50 == 0) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    // Consumer Thread
    std::thread consumer_thread([&]() {
        std::vector<uint8_t> read_buffer(GESTCAM_SLOT_SIZE);
        uint64_t last_frame = 0;
        uint64_t frame_idx = 0;
        uint64_t ts = 0;

        int idle_retries = 0;
        while (!producer_done.load(std::memory_order_acquire) || (consumer.HasNewFrame(last_frame) && idle_retries < 100)) {
            if (consumer.ReadLatestFrame(read_buffer.data(), frame_idx, ts)) {
                if (frame_idx > last_frame) {
                    last_frame = frame_idx;
                    idle_retries = 0;
                    frames_read.fetch_add(1, std::memory_order_relaxed);

                    // Check for tearing: Every byte in the frame must equal read_buffer[0]
                    uint8_t expected = read_buffer[0];
                    for (size_t i = 0; i < GESTCAM_SLOT_SIZE; i += 4096) {
                        if (read_buffer[i] != expected) {
                            tearing_count.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                    }
                } else {
                    idle_retries++;
                    std::this_thread::yield();
                }
            } else {
                idle_retries++;
                std::this_thread::yield();
            }
        }
    });

    producer_thread.join();
    consumer_thread.join();

    EXPECT_GT(frames_read.load(), 100);
    EXPECT_EQ(tearing_count.load(), 0); // Must have ZERO tearing!

    consumer.Close();
    producer.Close();
}

// Test 5: Latency Benchmark for WriteFrame
TEST(SharedMemoryTest, Latency_WriteBenchmark) {
    const std::wstring test_shm_name = L"Local\\GestCam_Test_Bench";
    SharedMemoryProducer producer;
    ASSERT_TRUE(producer.Initialize(test_shm_name));

    std::vector<uint8_t> dummy_frame(GESTCAM_SLOT_SIZE, 128);
    constexpr int BENCH_ROUNDS = 100;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < BENCH_ROUNDS; ++i) {
        producer.WriteFrame(dummy_frame.data(), i, i * 33333);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    double avg_ms = total_ms / BENCH_ROUNDS;

    std::cout << "[BENCHMARK] Average Shared Memory Write Latency: " 
              << avg_ms << " ms (" << (avg_ms * 1000.0) << " us)" << std::endl;

    // Must be <= 1.0 ms
    EXPECT_LE(avg_ms, 1.0);

    producer.Close();
}
