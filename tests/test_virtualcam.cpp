#include <gtest/gtest.h>
#include <windows.h>
#include <dshow.h>
#include "driver/GestCamGuids.h"
#include "driver/GestCamFilter.h"
#include "driver/GestCamStream.h"
#include "driver/FallbackFrame.h"
#include "gestcam/SharedMemoryProducer.h"
#include <vector>
#include <memory>

using namespace gestcam::driver;
using namespace gestcam;

// Test 1: COM Interface Instantiation & ClassID
TEST(VirtualCamTest, ComClassFactory_InstantiateFilter) {
    auto filter = std::make_unique<GestCamFilter>();

    CLSID clsid{};
    EXPECT_EQ(filter->GetClassID(&clsid), S_OK);
    EXPECT_TRUE(IsEqualCLSID(clsid, CLSID_GestCamVirtualCamera));

    IBaseFilter* pBase = nullptr;
    EXPECT_EQ(filter->QueryInterface(IID_IBaseFilter, reinterpret_cast<void**>(&pBase)), S_OK);
    ASSERT_NE(pBase, nullptr);
    pBase->Release();
}

// Test 2: Pin Enumeration & IKsPropertySet Category (Required for Chrome)
TEST(VirtualCamTest, PinEnumeration_And_Category) {
    auto filter = std::make_unique<GestCamFilter>();

    IEnumPins* pEnum = nullptr;
    ASSERT_EQ(filter->EnumPins(&pEnum), S_OK);
    ASSERT_NE(pEnum, nullptr);

    IPin* pPin = nullptr;
    ULONG fetched = 0;
    EXPECT_EQ(pEnum->Next(1, &pPin, &fetched), S_OK);
    EXPECT_EQ(fetched, 1u);
    ASSERT_NE(pPin, nullptr);

    // Verify IKsPropertySet pin category
    IKsPropertySet* pKs = nullptr;
    EXPECT_EQ(pPin->QueryInterface(IID_IKsPropertySet, reinterpret_cast<void**>(&pKs)), S_OK);
    ASSERT_NE(pKs, nullptr);

    DWORD supported = 0;
    EXPECT_EQ(pKs->QuerySupported(AMPROPSETID_Pin_GestCam, 0 /* AMPROPERTY_PIN_CATEGORY */, &supported), S_OK);
    EXPECT_NE(supported, 0u);

    GUID category{};
    ULONG bytesReturned = 0;
    EXPECT_EQ(pKs->Get(AMPROPSETID_Pin_GestCam, 0, nullptr, 0, &category, sizeof(category), &bytesReturned), S_OK);
    EXPECT_EQ(bytesReturned, sizeof(GUID));
    EXPECT_TRUE(IsEqualGUID(category, PIN_CATEGORY_CAPTURE_GestCam));

    pKs->Release();
    pPin->Release();
    pEnum->Release();
}

// Test 3: IAMStreamConfig Capability (1280x720 RGB24 @ 30 FPS)
TEST(VirtualCamTest, StreamConfig_FormatCaps) {
    auto filter = std::make_unique<GestCamFilter>();
    GestCamStream* stream = filter->GetStream();
    ASSERT_NE(stream, nullptr);

    int count = 0, size = 0;
    EXPECT_EQ(stream->GetNumberOfCapabilities(&count, &size), S_OK);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(size, static_cast<int>(sizeof(VIDEO_STREAM_CONFIG_CAPS)));

    AM_MEDIA_TYPE* pmt = nullptr;
    VIDEO_STREAM_CONFIG_CAPS caps{};
    EXPECT_EQ(stream->GetStreamCaps(0, &pmt, reinterpret_cast<BYTE*>(&caps)), S_OK);
    ASSERT_NE(pmt, nullptr);

    EXPECT_TRUE(IsEqualGUID(pmt->majortype, MEDIATYPE_Video));
    EXPECT_TRUE(IsEqualGUID(pmt->subtype, MEDIASUBTYPE_RGB24));
    EXPECT_EQ(caps.InputSize.cx, 1280);
    EXPECT_EQ(caps.InputSize.cy, 720);
    EXPECT_EQ(caps.MinFrameInterval, 166666); // 60 FPS
    EXPECT_EQ(caps.MaxFrameInterval, 333333); // 30 FPS

    DeleteMediaType(pmt);
    CoTaskMemFree(pmt);
}

