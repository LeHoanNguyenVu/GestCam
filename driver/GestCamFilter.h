#pragma once

#include <windows.h>
#include <dshow.h>
#include <strmif.h>
#include <atomic>
#include <string>

namespace gestcam::driver {

class GestCamStream;

class GestCamFilter : public IBaseFilter {
public:
    GestCamFilter();
    virtual ~GestCamFilter();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IPersist
    STDMETHODIMP GetClassID(CLSID* pClassID) override;

    // IMediaFilter
    STDMETHODIMP Stop() override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Run(REFERENCE_TIME tStart) override;
    STDMETHODIMP GetState(DWORD dwMilliSecsTimeout, FILTER_STATE* State) override;
    STDMETHODIMP SetSyncSource(IReferenceClock* pClock) override;
    STDMETHODIMP GetSyncSource(IReferenceClock** pClock) override;

    // IBaseFilter
    STDMETHODIMP EnumPins(IEnumPins** ppEnum) override;
    STDMETHODIMP FindPin(LPCWSTR Id, IPin** ppPin) override;
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* pInfo) override;
    STDMETHODIMP JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) override;
    STDMETHODIMP QueryVendorInfo(LPWSTR* pVendorInfo) override;

    GestCamStream* GetStream() const { return pin_; }
    FILTER_STATE GetCurrentFilterState() const { return state_; }

private:
    std::atomic<ULONG> ref_count_{1};
    FILTER_STATE state_ = State_Stopped;
    IReferenceClock* clock_ = nullptr;
    IFilterGraph* graph_ = nullptr;
    std::wstring filter_name_;
    GestCamStream* pin_ = nullptr;
};

} // namespace gestcam::driver
