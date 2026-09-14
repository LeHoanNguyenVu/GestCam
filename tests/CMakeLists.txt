add_executable(test_smoke test_smoke.cpp)

target_include_directories(test_smoke PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(test_smoke PRIVATE
    GTest::gtest_main
    ${MF_LIBRARIES}
)

include(GoogleTest)
gtest_discover_tests(test_smoke)
