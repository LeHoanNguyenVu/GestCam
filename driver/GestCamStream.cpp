#include "driver/GestCamStream.h"
#include "driver/GestCamFilter.h"
#include "driver/GestCamGuids.h"
#include "driver/FallbackFrame.h"
#include "gestcam/SharedMemoryProtocol.h"
#include <iostream>
#include <chrono>
#include <cstring>

namespace gestcam::driver {

void DeleteMediaType(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return;
    if (pmt->cbFormat != 0 && pmt->pbFormat != nullptr) {
        CoTaskMemFree(pmt->pbFormat);
        pmt->pbFormat = nullptr;
        pmt->cbFormat = 0;
    }
    if (pmt->pUnk != nullptr) {
        pmt->pUnk->Release();
        pmt->pUnk = nullptr;
    }
}

HRESULT CreateDefaultMediaType(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    ZeroMemory(pmt, sizeof(AM_MEDIA_TYPE));

    pmt->majortype = MEDIATYPE_Video;
    pmt->subtype = MEDIASUBTYPE_RGB24;
    pmt->bFixedSizeSamples = TRUE;
    pmt->bTemporalCompression = FALSE;
    pmt->lSampleSize = GESTCAM_SLOT_SIZE;
    pmt->formattype = FORMAT_VideoInfo;

    pmt->cbFormat = sizeof(VIDEOINFOHEADER);
    pmt->pbFormat = reinterpret_cast<BYTE*>(CoTaskMemAlloc(sizeof(VIDEOINFOHEADER)));
    if (!pmt->pbFormat) return E_OUTOFMEMORY;
    ZeroMemory(pmt->pbFormat, sizeof(VIDEOINFOHEADER));

    auto* vih = reinterpret_cast<VIDEOINFOHEADER*>(pmt->pbFormat);
    vih->rcSource = {0, 0, static_cast<LONG>(GESTCAM_WIDTH), static_cast<LONG>(GESTCAM_HEIGHT)};
    vih->rcTarget = vih->rcSource;
    vih->dwBitRate = GESTCAM_WIDTH * GESTCAM_HEIGHT * 3 * 8 * 30; // 30 FPS
    vih->dwBitErrorRate = 0;
    vih->AvgTimePerFrame = 333333; // 33.3333 ms in 100ns units (30 FPS)

    BITMAPINFOHEADER& bih = vih->bmiHeader;
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = GESTCAM_WIDTH;
    bih.biHeight = GESTCAM_HEIGHT; // Bottom-up standard DIB
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = GESTCAM_SLOT_SIZE;

    return S_OK;
}

// Minimal IEnumMediaTypes implementation
class GestCamEnumMediaTypes : public IEnumMediaTypes {
public:
    GestCamEnumMediaTypes() = default;
    virtual ~GestCamEnumMediaTypes() = default;

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumMediaTypes) {
            *ppv = static_cast<IEnumMediaTypes*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return ++ref_count_;
    }

    STDMETHODIMP_(ULONG) Release() override {
        ULONG u = --ref_count_;
        if (u == 0) delete this;
        return u;
    }

    STDMETHODIMP Next(ULONG cMediaTypes, AM_MEDIA_TYPE** ppMediaTypes, ULONG* pcFetched) override {
        if (!ppMediaTypes) return E_POINTER;
        if (cMediaTypes > 1 && !pcFetched) return E_INVALIDARG;

        ULONG fetched = 0;
        if (pos_ == 0 && cMediaTypes > 0) {
            ppMediaTypes[0] = reinterpret_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
            if (ppMediaTypes[0]) {
                CreateDefaultMediaType(ppMediaTypes[0]);
                fetched = 1;
                pos_ = 1;
            }
        }
        if (pcFetched) *pcFetched = fetched;
        return (fetched == cMediaTypes) ? S_OK : S_FALSE;
    }

    STDMETHODIMP Skip(ULONG cMediaTypes) override {
        pos_ += cMediaTypes;
        return (pos_ <= 1) ? S_OK : S_FALSE;
    }

    STDMETHODIMP Reset() override {
        pos_ = 0;
        return S_OK;
    }

