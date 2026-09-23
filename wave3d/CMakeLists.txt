cmake_minimum_required(VERSION 3.24)

project(wave3d VERSION 0.1.0 LANGUAGES CXX)

option(WAVE3D_BUILD_TESTS "Build Wave3D tests" ON)
option(WAVE3D_BUILD_DESKTOP "Build the Wave3D Qt desktop application" OFF)
option(
    WAVE3D_BUILD_QUALIFICATION_TOOLS
    "Build one-off CUDA device and qualification executables"
    OFF)
option(WAVE3D_ENABLE_CUDA "Build the Wave3D CUDA foundation" OFF)
option(WAVE3D_ENABLE_YAML "Build YAML run-configuration adapters" OFF)
option(WAVE3D_ENABLE_HDF5 "Build HDF5 scientific-data adapters" OFF)
option(WAVE3D_ENABLE_SEGY "Build SEG-Y trace adapters" OFF)
option(WAVE3D_ENABLE_RTM "Build removable RTM-ready interfaces only" OFF)
option(
    WAVE3D_ENABLE_OVERTHRUST
    "Build the optional SEG/EAGE Overthrust MAT converter"
    OFF)

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

if(WAVE3D_ENABLE_OVERTHRUST)
    if(NOT WAVE3D_ENABLE_HDF5 OR NOT WAVE3D_ENABLE_YAML)
        message(FATAL_ERROR
            "WAVE3D_ENABLE_OVERTHRUST requires HDF5 and YAML adapters")
    endif()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MATIO REQUIRED IMPORTED_TARGET matio)
    add_library(
        wave3d_io_overthrust_mat STATIC src/io/overthrust_mat.cpp)
    target_link_libraries(
        wave3d_io_overthrust_mat PUBLIC wave3d_core PkgConfig::MATIO)

    add_executable(
        wave3d_prepare_overthrust apps/prepare_overthrust.cpp)
    target_link_libraries(
        wave3d_prepare_overthrust
        PRIVATE
            wave3d_io_overthrust_mat
            wave3d_io_hdf5
            wave3d_io_yaml)
endif()

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

    add_executable(
        wave3d_derived_overthrust_tests
        tests/test_derived_overthrust.cpp)
    target_link_libraries(
        wave3d_derived_overthrust_tests PRIVATE wave3d_core)
    add_test(
        NAME wave3d_derived_overthrust_tests
        COMMAND wave3d_derived_overthrust_tests)
    set_tests_properties(
        wave3d_derived_overthrust_tests PROPERTIES LABELS "model")

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

    add_executable(
        wave3d_forward_interface_tests
        tests/test_forward_interfaces.cpp)
    target_link_libraries(
        wave3d_forward_interface_tests PRIVATE wave3d_core)
    add_test(
        NAME wave3d_forward_interface_tests
        COMMAND wave3d_forward_interface_tests)
    set_tests_properties(
        wave3d_forward_interface_tests PROPERTIES LABELS "interfaces")

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

    if(WAVE3D_ENABLE_OVERTHRUST)
        add_executable(
            wave3d_overthrust_mat_tests tests/test_overthrust_mat.cpp)
        target_link_libraries(
            wave3d_overthrust_mat_tests PRIVATE wave3d_io_overthrust_mat)
        add_test(
            NAME wave3d_overthrust_mat_tests
            COMMAND wave3d_overthrust_mat_tests)
        set_tests_properties(
            wave3d_overthrust_mat_tests PROPERTIES LABELS "io;model")
    endif()
endif()

