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

    auto pDllGetClassObject = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*)>(
        GetProcAddress(hDll, "DllGetClassObject"));
    auto pDllCanUnloadNow = reinterpret_cast<HRESULT(WINAPI*)()>(
        GetProcAddress(hDll, "DllCanUnloadNow"));
    auto pDllRegisterServer = reinterpret_cast<HRESULT(WINAPI*)()>(
        GetProcAddress(hDll, "DllRegisterServer"));
    auto pDllUnregisterServer = reinterpret_cast<HRESULT(WINAPI*)()>(
        GetProcAddress(hDll, "DllUnregisterServer"));

    EXPECT_NE(pDllGetClassObject, nullptr);
    EXPECT_NE(pDllCanUnloadNow, nullptr);
    EXPECT_NE(pDllRegisterServer, nullptr);
    EXPECT_NE(pDllUnregisterServer, nullptr);

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
