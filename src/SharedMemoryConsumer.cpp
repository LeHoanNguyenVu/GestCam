#include "gestcam/SharedMemoryConsumer.h"
#include <cstring>
#include <iostream>
#include <atomic>
#include <thread>

namespace gestcam {

SharedMemoryConsumer::SharedMemoryConsumer() = default;

SharedMemoryConsumer::~SharedMemoryConsumer() {
    Close();
}

SharedMemoryConsumer::SharedMemoryConsumer(SharedMemoryConsumer&& other) noexcept
    : map_handle_(other.map_handle_),
      state_(other.state_),
      name_(std::move(other.name_)) {
    other.map_handle_ = nullptr;
    other.state_ = nullptr;
}

SharedMemoryConsumer& SharedMemoryConsumer::operator=(SharedMemoryConsumer&& other) noexcept {
    if (this != &other) {
        Close();
        map_handle_ = other.map_handle_;
        state_ = other.state_;
        name_ = std::move(other.name_);
        other.map_handle_ = nullptr;
        other.state_ = nullptr;
    }
    return *this;
}

bool SharedMemoryConsumer::Open(const std::wstring& shm_name) {
    Close();
    name_ = shm_name;

    // Try opening with Read/Write access first
    map_handle_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name_.c_str());
    DWORD access = FILE_MAP_ALL_ACCESS;

    if (!map_handle_) {
        // Fallback to Read-Only access (e.g. strict low-integrity sandbox)
        map_handle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, name_.c_str());
        access = FILE_MAP_READ;
    }

    if (!map_handle_) {
        return false;
    }

    state_ = reinterpret_cast<SharedMemoryState*>(
        MapViewOfFile(map_handle_, access, 0, 0, sizeof(SharedMemoryState))
    );

    if (!state_) {
        CloseHandle(map_handle_);
        map_handle_ = nullptr;
        return false;
    }

    if (state_->magic != GESTCAM_MAGIC) {
        std::cerr << "[SharedMemoryConsumer] Invalid magic: 0x" 
                  << std::hex << state_->magic << std::endl;
        Close();
        return false;
    }

    return true;
}

bool SharedMemoryConsumer::ReadLatestFrame(uint8_t* out_rgb24_data, uint64_t& out_frame_index, uint64_t& out_timestamp_us) {
    if (!state_ || !out_rgb24_data) return false;

    // Retry loop (seqlock reader) to guarantee zero tearing
    constexpr int MAX_RETRIES = 5;
    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        uint32_t slot_idx = state_->ready_slot_idx;
        if (slot_idx >= GESTCAM_NUM_SLOTS) return false;

        const SharedBufferSlot& slot = state_->slots[slot_idx];

        uint32_t seq_before = slot.sequence_before;
        // If writer is currently writing (odd sequence), spin/retry
        if (seq_before & 1) {
            std::this_thread::yield();
            continue;
        }

        std::atomic_thread_fence(std::memory_order_acquire);

        out_frame_index = slot.frame_index;
        out_timestamp_us = slot.timestamp_us;
        std::memcpy(out_rgb24_data, slot.pixels, GESTCAM_SLOT_SIZE);

        std::atomic_thread_fence(std::memory_order_acquire);

        uint32_t seq_after = slot.sequence_after;

        // If sequence didn't change and remained even, read is bit-exact & tearing-free
        if (seq_before == seq_after) {
            return true;
        }
        std::this_thread::yield();
    }

    return false;
}

bool SharedMemoryConsumer::HasNewFrame(uint64_t last_frame_index) const {
    if (!state_) return false;
    uint32_t slot_idx = state_->ready_slot_idx;
    if (slot_idx >= GESTCAM_NUM_SLOTS) return false;
    return state_->slots[slot_idx].frame_index > last_frame_index;
}

uint32_t SharedMemoryConsumer::GetWidth() const {
    return state_ ? state_->width : 0;
}

uint32_t SharedMemoryConsumer::GetHeight() const {
    return state_ ? state_->height : 0;
}

void SharedMemoryConsumer::Close() {
    if (state_) {
        UnmapViewOfFile(state_);
        state_ = nullptr;
    }
    if (map_handle_) {
        CloseHandle(map_handle_);
        map_handle_ = nullptr;
    }
}

} // namespace gestcam
