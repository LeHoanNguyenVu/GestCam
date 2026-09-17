#!/bin/bash
# GestCam Virtual Camera - DLL Build Script (MSYS2 bash)
# Run this via: "C:\Program Files\msys64\usr\bin\bash.exe" build_dll.sh

export PATH=/ucrt64/bin:/usr/bin:$PATH
export TMPDIR=/d/GestCam/build

BUILD_DIR="/d/GestCam/build/driver"
SRC_DIR="/d/GestCam"

echo "=== GestCam Virtual Camera DLL Build ==="
echo "Build dir: $BUILD_DIR"
echo ""

# Step 1: Compile all sources
echo "[1/2] Compiling source files..."

INCLUDES="-I/d/GestCam/include -I/d/GestCam"
FLAGS="-Dgestcam_virtualcam_EXPORTS -std=c++20 -O3 -mavx2 -Wall -Wextra"
OBJ_DIR="$BUILD_DIR/CMakeFiles/gestcam-virtualcam.dir"

c++ $FLAGS $INCLUDES -c "$SRC_DIR/driver/DllMain.cpp" -o "$OBJ_DIR/DllMain.cpp.obj" || { echo "FAILED: DllMain.cpp"; exit 1; }
c++ $FLAGS $INCLUDES -c "$SRC_DIR/driver/GestCamFilter.cpp" -o "$OBJ_DIR/GestCamFilter.cpp.obj" || { echo "FAILED: GestCamFilter.cpp"; exit 1; }
c++ $FLAGS $INCLUDES -c "$SRC_DIR/driver/GestCamStream.cpp" -o "$OBJ_DIR/GestCamStream.cpp.obj" || { echo "FAILED: GestCamStream.cpp"; exit 1; }
c++ $FLAGS $INCLUDES -c "$SRC_DIR/driver/FallbackFrame.cpp" -o "$OBJ_DIR/FallbackFrame.cpp.obj" || { echo "FAILED: FallbackFrame.cpp"; exit 1; }
c++ $FLAGS $INCLUDES -c "$SRC_DIR/src/SharedMemoryConsumer.cpp" -o "$OBJ_DIR/__/src/SharedMemoryConsumer.cpp.obj" || { echo "FAILED: SharedMemoryConsumer.cpp"; exit 1; }

echo "  All sources compiled OK"

# Step 2: Archive and Link
echo "[2/2] Linking gestcam-virtualcam.dll..."

# Rebuild objects.a
cmake -E rm -f "$OBJ_DIR/objects.a" 2>/dev/null || rm -f "$OBJ_DIR/objects.a"
ar qc "$OBJ_DIR/objects.a" \
    "$OBJ_DIR/DllMain.cpp.obj" \
    "$OBJ_DIR/GestCamFilter.cpp.obj" \
    "$OBJ_DIR/GestCamStream.cpp.obj" \
    "$OBJ_DIR/FallbackFrame.cpp.obj" \
    "$OBJ_DIR/__/src/SharedMemoryConsumer.cpp.obj" || { echo "FAILED: ar"; exit 1; }

# Link
c++ -shared \
    "$SRC_DIR/driver/gestcam-virtualcam.def" \
    -static -static-libgcc -static-libstdc++ \
    -o "$BUILD_DIR/gestcam-virtualcam.dll" \
    -Wl,--out-implib,"$BUILD_DIR/libgestcam-virtualcam.dll.a" \
    -Wl,--major-image-version,0,--minor-image-version,0 \
    -Wl,--whole-archive "$OBJ_DIR/objects.a" \
    -Wl,--no-whole-archive \
    -lole32 -loleaut32 -luuid -lstrmiids -ladvapi32 \
    -lgdi32 -lwinmm -lkernel32 -luser32 \
    -lwinspool -lshell32 -lcomdlg32 \
    || { echo "FAILED: linker"; exit 1; }

echo ""
echo "=== BUILD SUCCESS ==="
echo "Output: D:\GestCam\build\driver\gestcam-virtualcam.dll"
