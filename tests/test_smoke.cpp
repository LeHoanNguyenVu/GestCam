#include <gtest/gtest.h>
#include "gestcam/Version.h"

#ifdef _WIN32
    #include <windows.h>
    #include <mfapi.h>
#endif

TEST(SmokeTest, CPlusPlus20Standard) {
#if defined(_MSVC_LANG)
    EXPECT_GE(_MSVC_LANG, 202002L) << "Project must be compiled with at least C++20 standard!";
#else
    EXPECT_GE(__cplusplus, 202002L) << "Project must be compiled with at least C++20 standard!";
#endif
}

TEST(SmokeTest, CompilerAVX2FlagEnabled) {
    EXPECT_TRUE(gestcam::SystemInfo::kCompiledWithAVX2) 
        << "Compiler flags must enable AVX2 instruction set (/arch:AVX2 or -mavx2)!";
}

TEST(SmokeTest, VersionStringFormat) {
    std::string version = gestcam::SystemInfo::kVersion;
    EXPECT_FALSE(version.empty());
    EXPECT_EQ(version, "1.1.0");
}

TEST(SmokeTest, MediaFoundationLinkage) {
    HRESULT hr = MFStartup(MF_VERSION);
    ASSERT_TRUE(SUCCEEDED(hr)) << "Media Foundation failed to start up. HRESULT: " << std::hex << hr;
    hr = MFShutdown();
    EXPECT_TRUE(SUCCEEDED(hr)) << "Media Foundation failed to shut down.";
}
