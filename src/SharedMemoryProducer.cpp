#include "gestcam/SharedMemoryProducer.h"
#include <sddl.h>
#include <cstring>
#include <iostream>
#include <atomic>

namespace gestcam {

SharedMemoryProducer::SharedMemoryProducer() = default;

SharedMemoryProducer::~SharedMemoryProducer() {
    Close();
}

SharedMemoryProducer::SharedMemoryProducer(SharedMemoryProducer&& other) noexcept
    : map_handle_(other.map_handle_),
      state_(other.state_),
      name_(std::move(other.name_)) {
    other.map_handle_ = nullptr;
    other.state_ = nullptr;
}

SharedMemoryProducer& SharedMemoryProducer::operator=(SharedMemoryProducer&& other) noexcept {
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

bool SharedMemoryProducer::Initialize(const std::wstring& shm_name) {
    Close();
    name_ = shm_name;

    // Convert SDDL string to Security Descriptor with Chrome Sandbox permissions
    PSECURITY_DESCRIPTOR pSD = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            GESTCAM_SDDL_PERMISSIVE,
            SDDL_REVISION_1,
            &pSD,
            nullptr)) {
        std::cerr << "[SharedMemoryProducer] Failed to convert SDDL, error: " 
                  << GetLastError() << std::endl;
        return false;
    }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = FALSE;
    sa.lpSecurityDescriptor = pSD;

    map_handle_ = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        &sa,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(SharedMemoryState)),
        name_.c_str()
    );

    DWORD err = GetLastError();
    if (pSD) {
        LocalFree(pSD);
        pSD = nullptr;
    }

    if (!map_handle_) {
        std::cerr << "[SharedMemoryProducer] CreateFileMappingW failed, error: " 
                  << err << std::endl;
        return false;
    }

    state_ = reinterpret_cast<SharedMemoryState*>(
        MapViewOfFile(map_handle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryState))
    );

    if (!state_) {
        std::cerr << "[SharedMemoryProducer] MapViewOfFile failed, error: " 
                  << GetLastError() << std::endl;
        CloseHandle(map_handle_);
        map_handle_ = nullptr;
        return false;
    }

    // Initialize SharedMemoryState header
    state_->magic = GESTCAM_MAGIC;
    state_->version = GESTCAM_VERSION;
    state_->width = GESTCAM_WIDTH;
    state_->height = GESTCAM_HEIGHT;
    state_->stride = GESTCAM_WIDTH * GESTCAM_CHANNELS;
    state_->format = 0; // RGB24
    state_->active_readers = 0;
    state_->ready_slot_idx = 0;
    state_->active_reading_slot = GESTCAM_SLOT_IDLE;

    for (uint32_t i = 0; i < GESTCAM_NUM_SLOTS; ++i) {
        state_->slots[i].frame_index = 0;
        state_->slots[i].timestamp_us = 0;
        state_->slots[i].sequence_before = 0;
        state_->slots[i].sequence_after = 0;
        std::memset(state_->slots[i].pixels, 0, GESTCAM_SLOT_SIZE);
    }

    return true;
}

bool SharedMemoryProducer::WriteFrame(const uint8_t* rgb24_data, uint64_t frame_index, uint64_t timestamp_us) {
    if (!state_ || !rgb24_data) return false;

    uint32_t current_ready = state_->ready_slot_idx;
    uint32_t current_reading = state_->active_reading_slot;

    // Pick a slot that is neither the current ready slot nor actively being read
    uint32_t target_slot = (current_ready + 1) % GESTCAM_NUM_SLOTS;
    if (target_slot == current_reading) {
        target_slot = (current_ready + 2) % GESTCAM_NUM_SLOTS;
    }

    SharedBufferSlot& slot = state_->slots[target_slot];
    slot.sequence_before++; // Becomes odd: writing in progress
    std::atomic_thread_fence(std::memory_order_release);

    slot.frame_index = frame_index;
    slot.timestamp_us = timestamp_us;
    std::memcpy(slot.pixels, rgb24_data, GESTCAM_SLOT_SIZE);

    slot.sequence_before++; // Becomes even: writing complete
    slot.sequence_after = slot.sequence_before;
    std::atomic_thread_fence(std::memory_order_release);

    // Atomically make the newly written slot ready for consumers
    state_->ready_slot_idx = target_slot;
    return true;
}

uint32_t SharedMemoryProducer::GetReadySlotIndex() const {
    return state_ ? state_->ready_slot_idx : 0;
}

void SharedMemoryProducer::Close() {
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
