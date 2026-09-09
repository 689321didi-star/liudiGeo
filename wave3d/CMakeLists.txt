cmake_minimum_required(VERSION 3.24)

project(wave3d VERSION 0.1.0 LANGUAGES CXX)

option(WAVE3D_BUILD_TESTS "Build Wave3D tests" ON)
option(WAVE3D_ENABLE_CUDA "Build the Wave3D CUDA foundation" OFF)
option(WAVE3D_ENABLE_YAML "Build YAML run-configuration adapters" OFF)
option(WAVE3D_ENABLE_HDF5 "Build HDF5 scientific-data adapters" OFF)
option(WAVE3D_ENABLE_SEGY "Build SEG-Y trace adapters" OFF)

add_library(wave3d_core INTERFACE)
target_include_directories(wave3d_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(wave3d_core INTERFACE cxx_std_17)

target_compile_options(
    wave3d_core
    INTERFACE
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4;/permissive->
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>
        $<$<COMPILE_LANG_AND_ID:CUDA,NVIDIA>:-Xcompiler=-Wall,-Wextra>)

if(WAVE3D_ENABLE_YAML)
    find_package(yaml-cpp CONFIG REQUIRED)
    add_library(wave3d_io_yaml STATIC src/io/yaml_config.cpp)
    target_link_libraries(wave3d_io_yaml PUBLIC wave3d_core yaml-cpp::yaml-cpp)
endif()

if(WAVE3D_ENABLE_HDF5)
    find_package(HDF5 REQUIRED COMPONENTS CXX)
    add_library(wave3d_io_hdf5 STATIC src/io/hdf5.cpp)
    target_link_libraries(wave3d_io_hdf5 PUBLIC wave3d_core HDF5::HDF5)
endif()

if(WAVE3D_ENABLE_SEGY)
    add_library(wave3d_io_segy STATIC src/io/segy.cpp)
    target_link_libraries(wave3d_io_segy PUBLIC wave3d_core)
endif()

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

    add_executable(
        wave3d_numerical_foundation_tests
        tests/test_numerical_foundation.cpp)
    target_link_libraries(wave3d_numerical_foundation_tests PRIVATE wave3d_core)
    add_test(
        NAME wave3d_numerical_foundation_tests
        COMMAND wave3d_numerical_foundation_tests)

    add_executable(
        wave3d_elastic_coefficient_tests
        tests/test_elastic_coefficients.cpp)
    target_link_libraries(wave3d_elastic_coefficient_tests PRIVATE wave3d_core)
    add_test(
        NAME wave3d_elastic_coefficient_tests
        COMMAND wave3d_elastic_coefficient_tests)

    add_executable(
        wave3d_cpu_staggered_derivative_tests
        tests/test_cpu_staggered_derivative.cpp)
    target_link_libraries(
        wave3d_cpu_staggered_derivative_tests PRIVATE wave3d_core)
    add_test(
        NAME wave3d_cpu_staggered_derivative_tests
        COMMAND wave3d_cpu_staggered_derivative_tests)

    add_executable(
        wave3d_cpu_elastic_update_tests
        tests/test_cpu_elastic_update.cpp)
    target_link_libraries(wave3d_cpu_elastic_update_tests PRIVATE wave3d_core)
    add_test(
        NAME wave3d_cpu_elastic_update_tests
        COMMAND wave3d_cpu_elastic_update_tests)

    add_executable(
        wave3d_staggered_acquisition_tests
        tests/test_staggered_acquisition.cpp)
    target_link_libraries(
        wave3d_staggered_acquisition_tests PRIVATE wave3d_core)
    add_test(
        NAME wave3d_staggered_acquisition_tests
        COMMAND wave3d_staggered_acquisition_tests)

    add_executable(
        wave3d_cpu_elastic_physics_tests
        tests/test_cpu_elastic_physics.cpp)
    target_link_libraries(
        wave3d_cpu_elastic_physics_tests PRIVATE wave3d_core)
    add_test(
        NAME wave3d_cpu_elastic_physics_tests
        COMMAND wave3d_cpu_elastic_physics_tests)
    set_tests_properties(
        wave3d_cpu_elastic_physics_tests PROPERTIES LABELS "physics")

    add_executable(wave3d_sponge_tests tests/test_sponge.cpp)
    target_link_libraries(wave3d_sponge_tests PRIVATE wave3d_core)
    add_test(NAME wave3d_sponge_tests COMMAND wave3d_sponge_tests)
    set_tests_properties(wave3d_sponge_tests PROPERTIES LABELS "boundary")

    add_executable(wave3d_cpml_tests tests/test_cpml.cpp)
    target_link_libraries(wave3d_cpml_tests PRIVATE wave3d_core)
    add_test(NAME wave3d_cpml_tests COMMAND wave3d_cpml_tests)
    set_tests_properties(wave3d_cpml_tests PROPERTIES LABELS "boundary")

    add_executable(
        wave3d_free_surface_tests
        tests/test_free_surface.cpp)
    target_link_libraries(wave3d_free_surface_tests PRIVATE wave3d_core)
    add_test(
        NAME wave3d_free_surface_tests
        COMMAND wave3d_free_surface_tests)
    set_tests_properties(
        wave3d_free_surface_tests PROPERTIES LABELS "boundary;physics")

    add_executable(wave3d_io_core_tests tests/test_io_core.cpp)
    target_link_libraries(wave3d_io_core_tests PRIVATE wave3d_core)
    add_test(NAME wave3d_io_core_tests COMMAND wave3d_io_core_tests)
    set_tests_properties(wave3d_io_core_tests PROPERTIES LABELS "io")

    if(WAVE3D_ENABLE_YAML)
        add_executable(wave3d_yaml_io_tests tests/test_yaml_io.cpp)
        target_link_libraries(wave3d_yaml_io_tests PRIVATE wave3d_io_yaml)
        add_test(NAME wave3d_yaml_io_tests COMMAND wave3d_yaml_io_tests)
        set_tests_properties(wave3d_yaml_io_tests PROPERTIES LABELS "io")
    endif()

    if(WAVE3D_ENABLE_HDF5)
        add_executable(wave3d_hdf5_io_tests tests/test_hdf5_io.cpp)
        target_link_libraries(wave3d_hdf5_io_tests PRIVATE wave3d_io_hdf5)
        add_test(NAME wave3d_hdf5_io_tests COMMAND wave3d_hdf5_io_tests)
        set_tests_properties(wave3d_hdf5_io_tests PROPERTIES LABELS "io")
    endif()

    if(WAVE3D_ENABLE_SEGY)
        add_executable(wave3d_segy_io_tests tests/test_segy_io.cpp)
        target_link_libraries(wave3d_segy_io_tests PRIVATE wave3d_io_segy)
        add_test(NAME wave3d_segy_io_tests COMMAND wave3d_segy_io_tests)
        set_tests_properties(wave3d_segy_io_tests PROPERTIES LABELS "io")
    endif()
endif()

if(WAVE3D_ENABLE_HDF5 AND WAVE3D_ENABLE_SEGY)
    add_executable(
        wave3d_segy_model_to_hdf5
        apps/segy_model_to_hdf5.cpp)
    target_link_libraries(
        wave3d_segy_model_to_hdf5
        PRIVATE wave3d_io_segy wave3d_io_hdf5)
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
            cuda/fill.cu
            cuda/elastic_propagator.cu
            cuda/sponge.cu
            cuda/cpml.cu
            cuda/free_surface.cu)
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

        add_executable(
            wave3d_cuda_elastic_tests
            tests/test_cuda_elastic.cpp)
        target_link_libraries(
            wave3d_cuda_elastic_tests PRIVATE wave3d_cuda)
        add_test(
            NAME wave3d_cuda_elastic_tests
            COMMAND wave3d_cuda_elastic_tests)
        set_tests_properties(
            wave3d_cuda_elastic_tests PROPERTIES LABELS "cuda;physics")

        add_executable(
            wave3d_cuda_sponge_tests
            tests/test_cuda_sponge.cpp)
        target_link_libraries(
            wave3d_cuda_sponge_tests PRIVATE wave3d_cuda)
        add_test(
            NAME wave3d_cuda_sponge_tests
            COMMAND wave3d_cuda_sponge_tests)
        set_tests_properties(
            wave3d_cuda_sponge_tests PROPERTIES LABELS "boundary;cuda;physics")

        add_executable(
            wave3d_cuda_cpml_tests
            tests/test_cuda_cpml.cpp)
        target_link_libraries(
            wave3d_cuda_cpml_tests PRIVATE wave3d_cuda)
        add_test(
            NAME wave3d_cuda_cpml_tests
            COMMAND wave3d_cuda_cpml_tests)
        set_tests_properties(
            wave3d_cuda_cpml_tests PROPERTIES LABELS "boundary;cuda;physics")

        add_executable(
            wave3d_cuda_free_surface_tests
            tests/test_cuda_free_surface.cpp)
        target_link_libraries(
            wave3d_cuda_free_surface_tests PRIVATE wave3d_cuda)
        add_test(
            NAME wave3d_cuda_free_surface_tests
            COMMAND wave3d_cuda_free_surface_tests)
        set_tests_properties(
            wave3d_cuda_free_surface_tests
            PROPERTIES LABELS "boundary;cuda;physics")
    endif()
endif()
