cmake_minimum_required(VERSION 3.24)

project(wave3d VERSION 0.1.0 LANGUAGES CXX)

option(WAVE3D_BUILD_TESTS "Build Wave3D tests" ON)

add_library(wave3d_core INTERFACE)
target_include_directories(wave3d_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(wave3d_core INTERFACE cxx_std_17)

if(MSVC)
    target_compile_options(wave3d_core INTERFACE /W4 /permissive-)
else()
    target_compile_options(wave3d_core INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_executable(wave3d_forward apps/forward3d.cpp)
target_link_libraries(wave3d_forward PRIVATE wave3d_core)

if(WAVE3D_BUILD_TESTS)
    enable_testing()
    add_executable(wave3d_core_tests tests/test_grid.cpp)
    target_link_libraries(wave3d_core_tests PRIVATE wave3d_core)
    add_test(NAME wave3d_core_tests COMMAND wave3d_core_tests)
endif()