    STDMETHODIMP Clone(IEnumMediaTypes** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        auto* clone = new GestCamEnumMediaTypes();
        clone->pos_ = pos_;
        *ppEnum = clone;
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_count_{1};
    ULONG pos_ = 0;
};

// ============================================================================
// GestCamStream Implementation
// ============================================================================

GestCamStream::GestCamStream(GestCamFilter* pFilter, HRESULT* phr)
    : filter_(pFilter) {
    CreateDefaultMediaType(&current_media_type_);
    if (phr) *phr = S_OK;
}

GestCamStream::~GestCamStream() {
    Inactive();
    Disconnect();
    DeleteMediaType(&current_media_type_);
}

STDMETHODIMP GestCamStream::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPin) {
        *ppv = static_cast<IPin*>(this);
    } else if (riid == IID_IKsPropertySet) {
        *ppv = static_cast<IKsPropertySet*>(this);
    } else if (riid == IID_IAMStreamConfig) {
        *ppv = static_cast<IAMStreamConfig*>(this);
    } else if (riid == IID_IQualityControl) {
        *ppv = static_cast<IQualityControl*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) GestCamStream::AddRef() {
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) GestCamStream::Release() {
    ULONG u = --ref_count_;
    if (u == 0) delete this;
    return u;
}

// IPin Implementation
STDMETHODIMP GestCamStream::Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) {
    if (!pReceivePin) return E_POINTER;
    if (connected_pin_) return VFW_E_ALREADY_CONNECTED;

    AM_MEDIA_TYPE mt;
    if (pmt) {
        mt = *pmt;
    } else {
        CreateDefaultMediaType(&mt);
    }

    HRESULT hr = pReceivePin->ReceiveConnection(this, &mt);
    if (SUCCEEDED(hr)) {
        connected_pin_ = pReceivePin;
        connected_pin_->AddRef();
        DeleteMediaType(&current_media_type_);
        current_media_type_ = mt;
    }
    return hr;
}

STDMETHODIMP GestCamStream::ReceiveConnection(IPin* /*pConnector*/, const AM_MEDIA_TYPE* /*pmt*/) {
    return E_NOTIMPL; // Output pin does not receive connections
}

STDMETHODIMP GestCamStream::Disconnect() {
    if (is_streaming_) Inactive();
    if (connected_pin_) {
        connected_pin_->Release();
        connected_pin_ = nullptr;
    }
    if (allocator_) {
        allocator_->Decommit();
        allocator_->Release();
        allocator_ = nullptr;
    }
    return S_OK;
}

STDMETHODIMP GestCamStream::ConnectedTo(IPin** pPin) {
    if (!pPin) return E_POINTER;
    if (!connected_pin_) return VFW_E_NOT_CONNECTED;
    *pPin = connected_pin_;
    (*pPin)->AddRef();
    return S_OK;
}

STDMETHODIMP GestCamStream::ConnectionMediaType(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (!connected_pin_) return VFW_E_NOT_CONNECTED;
    CreateDefaultMediaType(pmt);
    return S_OK;
}

STDMETHODIMP GestCamStream::QueryPinInfo(PIN_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    pInfo->pFilter = reinterpret_cast<IBaseFilter*>(filter_);
    if (pInfo->pFilter) pInfo->pFilter->AddRef();
    pInfo->dir = PINDIR_OUTPUT;
    wcscpy_s(pInfo->achName, L"Capture");
    return S_OK;
}

STDMETHODIMP GestCamStream::QueryDirection(PIN_DIRECTION* pPinDir) {
    if (!pPinDir) return E_POINTER;
    *pPinDir = PINDIR_OUTPUT;
    return S_OK;
}

STDMETHODIMP GestCamStream::QueryId(LPWSTR* Id) {
    if (!Id) return E_POINTER;
    *Id = reinterpret_cast<LPWSTR>(CoTaskMemAlloc(sizeof(L"Capture")));
    if (!*Id) return E_OUTOFMEMORY;
    wcscpy_s(*Id, 8, L"Capture");
    return S_OK;
}

STDMETHODIMP GestCamStream::QueryAccept(const AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (pmt->majortype == MEDIATYPE_Video && pmt->subtype == MEDIASUBTYPE_RGB24) {
        return S_OK;
    }
    return S_FALSE;
}

STDMETHODIMP GestCamStream::EnumMediaTypes(IEnumMediaTypes** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new GestCamEnumMediaTypes();
    return S_OK;
}

STDMETHODIMP GestCamStream::QueryInternalConnections(IPin** /*apPin*/, ULONG* nPin) {
    if (nPin) *nPin = 0;
    return E_NOTIMPL;
}

