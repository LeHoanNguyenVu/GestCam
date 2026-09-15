#pragma once

#include "gestcam/SharedMemoryProtocol.h"
#include <windows.h>
#include <string>
#include <cstdint>

namespace gestcam {

class SharedMemoryConsumer {
public:
    SharedMemoryConsumer();
    ~SharedMemoryConsumer();

    // Disable copying
    SharedMemoryConsumer(const SharedMemoryConsumer&) = delete;
    SharedMemoryConsumer& operator=(const SharedMemoryConsumer&) = delete;

    // Enable move
    SharedMemoryConsumer(SharedMemoryConsumer&& other) noexcept;
    SharedMemoryConsumer& operator=(SharedMemoryConsumer&& other) noexcept;

    // Open existing shared memory mapping
    bool Open(const std::wstring& shm_name = GESTCAM_DEFAULT_SHM_NAME);

    // Read the latest available frame into caller's buffer
    bool ReadLatestFrame(uint8_t* out_rgb24_data, uint64_t& out_frame_index, uint64_t& out_timestamp_us);

    // Check if a newer frame exists compared to last seen frame_index
    bool HasNewFrame(uint64_t last_frame_index) const;

    // Release mapping and close handle
    void Close();

    bool IsConnected() const { return state_ != nullptr; }
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;

private:
    HANDLE map_handle_ = nullptr;
    SharedMemoryState* state_ = nullptr;
    std::wstring name_;
};

} // namespace gestcam