if(WAVE3D_ENABLE_RTM)
    add_library(wave3d_rtm_interfaces INTERFACE)
    target_include_directories(
        wave3d_rtm_interfaces
        INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/optional/rtm/include")
    target_link_libraries(wave3d_rtm_interfaces INTERFACE wave3d_core)

    if(WAVE3D_BUILD_TESTS)
        add_executable(
            wave3d_checkpoint_interface_tests
            optional/rtm/tests/test_checkpoint_interfaces.cpp)
        target_link_libraries(
            wave3d_checkpoint_interface_tests
            PRIVATE wave3d_rtm_interfaces)
        add_test(
            NAME wave3d_checkpoint_interface_tests
            COMMAND wave3d_checkpoint_interface_tests)
        set_tests_properties(
            wave3d_checkpoint_interface_tests
            PROPERTIES LABELS "interfaces;rtm")

        add_executable(
            wave3d_rtm_task_interface_tests
            optional/rtm/tests/test_rtm_task_interfaces.cpp)
        target_link_libraries(
            wave3d_rtm_task_interface_tests PRIVATE wave3d_rtm_interfaces)
        add_test(
            NAME wave3d_rtm_task_interface_tests
            COMMAND wave3d_rtm_task_interface_tests)
        set_tests_properties(
            wave3d_rtm_task_interface_tests
            PROPERTIES LABELS "interfaces;rtm")
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
            src/cuda/forward_session.cpp
            cuda/fill.cu
            cuda/elastic_propagator.cu
            cuda/sponge.cu
            cuda/cpml.cu
            cuda/free_surface.cu
            cuda/visualization.cu)
    target_link_libraries(wave3d_cuda PUBLIC wave3d_core CUDA::cudart)
    set_target_properties(
        wave3d_cuda
        PROPERTIES
            CUDA_STANDARD 17
            CUDA_STANDARD_REQUIRED YES
            CUDA_EXTENSIONS NO)

    if(WAVE3D_BUILD_QUALIFICATION_TOOLS)
        add_executable(wave3d_cuda_info apps/cuda_info.cpp)
        target_link_libraries(wave3d_cuda_info PRIVATE wave3d_cuda)

        add_executable(wave3d_qualify_rtx5060 apps/qualify_rtx5060.cpp)
        target_link_libraries(wave3d_qualify_rtx5060 PRIVATE wave3d_cuda)

        add_executable(
            wave3d_qualify_visualization
            apps/qualify_visualization.cpp)
        target_link_libraries(
            wave3d_qualify_visualization PRIVATE wave3d_cuda)
        if(WAVE3D_ENABLE_HDF5)
            target_link_libraries(
                wave3d_qualify_rtx5060 PRIVATE wave3d_io_hdf5)
            target_compile_definitions(
                wave3d_qualify_rtx5060 PRIVATE WAVE3D_QUALIFY_HDF5=1)
        endif()
        if(WAVE3D_ENABLE_SEGY)
            target_link_libraries(
                wave3d_qualify_rtx5060 PRIVATE wave3d_io_segy)
            target_compile_definitions(
                wave3d_qualify_rtx5060 PRIVATE WAVE3D_QUALIFY_SEGY=1)
        endif()
    endif()

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

        add_executable(
            wave3d_cuda_forward_session_tests
            tests/test_cuda_forward_session.cpp)
        target_link_libraries(
            wave3d_cuda_forward_session_tests PRIVATE wave3d_cuda)
        add_test(
            NAME wave3d_cuda_forward_session_tests
            COMMAND wave3d_cuda_forward_session_tests)
        set_tests_properties(
            wave3d_cuda_forward_session_tests
            PROPERTIES LABELS "cuda;interfaces;physics")

        add_executable(
            wave3d_cuda_visualization_tests
            tests/test_cuda_visualization.cpp)
        target_link_libraries(
            wave3d_cuda_visualization_tests PRIVATE wave3d_cuda)
        add_test(
            NAME wave3d_cuda_visualization_tests
            COMMAND wave3d_cuda_visualization_tests)
        set_tests_properties(
            wave3d_cuda_visualization_tests
            PROPERTIES LABELS "cuda;interfaces;visualization")
    endif()
endif()