STDMETHODIMP GestCamStream::EndOfStream() { return S_OK; }
STDMETHODIMP GestCamStream::BeginFlush() { return S_OK; }
STDMETHODIMP GestCamStream::EndFlush() { return S_OK; }
STDMETHODIMP GestCamStream::NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) { return S_OK; }

// ============================================================================
// IKsPropertySet Implementation (CRITICAL FOR CHROME & ZOOM DETECTION)
// ============================================================================

STDMETHODIMP GestCamStream::Set(REFGUID, ULONG, LPVOID, ULONG, LPVOID, ULONG) {
    return E_NOTIMPL;
}

STDMETHODIMP GestCamStream::Get(REFGUID rguidPropSet, ULONG ulId, LPVOID, ULONG, LPVOID pPropertyData, ULONG ulDataLength, ULONG* pBytesReturned) {
    if (rguidPropSet == AMPROPSETID_Pin || rguidPropSet == AMPROPSETID_Pin_GestCam) {
        if (ulId == 0 /* AMPROPERTY_PIN_CATEGORY */) {
            if (!pPropertyData) {
                if (pBytesReturned) *pBytesReturned = sizeof(GUID);
                return S_OK;
            }
            if (ulDataLength < sizeof(GUID)) return E_UNEXPECTED;
            *reinterpret_cast<GUID*>(pPropertyData) = PIN_CATEGORY_CAPTURE_GestCam;
            if (pBytesReturned) *pBytesReturned = sizeof(GUID);
            return S_OK;
        }
    }
    return E_PROP_SET_UNSUPPORTED;
}

STDMETHODIMP GestCamStream::QuerySupported(REFGUID rguidPropSet, ULONG ulId, ULONG* pTypeSupport) {
    if (rguidPropSet == AMPROPSETID_Pin || rguidPropSet == AMPROPSETID_Pin_GestCam) {
        if (ulId == 0 /* AMPROPERTY_PIN_CATEGORY */) {
            if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
            return S_OK;
        }
    }
    return E_PROP_SET_UNSUPPORTED;
}

// ============================================================================
// IAMStreamConfig Implementation
// ============================================================================

STDMETHODIMP GestCamStream::SetFormat(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (QueryAccept(pmt) != S_OK) return VFW_E_INVALIDMEDIATYPE;
    DeleteMediaType(&current_media_type_);
    current_media_type_ = *pmt;
    return S_OK;
}

STDMETHODIMP GestCamStream::GetFormat(AM_MEDIA_TYPE** ppmt) {
    if (!ppmt) return E_POINTER;
    *ppmt = reinterpret_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (!*ppmt) return E_OUTOFMEMORY;
    CreateDefaultMediaType(*ppmt);
    return S_OK;
}

STDMETHODIMP GestCamStream::GetNumberOfCapabilities(int* piCount, int* piSize) {
    if (!piCount || !piSize) return E_POINTER;
    *piCount = 1;
    *piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

STDMETHODIMP GestCamStream::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) {
    if (!ppmt || !pSCC) return E_POINTER;
    if (iIndex != 0) return S_FALSE;

    *ppmt = reinterpret_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (!*ppmt) return E_OUTOFMEMORY;
    CreateDefaultMediaType(*ppmt);

    auto* caps = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(pSCC);
    ZeroMemory(caps, sizeof(VIDEO_STREAM_CONFIG_CAPS));
    caps->guid = FORMAT_VideoInfo;
    caps->VideoStandard = 0;
    caps->InputSize = {static_cast<LONG>(GESTCAM_WIDTH), static_cast<LONG>(GESTCAM_HEIGHT)};
    caps->MinCroppingSize = caps->InputSize;
    caps->MaxCroppingSize = caps->InputSize;
    caps->MinOutputSize = caps->InputSize;
    caps->MaxOutputSize = caps->InputSize;
    caps->MinFrameInterval = 166666; // 60 FPS
    caps->MaxFrameInterval = 333333; // 30 FPS
    caps->MinBitsPerSecond = GESTCAM_WIDTH * GESTCAM_HEIGHT * 24 * 30;
    caps->MaxBitsPerSecond = GESTCAM_WIDTH * GESTCAM_HEIGHT * 24 * 60;

    return S_OK;
}

// IQualityControl Implementation
STDMETHODIMP GestCamStream::Notify(IBaseFilter*, Quality) { return S_OK; }
STDMETHODIMP GestCamStream::SetSink(IQualityControl*) { return S_OK; }

