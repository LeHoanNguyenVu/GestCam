#include <windows.h>
#include <unknwn.h>
#include <dshow.h>
#include <strmif.h>
#include <string>
#include <atomic>
#include "driver/GestCamGuids.h"
#include "driver/GestCamFilter.h"


static HMODULE g_hModule = nullptr;
static std::atomic<LONG> g_lockCount{0};

// Helper: Set a registry string value
static LONG SetRegString(HKEY hRoot, const std::wstring& subkey, const std::wstring& valName, const std::wstring& data) {
    HKEY hKey = nullptr;
    LONG res = RegCreateKeyExW(hRoot, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (res == ERROR_SUCCESS) {
        res = RegSetValueExW(hKey, valName.empty() ? nullptr : valName.c_str(), 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(data.c_str()),
                             static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    return res;
}

// Minimal IClassFactory implementation
class GestCamClassFactory : public IClassFactory {
public:
    GestCamClassFactory() = default;
    virtual ~GestCamClassFactory() = default;

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
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

    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (pUnkOuter != nullptr) return CLASS_E_NOAGGREGATION;

        auto* filter = new gestcam::driver::GestCamFilter();
        HRESULT hr = filter->QueryInterface(riid, ppv);
        filter->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) {
            ++g_lockCount;
        } else {
            --g_lockCount;
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_count_{1};
};

// ============================================================================
// DLL Exported Functions
// ============================================================================

extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hModule = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

extern "C" HRESULT WINAPI DllCanUnloadNow(void) {
    return (g_lockCount == 0) ? S_OK : S_FALSE;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (IsEqualCLSID(rclsid, CLSID_GestCamVirtualCamera)) {
        auto* factory = new GestCamClassFactory();
        HRESULT hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT WINAPI DllRegisterServer(void) {
    wchar_t dllPath[MAX_PATH] = {0};
    if (GetModuleFileNameW(g_hModule, dllPath, MAX_PATH) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    std::wstring clsidStr = GESTCAM_VIRTUALCAM_CLSID_STR;
    std::wstring clsidKey = L"CLSID\\" + clsidStr;

    // 1. Register under HKCR\CLSID\{CLSID}
    SetRegString(HKEY_CLASSES_ROOT, clsidKey, L"", GESTCAM_VIRTUALCAM_FRIENDLY_NAME);
    SetRegString(HKEY_CLASSES_ROOT, clsidKey + L"\\InprocServer32", L"", dllPath);
    SetRegString(HKEY_CLASSES_ROOT, clsidKey + L"\\InprocServer32", L"ThreadingModel", L"Both");

    // 2. Register under DirectShow Video Input Device Category using IFilterMapper2
    // This writes the mandatory binary 'FilterData' required by Windows Media Foundation / Chrome
    HRESULT hrCo = CoInitialize(nullptr);
    IFilterMapper2* pFM = nullptr;
    HRESULT hrFM = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IFilterMapper2, reinterpret_cast<void**>(&pFM));
    if (SUCCEEDED(hrFM) && pFM) {
        REGPINTYPES regTypes[1];
        regTypes[0].clsMajorType = &MEDIATYPE_Video;
        regTypes[0].clsMinorType = &MEDIASUBTYPE_RGB24;

        REGFILTERPINS2 regPin;
        ZeroMemory(&regPin, sizeof(regPin));
        regPin.dwFlags = REG_PINFLAG_B_OUTPUT;
        regPin.cInstances = 1;
        regPin.nMediaTypes = 1;
        regPin.lpMediaType = regTypes;
        regPin.nMediums = 0;
        regPin.lpMedium = nullptr;
        regPin.clsPinCategory = &PIN_CATEGORY_CAPTURE_GestCam;

        REGFILTER2 rf2;
        ZeroMemory(&rf2, sizeof(rf2));
        rf2.dwVersion = 2;
        // MERIT_UNLIKELY allows Windows Camera Frame Server to bridge this
        // DirectShow filter into the Media Foundation stack (required for
        // Chrome, Edge, and Google Meet on Windows 10/11).
        // MERIT_DO_NOT_USE would make the Frame Server skip this device.
        rf2.dwMerit = MERIT_UNLIKELY;
        rf2.cPins2 = 1;
        rf2.rgPins2 = &regPin;


        HRESULT hrRegFilter = pFM->RegisterFilter(
            CLSID_GestCamVirtualCamera,
            GESTCAM_VIRTUALCAM_FRIENDLY_NAME,
            nullptr,
            &CLSID_VideoInputDeviceCategory_GestCam,
            GESTCAM_VIRTUALCAM_CLSID_STR,
            &rf2
        );
        if (FAILED(hrRegFilter)) {
            OutputDebugStringA("pFM->RegisterFilter FAILED!\n");
        }
        pFM->Release();
    }

    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }

    // 3. Ensure FriendlyName and CLSID are explicitly present
    std::wstring catKey = L"CLSID\\{860BB310-5D01-11D0-BD3B-00A0C911CE86}\\Instance\\" + clsidStr;
    SetRegString(HKEY_CLASSES_ROOT, catKey, L"CLSID", clsidStr);
    SetRegString(HKEY_CLASSES_ROOT, catKey, L"FriendlyName", GESTCAM_VIRTUALCAM_FRIENDLY_NAME);

    return S_OK;
}

extern "C" HRESULT WINAPI DllUnregisterServer(void) {
    HRESULT hrCo = CoInitialize(nullptr);
    IFilterMapper2* pFM = nullptr;
    HRESULT hrFM = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IFilterMapper2, reinterpret_cast<void**>(&pFM));
    if (SUCCEEDED(hrFM) && pFM) {
        pFM->UnregisterFilter(
            &CLSID_VideoInputDeviceCategory_GestCam,
            GESTCAM_VIRTUALCAM_CLSID_STR,
            CLSID_GestCamVirtualCamera
        );
        pFM->Release();
    }
    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }

    std::wstring clsidStr = GESTCAM_VIRTUALCAM_CLSID_STR;
    std::wstring catKey = L"CLSID\\{860BB310-5D01-11D0-BD3B-00A0C911CE86}\\Instance\\" + clsidStr;
    std::wstring clsidKey = L"CLSID\\" + clsidStr;

    RegDeleteTreeW(HKEY_CLASSES_ROOT, catKey.c_str());
    RegDeleteTreeW(HKEY_CLASSES_ROOT, clsidKey.c_str());

    return S_OK;
}

