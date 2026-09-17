#pragma once

#include <windows.h>
#include <initguid.h>

// GestCam Virtual Camera Filter CLSID: {9E38B6B2-225C-479D-86D2-959D960814B2}
DEFINE_GUID(CLSID_GestCamVirtualCamera,
    0x9e38b6b2, 0x225c, 0x479d, 0x86, 0xd2, 0x95, 0x9d, 0x96, 0x08, 0x14, 0xb2);

// DirectShow Video Input Device Category CLSID: {860BB310-5D01-11D0-BD3B-00A0C911CE86}
DEFINE_GUID(CLSID_VideoInputDeviceCategory_GestCam,
    0x860bb310, 0x5d01, 0x11d0, 0xbd, 0x3b, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86);

// Pin Category Capture: {FB6C4281-0353-11D1-905F-0000C0CC16BA}
DEFINE_GUID(PIN_CATEGORY_CAPTURE_GestCam,
    0xfb6c4281, 0x0353, 0x11d1, 0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba);

// Pin Property Set: {9B00F101-1567-11D1-B3F1-00AA003761C5}
DEFINE_GUID(AMPROPSETID_Pin_Standard,
    0x9b00f101, 0x1567, 0x11d1, 0xb3, 0xf1, 0x00, 0xaa, 0x00, 0x37, 0x61, 0xc5);

// Pin Property Set (Legacy alias): {C6E13370-30AC-11CF-A18C-00AA0061CE92}
DEFINE_GUID(AMPROPSETID_Pin_GestCam,
    0xc6e13370, 0x30ac, 0x11cf, 0xa1, 0x8c, 0x00, 0xaa, 0x00, 0x61, 0xce, 0x92);

constexpr const wchar_t* GESTCAM_VIRTUALCAM_FRIENDLY_NAME = L"GestCam Virtual Camera";
constexpr const char* GESTCAM_VIRTUALCAM_FRIENDLY_NAME_A = "GestCam Virtual Camera";
constexpr const wchar_t* GESTCAM_VIRTUALCAM_CLSID_STR = L"{9E38B6B2-225C-479D-86D2-959D960814B2}";