// ============================================================================
// Streaming Engine & Worker Thread
// ============================================================================

void GestCamStream::CopyFlippedRGB24(const uint8_t* src, uint8_t* dst, int width, int height) {
    // Windows standard DIB is bottom-up (row 0 is bottom)
    size_t row_stride = static_cast<size_t>(width) * 3;
    for (int y = 0; y < height; ++y) {
        const uint8_t* src_row = src + static_cast<size_t>(y) * row_stride;
        uint8_t* dst_row = dst + static_cast<size_t>(height - 1 - y) * row_stride;
        std::memcpy(dst_row, src_row, row_stride);
    }
}

HRESULT GestCamStream::Active() {
    if (is_streaming_) return S_OK;
    is_streaming_ = true;
    worker_thread_ = std::thread(&GestCamStream::ThreadProc, this);
    return S_OK;
}

HRESULT GestCamStream::Inactive() {
    if (!is_streaming_) return S_OK;
    is_streaming_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    return S_OK;
}

bool GestCamStream::ReadFrameForTest(uint8_t* out_buffer, bool& out_is_fallback) {
    if (!shm_consumer_.IsConnected()) {
        shm_consumer_.Open(GESTCAM_DEFAULT_SHM_NAME);
    }

    uint64_t frame_index = 0;
    uint64_t timestamp_us = 0;
    bool got_shm = false;

    if (shm_consumer_.IsConnected()) {
        got_shm = shm_consumer_.ReadLatestFrame(out_buffer, frame_index, timestamp_us);
    }

    if (got_shm) {
        out_is_fallback = false;
        return true;
    } else {
        out_is_fallback = true;
        RenderFallbackFrame(out_buffer, GESTCAM_WIDTH, GESTCAM_HEIGHT, ++frame_tick_);
        return true;
    }
}

void GestCamStream::ThreadProc() {
    // Open shared memory channel
    shm_consumer_.Open(GESTCAM_DEFAULT_SHM_NAME);

    std::vector<uint8_t> raw_rgb(GESTCAM_SLOT_SIZE);
    REFERENCE_TIME stream_time = 0;
    constexpr REFERENCE_TIME FRAME_DURATION = 333333; // 30 FPS

    IMemInputPin* mem_input = nullptr;
    if (connected_pin_) {
        connected_pin_->QueryInterface(IID_IMemInputPin, reinterpret_cast<void**>(&mem_input));
    }

    while (is_streaming_.load(std::memory_order_relaxed)) {
        auto loop_start = std::chrono::steady_clock::now();

        uint64_t frame_index = 0;
        uint64_t timestamp_us = 0;
        bool has_frame = false;

        if (shm_consumer_.IsConnected()) {
            has_frame = shm_consumer_.ReadLatestFrame(raw_rgb.data(), frame_index, timestamp_us);
        }

        if (mem_input && allocator_) {
            IMediaSample* pSample = nullptr;
            HRESULT hr = allocator_->GetBuffer(&pSample, nullptr, nullptr, 0);
            if (SUCCEEDED(hr) && pSample) {
                BYTE* pData = nullptr;
                pSample->GetPointer(&pData);

                if (pData) {
                    if (has_frame) {
                        CopyFlippedRGB24(raw_rgb.data(), pData, GESTCAM_WIDTH, GESTCAM_HEIGHT);
                        last_frame_time_ms_ = GetTickCount64();
                    } else {
                        // If no frame from Core in > 1000ms, render Standby screen
                        RenderFallbackFrame(pData, GESTCAM_WIDTH, GESTCAM_HEIGHT, ++frame_tick_);
                    }

                    REFERENCE_TIME rtStart = stream_time;
                    REFERENCE_TIME rtEnd = stream_time + FRAME_DURATION;
                    pSample->SetTime(&rtStart, &rtEnd);
                    pSample->SetSyncPoint(TRUE);
                    pSample->SetActualDataLength(GESTCAM_SLOT_SIZE);

                    mem_input->Receive(pSample);
                }
                pSample->Release();
            }
        }

        stream_time += FRAME_DURATION;

        // Pacing at 30 FPS (~33 ms per frame)
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - loop_start
        ).count();
        if (elapsed < 33) {
            std::this_thread::sleep_for(std::chrono::milliseconds(33 - elapsed));
        }
    }

    if (mem_input) {
        mem_input->Release();
    }
}

} // namespace gestcam::driver
