#include "gestcam/MFCameraCapture.h"

#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <chrono>
#include <thread>
#include <iostream>

namespace gestcam {

namespace {

VideoPixelFormat GuidToFormat(const GUID& subtype) {
    if (subtype == MFVideoFormat_NV12) return VideoPixelFormat::NV12;
    if (subtype == MFVideoFormat_YUY2) return VideoPixelFormat::YUY2;
    if (subtype == MFVideoFormat_MJPG) return VideoPixelFormat::MJPEG;
    if (subtype == MFVideoFormat_RGB24) return VideoPixelFormat::RGB24;
    return VideoPixelFormat::UNKNOWN;
}

} // namespace

MFCameraCapture::MFCameraCapture() = default;

MFCameraCapture::~MFCameraCapture() {
    Close();
}

bool MFCameraCapture::Open(int device_index, int width, int height, int target_fps) {
    Close();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    co_initialized_ = SUCCEEDED(hr);

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        Close();
        return false;
    }

    IMFAttributes* pAttributes = nullptr;
    hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr)) {
        Close();
        return false;
    }

    hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) {
        pAttributes->Release();
        Close();
        return false;
    }

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);
    pAttributes->Release();

    if (FAILED(hr) || count == 0 || device_index < 0 || static_cast<UINT32>(device_index) >= count) {
        if (ppDevices) {
            for (UINT32 i = 0; i < count; ++i) ppDevices[i]->Release();
            CoTaskMemFree(ppDevices);
        }
        Close();
        return false;
    }

    // Kích hoạt Media Source từ camera được chọn
    hr = ppDevices[device_index]->ActivateObject(IID_PPV_ARGS(&source_));
    for (UINT32 i = 0; i < count; ++i) ppDevices[i]->Release();
    CoTaskMemFree(ppDevices);

    if (FAILED(hr) || !source_) {
        Close();
        return false;
    }

    // Tạo Source Reader với tính năng xử lý video & giải mã phần cứng tự động
    IMFAttributes* pReaderAttributes = nullptr;
    hr = MFCreateAttributes(&pReaderAttributes, 3);
    if (SUCCEEDED(hr)) {
        pReaderAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        pReaderAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        pReaderAttributes->SetUINT32(MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, TRUE);

        hr = MFCreateSourceReaderFromMediaSource(source_, pReaderAttributes, &reader_);
        pReaderAttributes->Release();
    }

    if (FAILED(hr) || !reader_) {
        Close();
        return false;
    }

    // Định dạng mong muốn: Ưu tiên NV12 (hỗ trợ giải mã MJPEG sang NV12 cực nhanh), sau đó đến YUY2
    IMFMediaType* pDesiredType = nullptr;
    hr = MFCreateMediaType(&pDesiredType);

    if (SUCCEEDED(hr)) {
        pDesiredType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pDesiredType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        MFSetAttributeSize(pDesiredType, MF_MT_FRAME_SIZE, width, height);
        if (target_fps > 0) {
            MFSetAttributeRatio(pDesiredType, MF_MT_FRAME_RATE, target_fps, 1);
        }

        hr = reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, pDesiredType);
        if (FAILED(hr)) {
            // Thử YUY2 nếu camera không chấp nhận NV12 trực tiếp
            pDesiredType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);
            reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, pDesiredType);
        }
        pDesiredType->Release();
    }

    // Lấy thông số Media Type thực tế đang hoạt động
    IMFMediaType* pActualType = nullptr;
    hr = reader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &pActualType);
    if (SUCCEEDED(hr) && pActualType) {
        UINT32 actualWidth = 0, actualHeight = 0;
        UINT32 fpsNum = 0, fpsDen = 1;
        GUID actualSubtype = GUID_NULL;

        MFGetAttributeSize(pActualType, MF_MT_FRAME_SIZE, &actualWidth, &actualHeight);
        MFGetAttributeRatio(pActualType, MF_MT_FRAME_RATE, &fpsNum, &fpsDen);
        pActualType->GetGUID(MF_MT_SUBTYPE, &actualSubtype);

        current_mode_.width = static_cast<int>(actualWidth);
        current_mode_.height = static_cast<int>(actualHeight);
        current_mode_.fps = fpsDen > 0 ? static_cast<int>(fpsNum / fpsDen) : 0;
        current_mode_.format = GuidToFormat(actualSubtype);

        pActualType->Release();
    } else {
        Close();
        return false;
    }

    device_index_ = device_index;
    is_opened_ = true;
    return true;
}

void MFCameraCapture::Close() {
    if (reader_) {
        reader_->Release();
        reader_ = nullptr;
    }
    if (source_) {
        source_->Shutdown();
        source_->Release();
        source_ = nullptr;
    }
    if (is_opened_) {
        MFShutdown();
        if (co_initialized_) {
            CoUninitialize();
            co_initialized_ = false;
        }
        is_opened_ = false;
    }
    device_index_ = -1;
    current_mode_ = CameraFormatMode{};
}

bool MFCameraCapture::GrabFrame(RawVideoFrame& out_frame, int timeout_ms) {
    if (!is_opened_ || !reader_) return false;

    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG llTimestamp = 0;
        IMFSample* pSample = nullptr;

        HRESULT hr = reader_->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0,
            &streamIndex,
            &flags,
            &llTimestamp,
            &pSample
        );

        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            return false;
        }

        if (pSample) {
            IMFMediaBuffer* pBuffer = nullptr;
            hr = pSample->ConvertToContiguousBuffer(&pBuffer);
            if (SUCCEEDED(hr) && pBuffer) {
                BYTE* pData = nullptr;
                DWORD currentLength = 0;
                hr = pBuffer->Lock(&pData, nullptr, &currentLength);
                if (SUCCEEDED(hr) && pData && currentLength > 0) {
                    out_frame.width = current_mode_.width;
                    out_frame.height = current_mode_.height;
                    out_frame.format = current_mode_.format;
                    out_frame.stride = current_mode_.width; // Chuẩn stride NV12/YUY2 cơ sở
                    out_frame.timestamp_us = llTimestamp / 10; // Đổi 100ns -> microseconds
                    out_frame.data.assign(pData, pData + currentLength);

                    pBuffer->Unlock();
                    pBuffer->Release();
                    pSample->Release();
                    return true;
                }
                if (pData) pBuffer->Unlock();
                pBuffer->Release();
            }
            pSample->Release();
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time
        ).count();
        if (elapsed >= timeout_ms) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

} // namespace gestcam

#endif
