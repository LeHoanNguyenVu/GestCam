#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <utility>

namespace gestcam {

// Hàng đợi vòng tròn không khóa Single Producer Single Consumer (SPSC)
// Được căn chỉnh dòng nhớ 64-byte để triệt tiêu hiện tượng False Sharing giữa các nhân CPU
template <typename T, size_t Capacity = 4>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0 && Capacity > 0, "Capacity must be a power of 2!");

public:
    SPSCQueue() : head_(0), tail_(0) {}

    // Không cho phép copy để đảm bảo an toàn đa luồng
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Đẩy phần tử vào hàng đợi. Nếu đầy sẽ trả về false
    bool TryPush(T item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);

        if (head - tail >= Capacity) {
            return false; // Hàng đợi đã đầy
        }

        buffer_[head & (Capacity - 1)] = std::move(item);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Đẩy phần tử vào hàng đợi. Nếu đầy sẽ tự động DROP frame cũ nhất (Zero-Latency mode)
    void PushOverwrite(T item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        size_t tail = tail_.load(std::memory_order_relaxed);

        if (head - tail >= Capacity) {
            // Hàng đợi đầy: Drop phần tử cũ nhất bằng cách tăng tail
            tail_.store(tail + 1, std::memory_order_release);
        }

        buffer_[head & (Capacity - 1)] = std::move(item);
        head_.store(head + 1, std::memory_order_release);
    }

    // Rút phần tử ra khỏi hàng đợi. Nếu rỗng sẽ trả về false
    bool TryPop(T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);

        if (tail == head) {
            return false; // Hàng đợi rỗng
        }

        item = std::move(buffer_[tail & (Capacity - 1)]);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    size_t Size() const {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_relaxed);
        return (head >= tail) ? (head - tail) : 0;
    }

    bool Empty() const {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    size_t GetCapacity() const {
        return Capacity;
    }

private:
    std::array<T, Capacity> buffer_;

    // Căn chỉnh 64-byte (Cache Line Size) để tách biệt head và tail trên 2 dòng nhớ khác nhau
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
};

} // namespace gestcam
