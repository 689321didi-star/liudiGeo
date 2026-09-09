#include "wave3d/acquisition/moment_source_injector.hpp"
#include "wave3d/acquisition/receiver_sampler.hpp"
#include "wave3d/boundary/cpml.hpp"
#include "wave3d/boundary/free_surface.hpp"
#include "wave3d/core/forward_memory_plan.hpp"
#include "wave3d/core/simulation_config.hpp"
#include "wave3d/cuda/cpml.hpp"
#include "wave3d/cuda/cuda_error.hpp"
#include "wave3d/cuda/device_info.hpp"
#include "wave3d/cuda/free_surface.hpp"
#include "wave3d/io/data.hpp"
#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/model/physical_model.hpp"
#include "wave3d/numerics/elastic_validation.hpp"

#ifdef WAVE3D_QUALIFY_HDF5
#include "wave3d/io/hdf5.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

[[nodiscard]] double mebibytes(std::size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

[[nodiscard]] std::size_t parse_steps(const char* text) {
    std::size_t consumed = 0;
    const std::string value_text(text);
    const auto value = std::stoull(value_text, &consumed);
    if (consumed != value_text.size() || value == 0 || value > 4000) {
        throw std::invalid_argument("steps must be an integer in [1,4000]");
    }
    return static_cast<std::size_t>(value);
}

template <typename Container>
[[nodiscard]] bool all_finite(const Container& values) {
    return std::all_of(values.begin(), values.end(), [](float value) {
        return std::isfinite(value);
    });
}

[[nodiscard]] std::vector<wave3d::PhysicalPoint3D> receiver_geometry() {
    return {
        {500.0, 500.0, 0.0},
        {995.0, 500.0, 0.0},
        {1490.0, 500.0, 0.0},
        {500.0, 995.0, 0.0},
        {995.0, 995.0, 0.0},
        {1490.0, 995.0, 0.0},
        {500.0, 1490.0, 0.0},
        {995.0, 1490.0, 0.0},
        {1490.0, 1490.0, 0.0}};
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 3) {
        std::cerr << "usage: wave3d_qualify_rtx5060 [STEPS<=4000] "
                     "[TRACE_OUTPUT.h5]\n";
        return 2;
    }
    try {
        const std::size_t steps = argc >= 2 ? parse_steps(argv[1]) : 4000;
#ifndef WAVE3D_QUALIFY_HDF5
        if (argc == 3) {
            throw std::invalid_argument(
                "this build has no HDF5 output adapter");
        }
#endif
        const wave3d::Grid3D grid{
            200, 200, 200,
            10.0F, 10.0F, 10.0F,
            6,
            {20, 20}, {20, 20}, {0, 20}};
        constexpr double dt_s = 0.0005;
        wave3d::SimulationConfig simulation{};
        simulation.grid = grid;
        simulation.time = {dt_s, dt_s * static_cast<double>(steps)};
        simulation.material = {
            3200.0F, 3200.0F, 2200.0F, 2200.0F, 2500.0F, 2500.0F};
        simulation.top_boundary = wave3d::TopBoundary::FreeSurface;
        simulation.numerics = {0.85, 30.0};
        const auto validation_errors =
            wave3d::validate_staggered_elastic(simulation);
        if (!validation_errors.empty()) {
            throw std::invalid_argument(validation_errors.front());
        }

        const auto device_before = wave3d::cuda::query_device(0);
        const auto receiver_points = receiver_geometry();
        wave3d::ForwardMemoryPlanRequest request{};
        request.grid = grid;
        request.receiver_count = receiver_points.size();
        request.time_step_count = steps;
        request.available_device_bytes = device_before.free_memory_bytes;
        request.boundary_kind =
            wave3d::ForwardMemoryPlanRequest::BoundaryKind::Cpml;
        const auto plan = wave3d::make_elastic_forward_memory_plan(request);
        plan.require_fit();

        const auto setup_start = Clock::now();
        const auto coefficients = wave3d::prepare_elastic_coefficients(
            wave3d::make_homogeneous_model(
                grid, {3200.0F, 2200.0F, 2500.0F}));
        const auto source_metadata = wave3d::prepare_moment_tensor_source(
            grid,
            {995.0, 995.0, 600.0},
            0.0,
            wave3d::isotropic_explosion(1.0e12),
            {30.0, 0.04, 1.0});
        const auto source = wave3d::prepare_moment_tensor_source_stencils(
            grid, source_metadata);
        const auto receivers = wave3d::prepare_receiver_stencils(
            grid, wave3d::prepare_receiver_set(grid, receiver_points));
        const wave3d::CpmlParameters cpml_parameters{
            dt_s,
            3200.0,
            30.0,
            1.0e-3,
            2.0,
            1.0,
            {true, true, true, true, false, true}};
        const auto profile = wave3d::prepare_cpml_profile(
            grid, cpml_parameters);
        const auto surface = wave3d::prepare_traction_free_surface(grid);

        wave3d::cuda::DeviceElasticCoefficients device_coefficients(
            coefficients);
        wave3d::cuda::DeviceElasticWavefield wavefield(grid);
        wave3d::cuda::DeviceMomentTensorSource device_source(source);
        wave3d::cuda::DeviceReceiverSet device_receivers(receivers);
        wave3d::cuda::DeviceReceiverTraces traces(
            receiver_points.size(), steps, dt_s);
        wave3d::cuda::DeviceCpmlProfile device_profile(profile);
        wave3d::cuda::DeviceCpmlState state(grid);
        wave3d::cuda::DeviceTractionFreeSurface device_surface(surface);
        wave3d::cuda::synchronize();
        const auto setup_end = Clock::now();
        const auto device_allocated = wave3d::cuda::query_device(0);

        const std::size_t owned_device_bytes =
            device_coefficients.bytes() + wavefield.bytes() +
            device_source.bytes() + device_receivers.bytes() + traces.bytes() +
            device_profile.bytes() + state.bytes();
        const std::size_t observed_device_reduction =
            device_before.free_memory_bytes >= device_allocated.free_memory_bytes
                ? device_before.free_memory_bytes -
                      device_allocated.free_memory_bytes
                : 0;

        const auto run_start = Clock::now();
        const auto first_block_steps = std::min<std::size_t>(10, steps);
        for (std::size_t step = 0; step < first_block_steps; ++step) {
            wave3d::cuda::advance_elastic_free_surface_step(
                wavefield,
                device_coefficients,
                device_source,
                device_receivers,
                traces,
                device_profile,
                state,
                device_surface,
                step);
        }
        wave3d::cuda::synchronize();
        const auto first_block_end = Clock::now();
        for (std::size_t step = first_block_steps; step < steps; ++step) {
            wave3d::cuda::advance_elastic_free_surface_step(
                wavefield,
                device_coefficients,
                device_source,
                device_receivers,
                traces,
                device_profile,
                state,
                device_surface,
                step);
        }
        wave3d::cuda::synchronize();
        const auto run_end = Clock::now();
        const auto device_after_run = wave3d::cuda::query_device(0);

        const auto trace_values = receiver_points.size() * steps;
        std::vector<float> vx(trace_values);
        std::vector<float> vy(trace_values);
        std::vector<float> vz(trace_values);
        const auto trace_download_start = Clock::now();
        traces.download(vx, vy, vz);
        const auto trace_download_end = Clock::now();

        wave3d::ElasticWavefield host_wavefield(grid);
        wavefield.download(host_wavefield);
        wave3d::CpmlState host_state(grid);
        state.download(host_state);
        bool finite = all_finite(vx) && all_finite(vy) && all_finite(vz);
        for (const auto* field : {
                 &host_wavefield.vx_m_s,
                 &host_wavefield.vy_m_s,
                 &host_wavefield.vz_m_s,
                 &host_wavefield.sxx_pa,
                 &host_wavefield.syy_pa,
                 &host_wavefield.szz_pa,
                 &host_wavefield.sxy_pa,
                 &host_wavefield.sxz_pa,
                 &host_wavefield.syz_pa}) {
            finite = finite && all_finite(*field);
        }
        for (const auto& field : host_state.fields) {
            finite = finite && all_finite(field);
        }

        double maximum_surface_traction_pa = 0.0;
        const auto surface_z = grid.physical_origin_z();
        for (std::size_t y = 0; y < grid.allocated_ny(); ++y) {
            for (std::size_t x = 0; x < grid.allocated_nx(); ++x) {
                const auto surface_index = grid.linear_index(x, y, surface_z);
                const auto upper_half =
                    grid.linear_index(x, y, surface_z - 1);
                maximum_surface_traction_pa = std::max({
                    maximum_surface_traction_pa,
                    std::abs(static_cast<double>(
                        host_wavefield.szz_pa[surface_index])),
                    std::abs(0.5 * static_cast<double>(
                        host_wavefield.sxz_pa[upper_half] +
                        host_wavefield.sxz_pa[surface_index])),
                    std::abs(0.5 * static_cast<double>(
                        host_wavefield.syz_pa[upper_half] +
                        host_wavefield.syz_pa[surface_index]))});
            }
        }

        double hdf5_write_ms = 0.0;
        std::uintmax_t hdf5_bytes = 0;
#ifdef WAVE3D_QUALIFY_HDF5
        if (argc == 3) {
            const wave3d::io::ThreeComponentTraces host_traces{
                receiver_points.size(),
                steps,
                dt_s,
                receiver_points,
                source_metadata,
                vx,
                vy,
                vz};
            const auto output_start = Clock::now();
            wave3d::io::write_hdf5_traces(argv[2], host_traces);
            const auto output_end = Clock::now();
            hdf5_write_ms = milliseconds(output_end - output_start);
            hdf5_bytes = std::filesystem::file_size(argv[2]);
        }
#endif

        const double run_ms = milliseconds(run_end - run_start);
        const double first_block_ms =
            milliseconds(first_block_end - run_start);
        const double allocated_cell_steps =
            static_cast<double>(grid.allocated_cell_count()) *
            static_cast<double>(steps);
        std::cout << std::fixed << std::setprecision(6)
                  << "device_name=" << device_before.name << '\n'
                  << "compute_capability=" << device_before.compute_major
                  << '.' << device_before.compute_minor << '\n'
                  << "steps=" << steps << '\n'
                  << "allocated_cells=" << grid.allocated_cell_count() << '\n'
                  << "free_before_mib="
                  << mebibytes(device_before.free_memory_bytes) << '\n'
                  << "planned_allocations_mib="
                  << mebibytes(plan.allocation_bytes) << '\n'
                  << "planned_required_with_reserve_mib="
                  << mebibytes(plan.required_bytes) << '\n'
                  << "planned_budget_mib=" << mebibytes(plan.budget_bytes)
                  << '\n'
                  << "owned_device_mib=" << mebibytes(owned_device_bytes)
                  << '\n'
                  << "observed_device_reduction_mib="
                  << mebibytes(observed_device_reduction) << '\n'
                  << "free_after_run_mib="
                  << mebibytes(device_after_run.free_memory_bytes) << '\n'
                  << "setup_ms=" << milliseconds(setup_end - setup_start)
                  << '\n'
                  << "first_block_steps=" << first_block_steps << '\n'
                  << "first_block_ms=" << first_block_ms << '\n'
                  << "propagation_ms=" << run_ms << '\n'
                  << "ms_per_step=" << run_ms / static_cast<double>(steps)
                  << '\n'
                  << "allocated_mcell_steps_per_s="
                  << allocated_cell_steps / (run_ms * 1000.0) << '\n'
                  << "trace_download_ms="
                  << milliseconds(trace_download_end - trace_download_start)
                  << '\n'
                  << "hdf5_write_ms=" << hdf5_write_ms << '\n'
                  << "hdf5_bytes=" << hdf5_bytes << '\n'
                  << "max_surface_traction_pa="
                  << maximum_surface_traction_pa << '\n'
                  << "all_finite=" << (finite ? "true" : "false") << '\n';

        if (!finite) {
            throw std::runtime_error(
                "qualification found NaN or Inf in final state/traces");
        }
        if (maximum_surface_traction_pa != 0.0) {
            throw std::runtime_error(
                "qualification final surface traction is non-zero");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RTX 5060 qualification failed: " << error.what() << '\n';
        return 1;
    }
}
