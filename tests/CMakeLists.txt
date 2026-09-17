add_executable(test_smoke test_smoke.cpp)

target_include_directories(test_smoke PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(test_smoke PRIVATE
    GTest::gtest_main
    ${MF_LIBRARIES}
)

add_executable(test_camera test_camera.cpp)

target_include_directories(test_camera PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(test_camera PRIVATE
    gestcam_lib
    GTest::gtest_main
    ${MF_LIBRARIES}
)

add_executable(test_color_ringbuffer test_color_ringbuffer.cpp)

target_include_directories(test_color_ringbuffer PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(test_color_ringbuffer PRIVATE
    gestcam_lib
    GTest::gtest_main
    ${MF_LIBRARIES}
)

add_executable(test_ipc test_ipc.cpp)

target_include_directories(test_ipc PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(test_ipc PRIVATE
    gestcam_lib
    GTest::gtest_main
    ${MF_LIBRARIES}
)

add_executable(test_virtualcam
    test_virtualcam.cpp
    ${PROJECT_SOURCE_DIR}/driver/GestCamFilter.cpp
    ${PROJECT_SOURCE_DIR}/driver/GestCamStream.cpp
    ${PROJECT_SOURCE_DIR}/driver/FallbackFrame.cpp
)

target_include_directories(test_virtualcam PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(test_virtualcam PRIVATE
    gestcam_lib
    GTest::gtest_main
    ${MF_LIBRARIES}
    -lgdi32 -lwinmm
)

add_dependencies(test_virtualcam gestcam-virtualcam)

add_custom_command(TARGET test_virtualcam POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:gestcam-virtualcam>
    $<TARGET_FILE_DIR:test_virtualcam>
)

add_executable(test_onnx_directml test_onnx_directml.cpp)

target_include_directories(test_onnx_directml PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/onnxruntime/include
)

target_link_libraries(test_onnx_directml PRIVATE
    gestcam_lib
    GTest::gtest_main
    ${MF_LIBRARIES}
    onnxruntime
    DirectML
)

add_custom_command(TARGET test_onnx_directml POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/third_party/onnxruntime/bin/onnxruntime.dll
    $<TARGET_FILE_DIR:test_onnx_directml>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/third_party/onnxruntime/bin/DirectML.dll
    $<TARGET_FILE_DIR:test_onnx_directml>
)

add_executable(test_ultraface test_ultraface.cpp)

target_include_directories(test_ultraface PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/onnxruntime/include
)

target_link_libraries(test_ultraface PRIVATE
    gestcam_lib
    GTest::gtest_main
    ${MF_LIBRARIES}
    onnxruntime
    DirectML
)

add_custom_command(TARGET test_ultraface POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/third_party/onnxruntime/bin/onnxruntime.dll
    $<TARGET_FILE_DIR:test_ultraface>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/third_party/onnxruntime/bin/DirectML.dll
    $<TARGET_FILE_DIR:test_ultraface>
)

add_executable(test_hand_tracker test_hand_tracker.cpp)

target_include_directories(test_hand_tracker PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/onnxruntime/include
)

target_link_libraries(test_hand_tracker PRIVATE
    gestcam_lib
    GTest::gtest_main
    ${MF_LIBRARIES}
    onnxruntime
    DirectML
)

add_custom_command(TARGET test_hand_tracker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/third_party/onnxruntime/bin/onnxruntime.dll
    $<TARGET_FILE_DIR:test_hand_tracker>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/third_party/onnxruntime/bin/DirectML.dll
    $<TARGET_FILE_DIR:test_hand_tracker>
)

include(GoogleTest)
gtest_discover_tests(test_smoke)
gtest_discover_tests(test_camera)
gtest_discover_tests(test_color_ringbuffer)
gtest_discover_tests(test_ipc)
gtest_discover_tests(test_virtualcam)
gtest_discover_tests(test_onnx_directml)
gtest_discover_tests(test_ultraface)
gtest_discover_tests(test_hand_tracker)
