# VirtualCam DirectShow Driver Module

add_library(gestcam-virtualcam SHARED
    DllMain.cpp
    GestCamFilter.cpp
    GestCamStream.cpp
    FallbackFrame.cpp
    ${PROJECT_SOURCE_DIR}/src/SharedMemoryConsumer.cpp
    gestcam-virtualcam.def
)

target_include_directories(gestcam-virtualcam PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}
)

if(MSVC)
    target_link_libraries(gestcam-virtualcam PRIVATE
        ole32 oleaut32 uuid strmiids advapi32 gdi32 winmm
    )
else()
    target_link_libraries(gestcam-virtualcam PRIVATE
        -lole32 -loleaut32 -luuid -lstrmiids -ladvapi32 -lgdi32 -lwinmm
    )
    set_target_properties(gestcam-virtualcam PROPERTIES
        LINK_FLAGS "-static -static-libgcc -static-libstdc++"
    )
endif()

set_target_properties(gestcam-virtualcam PROPERTIES
    OUTPUT_NAME "gestcam-virtualcam"
    PREFIX ""
)

message(STATUS "[GestCam] VirtualCam Driver target configured (gestcam-virtualcam.dll)")
