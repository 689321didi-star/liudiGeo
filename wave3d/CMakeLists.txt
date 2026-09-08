cmake_minimum_required(VERSION 3.24)

project(wave3d VERSION 0.1.0 LANGUAGES CXX)

option(WAVE3D_BUILD_TESTS "Build Wave3D tests" ON)
option(WAVE3D_ENABLE_CUDA "Build the Wave3D CUDA foundation" OFF)

add_library(wave3d_core INTERFACE)
target_include_directories(wave3d_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(wave3d_core INTERFACE cxx_std_17)

target_compile_options(
    wave3d_core
    INTERFACE
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4;/permissive->
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>
        $<$<COMPILE_LANG_AND_ID:CUDA,NVIDIA>:-Xcompiler=-Wall,-Wextra>)

add_executable(wave3d_forward apps/forward3d.cpp)
target_link_libraries(wave3d_forward PRIVATE wave3d_core)

if(WAVE3D_BUILD_TESTS)
    enable_testing()
    add_executable(wave3d_core_tests tests/test_grid.cpp)
    target_link_libraries(wave3d_core_tests PRIVATE wave3d_core)
    add_test(NAME wave3d_core_tests COMMAND wave3d_core_tests)

    add_executable(wave3d_memory_plan_tests tests/test_memory_plan.cpp)
    target_link_libraries(wave3d_memory_plan_tests PRIVATE wave3d_core)
    add_test(NAME wave3d_memory_plan_tests COMMAND wave3d_memory_plan_tests)

    add_executable(wave3d_coordinate_tests tests/test_coordinates.cpp)
    target_link_libraries(wave3d_coordinate_tests PRIVATE wave3d_core)
    add_test(NAME wave3d_coordinate_tests COMMAND wave3d_coordinate_tests)

    add_executable(wave3d_model_tests tests/test_model.cpp)
    target_link_libraries(wave3d_model_tests PRIVATE wave3d_core)
    add_test(NAME wave3d_model_tests COMMAND wave3d_model_tests)

    add_executable(wave3d_acquisition_tests tests/test_acquisition.cpp)
    target_link_libraries(wave3d_acquisition_tests PRIVATE wave3d_core)
    add_test(NAME wave3d_acquisition_tests COMMAND wave3d_acquisition_tests)
endif()

if(WAVE3D_ENABLE_CUDA)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 120)
    endif()
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)

    add_library(
        wave3d_cuda
        STATIC
            src/cuda/device_info.cpp
            cuda/fill.cu)
    target_link_libraries(wave3d_cuda PUBLIC wave3d_core CUDA::cudart)
    set_target_properties(
        wave3d_cuda
        PROPERTIES
            CUDA_STANDARD 17
            CUDA_STANDARD_REQUIRED YES
            CUDA_EXTENSIONS NO)

    add_executable(wave3d_cuda_info apps/cuda_info.cpp)
    target_link_libraries(wave3d_cuda_info PRIVATE wave3d_cuda)

    if(WAVE3D_BUILD_TESTS)
        add_executable(wave3d_cuda_tests tests/test_cuda.cpp)
        target_link_libraries(wave3d_cuda_tests PRIVATE wave3d_cuda)
        add_test(NAME wave3d_cuda_tests COMMAND wave3d_cuda_tests)
        set_tests_properties(wave3d_cuda_tests PROPERTIES LABELS "cuda")
    endif()
endif()
