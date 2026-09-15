#include "gestcam/CameraEnumerator.h"

#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

namespace gestcam {

namespace {

std::string WideToUtf8(const wchar_t* wstr) {
    if (!wstr) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (size_needed <= 1) return "";
    std::string result(size_needed - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), size_needed, nullptr, nullptr);
    return result;
}

VideoPixelFormat GuidToPixelFormat(const GUID& subtype) {
    if (subtype == MFVideoFormat_NV12) return VideoPixelFormat::NV12;
    if (subtype == MFVideoFormat_YUY2) return VideoPixelFormat::YUY2;
    if (subtype == MFVideoFormat_MJPG) return VideoPixelFormat::MJPEG;
    if (subtype == MFVideoFormat_RGB24) return VideoPixelFormat::RGB24;
    return VideoPixelFormat::UNKNOWN;
}

} // namespace

std::vector<CameraDeviceInfo> CameraEnumerator::EnumerateDevices() {
    std::vector<CameraDeviceInfo> devices;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool co_initialized = SUCCEEDED(hr);

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        if (co_initialized) CoUninitialize();
        return devices;
    }

    IMFAttributes* pAttributes = nullptr;
    hr = MFCreateAttributes(&pAttributes, 1);
    if (SUCCEEDED(hr)) {
        hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (SUCCEEDED(hr)) {
            IMFActivate** ppDevices = nullptr;
            UINT32 count = 0;
            hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);
            if (SUCCEEDED(hr) && ppDevices) {
                for (UINT32 i = 0; i < count; ++i) {
                    CameraDeviceInfo info;
                    info.index = static_cast<int>(i);

                    wchar_t* pwszName = nullptr;
                    UINT32 cchName = 0;
                    if (SUCCEEDED(ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &pwszName, &cchName))) {
                        info.name = WideToUtf8(pwszName);
                        CoTaskMemFree(pwszName);
                    }

                    wchar_t* pwszLink = nullptr;
                    UINT32 cchLink = 0;
                    if (SUCCEEDED(ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &pwszLink, &cchLink))) {
                        info.symbolic_link = WideToUtf8(pwszLink);
                        CoTaskMemFree(pwszLink);
                    }

                    devices.push_back(std::move(info));
                    ppDevices[i]->Release();
                }
                CoTaskMemFree(ppDevices);
            }
        }
        pAttributes->Release();
    }

    MFShutdown();
    if (co_initialized) CoUninitialize();
    return devices;
}

std::vector<CameraFormatMode> CameraEnumerator::QuerySupportedFormats(int device_index) {
    std::vector<CameraFormatMode> formats;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool co_initialized = SUCCEEDED(hr);

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        if (co_initialized) CoUninitialize();
        return formats;
    }

    IMFAttributes* pAttributes = nullptr;
    hr = MFCreateAttributes(&pAttributes, 1);
    if (SUCCEEDED(hr)) {
        hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (SUCCEEDED(hr)) {
            IMFActivate** ppDevices = nullptr;
            UINT32 count = 0;
            hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);
            if (SUCCEEDED(hr) && ppDevices) {
                if (device_index >= 0 && static_cast<UINT32>(device_index) < count) {
                    IMFMediaSource* pSource = nullptr;
                    hr = ppDevices[device_index]->ActivateObject(IID_PPV_ARGS(&pSource));
                    if (SUCCEEDED(hr) && pSource) {
                        IMFSourceReader* pReader = nullptr;
                        hr = MFCreateSourceReaderFromMediaSource(pSource, nullptr, &pReader);
                        if (SUCCEEDED(hr) && pReader) {
                            DWORD dwMediaTypeIndex = 0;
                            while (true) {
                                IMFMediaType* pNativeType = nullptr;
                                hr = pReader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), dwMediaTypeIndex, &pNativeType);
                                if (FAILED(hr)) break;

                                GUID subtype = GUID_NULL;
                                UINT32 width = 0, height = 0;
                                UINT32 fpsNum = 0, fpsDen = 1;

                                if (SUCCEEDED(pNativeType->GetGUID(MF_MT_SUBTYPE, &subtype)) &&
                                    SUCCEEDED(MFGetAttributeSize(pNativeType, MF_MT_FRAME_SIZE, &width, &height))) {
                                    MFGetAttributeRatio(pNativeType, MF_MT_FRAME_RATE, &fpsNum, &fpsDen);

                                    CameraFormatMode mode;
                                    mode.width = static_cast<int>(width);
                                    mode.height = static_cast<int>(height);
                                    mode.fps = fpsDen > 0 ? static_cast<int>(fpsNum / fpsDen) : 0;
                                    mode.format = GuidToPixelFormat(subtype);

                                    formats.push_back(mode);
                                }
                                pNativeType->Release();
                                ++dwMediaTypeIndex;
                            }
                            pReader->Release();
                        }
                        pSource->Release();
                    }
                }
                for (UINT32 i = 0; i < count; ++i) ppDevices[i]->Release();
                CoTaskMemFree(ppDevices);
            }
        }
        pAttributes->Release();
    }

    MFShutdown();
    if (co_initialized) CoUninitialize();
    return formats;
}

} // namespace gestcam

#endif
