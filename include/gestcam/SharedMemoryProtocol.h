#pragma once

#include <cstdint>

#pragma pack(push, 1)

constexpr uint32_t GESTCAM_MAGIC    = 0x4D454D45; // "MEME"
constexpr uint32_t GESTCAM_VERSION  = 0x00010001;
constexpr uint32_t GESTCAM_WIDTH    = 1280;
constexpr uint32_t GESTCAM_HEIGHT   = 720;
constexpr uint32_t GESTCAM_CHANNELS = 3;
constexpr uint32_t GESTCAM_SLOT_SIZE = GESTCAM_WIDTH * GESTCAM_HEIGHT * GESTCAM_CHANNELS;
constexpr uint32_t GESTCAM_NUM_SLOTS = 3;
constexpr uint32_t GESTCAM_SLOT_IDLE = 0xFFFFFFFF;

// Default name for Win32 File Mapping
constexpr const wchar_t* GESTCAM_DEFAULT_SHM_NAME = L"Local\\GestCam_SharedBuffer";

// SDDL granting Generic All (GA) to Everyone (WD) and ALL APPLICATION PACKAGES (AC - Chrome Sandbox)
constexpr const wchar_t* GESTCAM_SDDL_PERMISSIVE = L"D:(A;;GA;;;WD)(A;;GA;;;AC)";

struct SharedBufferSlot {
    uint64_t frame_index;
    uint64_t timestamp_us;
    uint32_t sequence_before; // Incremented before write starts (odd = writing)
    uint32_t sequence_after;  // Incremented after write finishes (even = stable)
    uint8_t  pixels[GESTCAM_SLOT_SIZE];
};

struct SharedMemoryState {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;                  // 0: RGB24
    volatile uint32_t active_readers;
    volatile uint32_t ready_slot_idx;       // 0, 1, 2
    volatile uint32_t active_reading_slot;  // 0, 1, 2, or GESTCAM_SLOT_IDLE
    SharedBufferSlot slots[GESTCAM_NUM_SLOTS];
};

#pragma pack(pop)