if(WAVE3D_ENABLE_CUDA AND WAVE3D_ENABLE_YAML AND WAVE3D_ENABLE_HDF5 AND
   WAVE3D_ENABLE_SEGY)
    add_library(
        wave3d_forward_run
        STATIC
            src/task/cuda_forward_run.cpp)
    target_link_libraries(
        wave3d_forward_run
        PUBLIC
            wave3d_cuda
            wave3d_io_yaml
            wave3d_io_hdf5
            wave3d_io_segy)

    add_executable(wave3d_run apps/run_forward.cpp)
    target_link_libraries(wave3d_run PRIVATE wave3d_forward_run)

    if(WAVE3D_BUILD_TESTS)
        add_executable(
            wave3d_cuda_forward_run_tests
            tests/test_cuda_forward_run.cpp)
        target_link_libraries(
            wave3d_cuda_forward_run_tests PRIVATE wave3d_forward_run)
        add_test(
            NAME wave3d_cuda_forward_run_tests
            COMMAND wave3d_cuda_forward_run_tests)
        set_tests_properties(
            wave3d_cuda_forward_run_tests
            PROPERTIES LABELS "cuda;io;pipeline")
    endif()
endif()

if(WAVE3D_BUILD_DESKTOP)
    find_package(
        Qt6 6.8 REQUIRED
        COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

    add_library(
        wave3d_desktop_selection STATIC
        include/wave3d/desktop/selection_controller.hpp
        src/desktop/selection_controller.cpp)
    set_target_properties(wave3d_desktop_selection PROPERTIES AUTOMOC ON)
    target_include_directories(
        wave3d_desktop_selection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(wave3d_desktop_selection PUBLIC Qt6::Core)

    add_library(
        wave3d_desktop_project STATIC
        src/desktop/project_workspace.cpp)
    target_link_libraries(
        wave3d_desktop_project PUBLIC wave3d_core Qt6::Core)

    add_library(
        wave3d_desktop_experiment STATIC
        src/desktop/experiment_draft.cpp)
    target_link_libraries(
        wave3d_desktop_experiment PUBLIC wave3d_core Qt6::Core)

    if(WAVE3D_ENABLE_HDF5)
        add_library(
            wave3d_desktop_static_model STATIC
            src/desktop/model_derivation.cpp
            src/desktop/static_model_scene.cpp)
        target_link_libraries(
            wave3d_desktop_static_model
            PUBLIC wave3d_core wave3d_io_hdf5 Qt6::Core Qt6::Gui)
        if(WAVE3D_ENABLE_SEGY)
            target_sources(
                wave3d_desktop_static_model
                PRIVATE src/desktop/segy_model_conversion.cpp)
            target_link_libraries(
                wave3d_desktop_static_model PUBLIC wave3d_io_segy)
        endif()
    endif()

    add_library(
        wave3d_desktop_shell STATIC
        src/desktop/experiment_editor.cpp
        src/desktop/main_window.cpp
        src/desktop/main_window_shell.cpp
        src/desktop/project_navigator.cpp
        src/desktop/result_workspace.cpp
        src/desktop/theme.cpp
        src/desktop/volume_viewport.cpp)
    target_link_libraries(
        wave3d_desktop_shell
        PUBLIC
            wave3d_core
            wave3d_desktop_experiment
            wave3d_desktop_project
            wave3d_desktop_selection
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Qt6::OpenGL
            Qt6::OpenGLWidgets)
    if(WAVE3D_ENABLE_HDF5)
        target_link_libraries(
            wave3d_desktop_shell PUBLIC wave3d_desktop_static_model)
        target_compile_definitions(
            wave3d_desktop_shell PUBLIC WAVE3D_DESKTOP_HAS_HDF5=1)
    endif()
    if(WAVE3D_ENABLE_YAML)
        target_link_libraries(
            wave3d_desktop_shell PUBLIC wave3d_io_yaml)
        target_compile_definitions(
            wave3d_desktop_shell PUBLIC WAVE3D_DESKTOP_HAS_YAML=1)
    endif()
    if(WAVE3D_ENABLE_SEGY)
        target_link_libraries(
            wave3d_desktop_shell PUBLIC wave3d_io_segy)
        target_compile_definitions(
            wave3d_desktop_shell PUBLIC WAVE3D_DESKTOP_HAS_SEGY=1)
    endif()
    if(WAVE3D_ENABLE_CUDA AND WAVE3D_ENABLE_YAML AND WAVE3D_ENABLE_HDF5 AND
       WAVE3D_ENABLE_SEGY)
        target_sources(
            wave3d_desktop_shell
            PRIVATE
                src/desktop/forward_run_worker.cpp
                src/desktop/live_wavefield_view.cpp)
        target_link_libraries(
            wave3d_desktop_shell PUBLIC wave3d_forward_run)
        target_compile_definitions(
            wave3d_desktop_shell PUBLIC WAVE3D_DESKTOP_HAS_CUDA_FORWARD=1)

        add_executable(
            wave3d_live_display_benchmark
            apps/benchmark_live_display.cpp)
        target_link_libraries(
            wave3d_live_display_benchmark PRIVATE wave3d_desktop_shell)
    endif()

    add_executable(wave3d_studio apps/studio/main.cpp)
    target_link_libraries(wave3d_studio PRIVATE wave3d_desktop_shell)

    if(WAVE3D_BUILD_TESTS)
        find_package(Qt6 6.8 REQUIRED COMPONENTS Test)

        add_executable(
            wave3d_desktop_selection_tests
            tests/test_desktop_selection_controller.cpp)
        target_link_libraries(
            wave3d_desktop_selection_tests
            PRIVATE
                wave3d_desktop_selection
                wave3d_desktop_shell
                Qt6::Test)
        add_test(
            NAME wave3d_desktop_selection_tests
            COMMAND wave3d_desktop_selection_tests)
        set_tests_properties(
            wave3d_desktop_selection_tests
            PROPERTIES
                LABELS "desktop;interfaces"
                ENVIRONMENT "QT_QPA_PLATFORM=minimal")

        add_executable(
            wave3d_desktop_project_navigator_tests
            tests/test_desktop_project_navigator.cpp)
        target_link_libraries(
            wave3d_desktop_project_navigator_tests
            PRIVATE wave3d_desktop_shell Qt6::Test)
        add_test(
            NAME wave3d_desktop_project_navigator_tests
            COMMAND wave3d_desktop_project_navigator_tests)
        set_tests_properties(
            wave3d_desktop_project_navigator_tests
            PROPERTIES
                LABELS "desktop;interfaces"
                ENVIRONMENT "QT_QPA_PLATFORM=minimal")

        add_executable(
            wave3d_desktop_experiment_tests
            tests/test_desktop_experiment_draft.cpp)
        target_link_libraries(
            wave3d_desktop_experiment_tests PRIVATE wave3d_desktop_experiment)
        add_test(
            NAME wave3d_desktop_experiment_tests
            COMMAND wave3d_desktop_experiment_tests)
        set_tests_properties(
            wave3d_desktop_experiment_tests
            PROPERTIES LABELS "desktop;interfaces;numerics;source")

        if(WAVE3D_ENABLE_HDF5)
            add_executable(
                wave3d_desktop_static_model_tests
                tests/test_desktop_static_model.cpp)
            target_link_libraries(
                wave3d_desktop_static_model_tests
                PRIVATE wave3d_desktop_static_model)
            add_test(
                NAME wave3d_desktop_static_model_tests
                COMMAND wave3d_desktop_static_model_tests)
            set_tests_properties(
                wave3d_desktop_static_model_tests
                PROPERTIES LABELS "desktop;io;model;visualization")

            add_executable(
                wave3d_desktop_model_derivation_tests
                tests/test_desktop_model_derivation.cpp)
            target_link_libraries(
                wave3d_desktop_model_derivation_tests
                PRIVATE
                    wave3d_desktop_project
                    wave3d_desktop_static_model)
            add_test(
                NAME wave3d_desktop_model_derivation_tests
                COMMAND wave3d_desktop_model_derivation_tests)
            set_tests_properties(
                wave3d_desktop_model_derivation_tests
                PROPERTIES LABELS "desktop;io;model;provenance")

            if(WAVE3D_ENABLE_SEGY)
                add_executable(
                    wave3d_desktop_segy_model_conversion_tests
                    tests/test_desktop_segy_model_conversion.cpp)
                target_link_libraries(
                    wave3d_desktop_segy_model_conversion_tests
                    PRIVATE
                        wave3d_desktop_project
                        wave3d_desktop_static_model)
                add_test(
                    NAME wave3d_desktop_segy_model_conversion_tests
                    COMMAND wave3d_desktop_segy_model_conversion_tests)
                set_tests_properties(
                    wave3d_desktop_segy_model_conversion_tests
                    PROPERTIES LABELS "desktop;io;model;provenance")
            endif()
        endif()

        if(WAVE3D_ENABLE_SEGY)
            add_executable(
                wave3d_desktop_result_workspace_tests
                tests/test_desktop_result_workspace.cpp)
            target_link_libraries(
                wave3d_desktop_result_workspace_tests
                PRIVATE wave3d_desktop_shell wave3d_io_segy)
            add_test(
                NAME wave3d_desktop_result_workspace_tests
                COMMAND wave3d_desktop_result_workspace_tests)
            set_tests_properties(
                wave3d_desktop_result_workspace_tests
                PROPERTIES
                    LABELS "desktop;io;visualization"
                    ENVIRONMENT "QT_QPA_PLATFORM=minimal")
        endif()

        add_executable(
            wave3d_desktop_project_tests
            tests/test_desktop_project_workspace.cpp)
        target_link_libraries(
            wave3d_desktop_project_tests PRIVATE wave3d_desktop_project)
        add_test(
            NAME wave3d_desktop_project_tests
            COMMAND wave3d_desktop_project_tests)
        set_tests_properties(
            wave3d_desktop_project_tests
            PROPERTIES LABELS "desktop;io;interfaces")

        add_executable(
            wave3d_desktop_shell_tests
            tests/test_desktop_shell.cpp)
        target_link_libraries(
            wave3d_desktop_shell_tests PRIVATE wave3d_desktop_shell)
        add_test(
            NAME wave3d_desktop_shell_tests
            COMMAND wave3d_desktop_shell_tests)
        set_tests_properties(
            wave3d_desktop_shell_tests
            PROPERTIES
                LABELS "desktop;interfaces"
                ENVIRONMENT "QT_QPA_PLATFORM=minimal")

        if(WAVE3D_ENABLE_CUDA AND WAVE3D_ENABLE_YAML AND WAVE3D_ENABLE_HDF5 AND
           WAVE3D_ENABLE_SEGY)
            add_executable(
                wave3d_desktop_forward_worker_tests
                tests/test_desktop_forward_worker.cpp)
            target_link_libraries(
                wave3d_desktop_forward_worker_tests PRIVATE wave3d_desktop_shell)
            add_test(
                NAME wave3d_desktop_forward_worker_tests
                COMMAND wave3d_desktop_forward_worker_tests)
            set_tests_properties(
                wave3d_desktop_forward_worker_tests
                PROPERTIES LABELS "desktop;cuda;interfaces")

            add_executable(
                wave3d_desktop_live_wavefield_tests
                tests/test_desktop_live_wavefield.cpp)
            target_link_libraries(
                wave3d_desktop_live_wavefield_tests PRIVATE wave3d_desktop_shell)
            add_test(
                NAME wave3d_desktop_live_wavefield_tests
                COMMAND wave3d_desktop_live_wavefield_tests)
            set_tests_properties(
                wave3d_desktop_live_wavefield_tests
                PROPERTIES LABELS "desktop;cuda;visualization")
        endif()
    endif()
endif()
