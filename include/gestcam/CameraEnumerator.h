#pragma once

#include "gestcam/CameraTypes.h"
#include <vector>

namespace gestcam {

class CameraEnumerator {
public:
    static std::vector<CameraDeviceInfo> EnumerateDevices();
    static std::vector<CameraFormatMode> QuerySupportedFormats(int device_index);
};

} // namespace gestcam
