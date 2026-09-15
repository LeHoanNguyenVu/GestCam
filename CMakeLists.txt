cmake_minimum_required(VERSION 3.20)
project(GestCam VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Tối ưu hóa Compiler Flags và AVX2
if(MSVC)
    add_compile_options(/O2 /arch:AVX2 /W4 /permissive- /utf-8)
    set(MF_LIBRARIES mf mfplat mfreadwrite mfuuid ole32 oleaut32)
else()
    add_compile_options(-O3 -mavx2 -Wall -Wextra)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
    set(MF_LIBRARIES -lmf -lmfplat -lmfreadwrite -lmfuuid -lstrmiids -lole32 -loleaut32 -luuid)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Thư viện dùng chung (Core Library)
add_library(gestcam_lib STATIC
    src/CameraEnumerator.cpp
    src/MFCameraCapture.cpp
    src/MockCameraSource.cpp
)

target_link_libraries(gestcam_lib PUBLIC
    ${MF_LIBRARIES}
)

# Ứng dụng chính (Core Process CLI)
add_executable(gestcam_core
    src/main.cpp
)

target_link_libraries(gestcam_core PRIVATE
    gestcam_lib
)

# Cấu hình Driver DLL Module
add_subdirectory(driver)

# Cấu hình Kiểm thử Tự động (GoogleTest + CTest)
include(CTest)
enable_testing()

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# Ngăn chặn GoogleTest ghi đè cấu hình runtime của project cha
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_subdirectory(tests)