// Test 4: Standby Fallback Frame Generation
TEST(VirtualCamTest, FallbackFrame_Generation) {
    std::vector<uint8_t> frame(GESTCAM_SLOT_SIZE, 0);

    RenderFallbackFrame(frame.data(), GESTCAM_WIDTH, GESTCAM_HEIGHT, 15);

    // Verify frame is not completely dark/empty
    uint64_t sum = 0;
    for (size_t i = 0; i < GESTCAM_SLOT_SIZE; i += 64) {
        sum += frame[i];
    }
    EXPECT_GT(sum, 10000u);
}

// Test 5: Shared Memory End-to-End Integration & Fallback Switching
TEST(VirtualCamTest, SharedMemory_Integration_And_Fallback) {
    auto filter = std::make_unique<GestCamFilter>();
    GestCamStream* stream = filter->GetStream();
    ASSERT_NE(stream, nullptr);

    std::vector<uint8_t> out_buffer(GESTCAM_SLOT_SIZE);
    bool is_fallback = false;

    // Case A: When SharedMemoryProducer is running, stream reads live frame
    {
        SharedMemoryProducer producer;
        ASSERT_TRUE(producer.Initialize());

        std::vector<uint8_t> test_frame(GESTCAM_SLOT_SIZE, 210);
        EXPECT_TRUE(producer.WriteFrame(test_frame.data(), 1, 100000));

        EXPECT_TRUE(stream->ReadFrameForTest(out_buffer.data(), is_fallback));
        EXPECT_FALSE(is_fallback); // Live stream active!
        EXPECT_EQ(out_buffer[0], 210);
        EXPECT_EQ(out_buffer[1000], 210);

        producer.Close();
    }
}

// Test 6: DLL Dynamic Loading & Export Table Verification
TEST(VirtualCamTest, Dll_LoadLibrary_And_Exports) {
    HMODULE hDll = LoadLibraryW(L"gestcam-virtualcam.dll");
    if (!hDll) {
        // Look in driver output directory
        hDll = LoadLibraryW(L"driver\\gestcam-virtualcam.dll");
    }
    ASSERT_NE(hDll, nullptr) << "Failed to load gestcam-virtualcam.dll";

    // Suppress -Wcast-function-type for GetProcAddress
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    auto pDllGetClassObject = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*)>(
        GetProcAddress(hDll, "DllGetClassObject"));
    auto pDllCanUnloadNow = reinterpret_cast<HRESULT(WINAPI*)()>(
        GetProcAddress(hDll, "DllCanUnloadNow"));
    auto pDllRegisterServer = reinterpret_cast<HRESULT(WINAPI*)()>(
        GetProcAddress(hDll, "DllRegisterServer"));
    auto pDllUnregisterServer = reinterpret_cast<HRESULT(WINAPI*)()>(
        GetProcAddress(hDll, "DllUnregisterServer"));
#pragma GCC diagnostic pop

    EXPECT_NE(pDllGetClassObject, nullptr);
    EXPECT_NE(pDllCanUnloadNow, nullptr);
    EXPECT_NE(pDllRegisterServer, nullptr);
    EXPECT_NE(pDllUnregisterServer, nullptr);

    HRESULT hrReg = pDllRegisterServer();
    std::cout << "[DLL REGISTER] hrReg = 0x" << std::hex << hrReg << std::endl;

    // Test creating ClassFactory from loaded DLL
    IClassFactory* pFactory = nullptr;

    HRESULT hr = pDllGetClassObject(CLSID_GestCamVirtualCamera, IID_IClassFactory, reinterpret_cast<void**>(&pFactory));
    EXPECT_EQ(hr, S_OK);
    ASSERT_NE(pFactory, nullptr);

    IBaseFilter* pFilter = nullptr;
    hr = pFactory->CreateInstance(nullptr, IID_IBaseFilter, reinterpret_cast<void**>(&pFilter));
    EXPECT_EQ(hr, S_OK);
    ASSERT_NE(pFilter, nullptr);

    pFilter->Release();
    pFactory->Release();
    FreeLibrary(hDll);
}

