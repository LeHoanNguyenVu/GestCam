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

include(GoogleTest)
gtest_discover_tests(test_smoke)
gtest_discover_tests(test_camera)
gtest_discover_tests(test_color_ringbuffer)
gtest_discover_tests(test_ipc)
