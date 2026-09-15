#pragma once

#include <cstdint>

namespace gestcam::driver {

// Renders a standby / fallback frame (1280x720 RGB24) with pulsing indicator
void RenderFallbackFrame(uint8_t* out_rgb24, int width, int height, uint64_t frame_tick);

} // namespace gestcam::driver
