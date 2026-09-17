#include "driver/GestCamFilter.h"
#include "driver/GestCamStream.h"
#include "driver/GestCamGuids.h"

namespace gestcam::driver {

// Minimal IEnumPins implementation
class GestCamEnumPins : public IEnumPins {
public:
    explicit GestCamEnumPins(IPin* pin) : pin_(pin) {
        if (pin_) pin_->AddRef();
    }

    virtual ~GestCamEnumPins() {
        if (pin_) pin_->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumPins) {
            *ppv = static_cast<IEnumPins*>(this);
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

    STDMETHODIMP Next(ULONG cPins, IPin** ppPins, ULONG* pcFetched) override {
        if (!ppPins) return E_POINTER;
        if (cPins > 1 && !pcFetched) return E_INVALIDARG;

        ULONG fetched = 0;
        if (pos_ == 0 && cPins > 0 && pin_) {
            ppPins[0] = pin_;
            pin_->AddRef();
            fetched = 1;
            pos_ = 1;
        }
        if (pcFetched) *pcFetched = fetched;
        return (fetched == cPins) ? S_OK : S_FALSE;
    }

    STDMETHODIMP Skip(ULONG cPins) override {
        pos_ += cPins;
        return (pos_ <= 1) ? S_OK : S_FALSE;
    }

    STDMETHODIMP Reset() override {
        pos_ = 0;
        return S_OK;
    }

    STDMETHODIMP Clone(IEnumPins** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        auto* clone = new GestCamEnumPins(pin_);
        clone->pos_ = pos_;
        *ppEnum = clone;
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_count_{1};
    IPin* pin_ = nullptr;
    ULONG pos_ = 0;
};

// ============================================================================
// GestCamFilter Implementation
// ============================================================================

GestCamFilter::GestCamFilter() {
    HRESULT hr = S_OK;
    pin_ = new GestCamStream(this, &hr);
}

GestCamFilter::~GestCamFilter() {
    if (pin_) {
        pin_->Release();
        pin_ = nullptr;
    }
    if (clock_) {
        clock_->Release();
        clock_ = nullptr;
    }
}

STDMETHODIMP GestCamFilter::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPersist || riid == IID_IMediaFilter || riid == IID_IBaseFilter) {
        *ppv = static_cast<IBaseFilter*>(this);
        AddRef();
        return S_OK;
    } else if (riid == IID_IAMFilterMiscFlags) {
        *ppv = static_cast<IAMFilterMiscFlags*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) GestCamFilter::GetMiscFlags() {
    return AM_FILTER_MISC_FLAGS_IS_SOURCE;
}


STDMETHODIMP_(ULONG) GestCamFilter::AddRef() {
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) GestCamFilter::Release() {
    ULONG u = --ref_count_;
    if (u == 0) delete this;
    return u;
}

// IPersist
STDMETHODIMP GestCamFilter::GetClassID(CLSID* pClassID) {
    if (!pClassID) return E_POINTER;
    *pClassID = CLSID_GestCamVirtualCamera;
    return S_OK;
}

// IMediaFilter
STDMETHODIMP GestCamFilter::Stop() {
    state_ = State_Stopped;
    if (pin_) pin_->Inactive();
    return S_OK;
}

STDMETHODIMP GestCamFilter::Pause() {
    state_ = State_Paused;
    if (pin_) pin_->Active();
    return S_OK;
}

STDMETHODIMP GestCamFilter::Run(REFERENCE_TIME) {
    state_ = State_Running;
    if (pin_) pin_->Active();
    return S_OK;
}

STDMETHODIMP GestCamFilter::GetState(DWORD, FILTER_STATE* State) {
    if (!State) return E_POINTER;
    *State = state_;
    return S_OK;
}

STDMETHODIMP GestCamFilter::SetSyncSource(IReferenceClock* pClock) {
    if (clock_) clock_->Release();
    clock_ = pClock;
    if (clock_) clock_->AddRef();
    return S_OK;
}

STDMETHODIMP GestCamFilter::GetSyncSource(IReferenceClock** pClock) {
    if (!pClock) return E_POINTER;
    *pClock = clock_;
    if (clock_) clock_->AddRef();
    return S_OK;
}

// IBaseFilter
STDMETHODIMP GestCamFilter::EnumPins(IEnumPins** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new GestCamEnumPins(pin_);
    return S_OK;
}

STDMETHODIMP GestCamFilter::FindPin(LPCWSTR Id, IPin** ppPin) {
    if (!Id || !ppPin) return E_POINTER;
    if (wcscmp(Id, L"Capture") == 0 && pin_) {
        *ppPin = pin_;
        pin_->AddRef();
        return S_OK;
    }
    *ppPin = nullptr;
    return VFW_E_NOT_FOUND;
}

STDMETHODIMP GestCamFilter::QueryFilterInfo(FILTER_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    wcscpy_s(pInfo->achName, filter_name_.empty() ? GESTCAM_VIRTUALCAM_FRIENDLY_NAME : filter_name_.c_str());
    pInfo->pGraph = graph_;
    if (graph_) graph_->AddRef();
    return S_OK;
}

STDMETHODIMP GestCamFilter::JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) {
    graph_ = pGraph;
    if (pName) {
        filter_name_ = pName;
    } else {
        filter_name_ = GESTCAM_VIRTUALCAM_FRIENDLY_NAME;
    }
    return S_OK;
}

STDMETHODIMP GestCamFilter::QueryVendorInfo(LPWSTR* pVendorInfo) {
    if (!pVendorInfo) return E_POINTER;
    *pVendorInfo = reinterpret_cast<LPWSTR>(CoTaskMemAlloc(sizeof(L"GestCam Project")));
    if (!*pVendorInfo) return E_OUTOFMEMORY;
    wcscpy_s(*pVendorInfo, 16, L"GestCam Project");
    return S_OK;
}

} // namespace gestcam::driver
