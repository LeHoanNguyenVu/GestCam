#pragma once

#include "gestcam/SharedMemoryProtocol.h"
#include <windows.h>
#include <string>
#include <cstdint>

namespace gestcam {

class SharedMemoryProducer {
public:
    SharedMemoryProducer();
    ~SharedMemoryProducer();

    // Disable copying
    SharedMemoryProducer(const SharedMemoryProducer&) = delete;
    SharedMemoryProducer& operator=(const SharedMemoryProducer&) = delete;

    // Enable move
    SharedMemoryProducer(SharedMemoryProducer&& other) noexcept;
    SharedMemoryProducer& operator=(SharedMemoryProducer&& other) noexcept;

    // Initialize Shared Memory file mapping with permissive Low-Integrity SDDL
    bool Initialize(const std::wstring& shm_name = GESTCAM_DEFAULT_SHM_NAME);

    // Write a single RGB24 frame using triple-buffering logic
    bool WriteFrame(const uint8_t* rgb24_data, uint64_t frame_index = 0, uint64_t timestamp_us = 0);

    // Release mapping and close handle
    void Close();

    bool IsInitialized() const { return state_ != nullptr; }
    uint32_t GetReadySlotIndex() const;

private:
    HANDLE map_handle_ = nullptr;
    SharedMemoryState* state_ = nullptr;
    std::wstring name_;
};

} // namespace gestcam