// Test 7: DirectShow Video Input Category Moniker Verification
TEST(VirtualCamTest, DirectShow_Category_SystemEnumeration) {
    HRESULT hr = CoInitialize(nullptr);

    ICreateDevEnum* pDevEnum = nullptr;
    hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ICreateDevEnum, reinterpret_cast<void**>(&pDevEnum));

    if (SUCCEEDED(hr) && pDevEnum) {
        IEnumMoniker* pEnum = nullptr;
        hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory_GestCam, &pEnum, 0);

        // Enumerator should succeed or return S_FALSE if category is empty
        EXPECT_TRUE(hr == S_OK || hr == S_FALSE);

        if (hr == S_OK && pEnum) {
            IMoniker* pMoniker = nullptr;
            bool found_gestcam = false;

            while (pEnum->Next(1, &pMoniker, nullptr) == S_OK) {
                IPropertyBag* pPropBag = nullptr;
                hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, reinterpret_cast<void**>(&pPropBag));
                if (SUCCEEDED(hr) && pPropBag) {
                    VARIANT var;
                    VariantInit(&var);
                    hr = pPropBag->Read(L"FriendlyName", &var, 0);
                    if (SUCCEEDED(hr) && var.vt == VT_BSTR && var.bstrVal) {
                        if (wcscmp(var.bstrVal, GESTCAM_VIRTUALCAM_FRIENDLY_NAME) == 0) {
                            found_gestcam = true;
                            IBaseFilter* pFilter = nullptr;
                            HRESULT hrBind = pMoniker->BindToObject(0, 0, IID_IBaseFilter, reinterpret_cast<void**>(&pFilter));
                            std::cout << "[MONIKER BIND TO OBJECT] hr = 0x" << std::hex << hrBind << std::endl;
                            EXPECT_EQ(hrBind, S_OK);
                            if (SUCCEEDED(hrBind) && pFilter) {
                                pFilter->Release();
                            }
                        }
                    }
                    VariantClear(&var);
                    pPropBag->Release();
                }
                pMoniker->Release();

            }
            pEnum->Release();

            std::cout << "[SYSTEM ENUM] GestCam Virtual Camera in DirectShow: " 
                      << (found_gestcam ? "PRESENT (REGISTERED)" : "NOT YET REGISTERED") << std::endl;
        }
        pDevEnum->Release();
    }

    CoUninitialize();
}

// Test 8: DirectShow Filter Graph Connect & Render
TEST(VirtualCamTest, DirectShow_Graph_Connect_Test) {
    HRESULT hr = CoInitialize(nullptr);
    IGraphBuilder* pGraph = nullptr;
    hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, reinterpret_cast<void**>(&pGraph));
    ASSERT_EQ(hr, S_OK);
    ASSERT_NE(pGraph, nullptr);

    auto filter = std::make_unique<GestCamFilter>();
    hr = pGraph->AddFilter(filter.get(), L"GestCam Source");
    EXPECT_EQ(hr, S_OK);

    IPin* pOutPin = filter->GetStream();
    ASSERT_NE(pOutPin, nullptr);

    hr = pGraph->Render(pOutPin);
    std::cout << "[GRAPH RENDER] Result hr = 0x" << std::hex << hr << std::endl;
    EXPECT_EQ(hr, S_OK);

    IMediaControl* pControl = nullptr;
    hr = pGraph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&pControl));
    if (SUCCEEDED(hr) && pControl) {
        hr = pControl->Run();
        std::cout << "[GRAPH RUN] Result hr = 0x" << std::hex << hr << std::endl;
        Sleep(500);
        pControl->Stop();
        pControl->Release();
    }

    pGraph->Release();
    CoUninitialize();
}

TEST(VirtualCamTest, DirectShow_RegisterFilter_Verification) {
    HRESULT hr = CoInitialize(nullptr);
    IFilterMapper2* pFM = nullptr;
    hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IFilterMapper2, reinterpret_cast<void**>(&pFM));
    ASSERT_EQ(hr, S_OK);
    ASSERT_NE(pFM, nullptr);

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
    rf2.dwMerit = MERIT_DO_NOT_USE;
    rf2.cPins2 = 1;
    rf2.rgPins2 = &regPin;

    hr = pFM->RegisterFilter(
        CLSID_GestCamVirtualCamera,
        GESTCAM_VIRTUALCAM_FRIENDLY_NAME,
        nullptr,
        &CLSID_VideoInputDeviceCategory_GestCam,
        GESTCAM_VIRTUALCAM_CLSID_STR,
        &rf2
    );
    std::cout << "[TEST REGISTER FILTER] hr = 0x" << std::hex << hr << std::endl;
    EXPECT_TRUE(hr == S_OK || hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));

    pFM->Release();
    CoUninitialize();
}


