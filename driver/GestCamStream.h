#pragma once

#include <windows.h>
#include <dshow.h>
#include <strmif.h>
#include <amvideo.h>
#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include "gestcam/SharedMemoryConsumer.h"

// Forward declaration
namespace gestcam::driver {
class GestCamFilter;

class GestCamStream : public IPin,
                      public IKsPropertySet,
                      public IAMStreamConfig,
                      public IQualityControl {
public:
    GestCamStream(GestCamFilter* pFilter, HRESULT* phr);
    virtual ~GestCamStream();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IPin
    STDMETHODIMP Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP Disconnect() override;
    STDMETHODIMP ConnectedTo(IPin** pPin) override;
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP QueryPinInfo(PIN_INFO* pInfo) override;
    STDMETHODIMP QueryDirection(PIN_DIRECTION* pPinDir) override;
    STDMETHODIMP QueryId(LPWSTR* Id) override;
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes** ppEnum) override;
    STDMETHODIMP QueryInternalConnections(IPin** apPin, ULONG* nPin) override;
    STDMETHODIMP EndOfStream() override;
    STDMETHODIMP BeginFlush() override;
    STDMETHODIMP EndFlush() override;
    STDMETHODIMP NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) override;

    // IKsPropertySet (REQUIRED FOR CHROME/TEAMS/ZOOM)
    STDMETHODIMP Set(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData, ULONG ulInstanceLength, LPVOID pPropertyData, ULONG ulDataLength) override;
    STDMETHODIMP Get(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData, ULONG ulInstanceLength, LPVOID pPropertyData, ULONG ulDataLength, ULONG* pBytesReturned) override;
    STDMETHODIMP QuerySupported(REFGUID rguidPropSet, ULONG ulId, ULONG* pTypeSupport) override;

    // IAMStreamConfig
    STDMETHODIMP SetFormat(AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP GetFormat(AM_MEDIA_TYPE** ppmt) override;
    STDMETHODIMP GetNumberOfCapabilities(int* piCount, int* piSize) override;
    STDMETHODIMP GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) override;

    // IQualityControl
    STDMETHODIMP Notify(IBaseFilter* pSelf, Quality q) override;
    STDMETHODIMP SetSink(IQualityControl* piqc) override;

    // Streaming Control called by GestCamFilter
    HRESULT Active();
    HRESULT Inactive();

    // Helper for testing
    bool ReadFrameForTest(uint8_t* out_buffer, bool& out_is_fallback);

private:
    void ThreadProc();
    static void CopyFlippedRGB24(const uint8_t* src, uint8_t* dst, int width, int height);

    std::atomic<ULONG> ref_count_{1};
    GestCamFilter* filter_ = nullptr;
    IPin* connected_pin_ = nullptr;
    IMemAllocator* allocator_ = nullptr;
    AM_MEDIA_TYPE current_media_type_{};

    std::atomic<bool> is_streaming_{false};
    std::thread worker_thread_;

    SharedMemoryConsumer shm_consumer_;
    uint64_t last_shm_frame_index_ = 0;
    uint64_t last_frame_time_ms_ = 0;
    uint64_t frame_tick_ = 0;
};

// Helper: Free an AM_MEDIA_TYPE structure
void DeleteMediaType(AM_MEDIA_TYPE* pmt);
HRESULT CreateDefaultMediaType(AM_MEDIA_TYPE* pmt);
HRESULT CopyMediaType(AM_MEDIA_TYPE* pDest, const AM_MEDIA_TYPE* pSrc);

} // namespace gestcam::driver
