#include "wave3d/cuda/free_surface.hpp"

#include "wave3d/cuda/cuda_error.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace wave3d::cuda {
namespace {

constexpr unsigned int threads_per_block = 256;
constexpr std::size_t maximum_block_count = 65535;

[[nodiscard]] unsigned int launch_block_count(std::size_t work_items) {
    const auto required = (work_items - 1) / threads_per_block + 1;
    return static_cast<unsigned int>(std::min(required, maximum_block_count));
}

__global__ void free_surface_stress_kernel(
    std::size_t plane_cells,
    std::size_t surface_z,
    std::size_t ghost_depth,
    float* sxx,
    float* syy,
    float* szz,
    float* sxy,
    float* sxz,
    float* syz) {
    const auto start = static_cast<std::size_t>(blockIdx.x) * blockDim.x +
                       threadIdx.x;
    const auto stride = static_cast<std::size_t>(blockDim.x) * gridDim.x;
    for (auto plane_index = start; plane_index < plane_cells;
         plane_index += stride) {
        const auto surface_index = plane_index + plane_cells * surface_z;
        szz[surface_index] = 0.0F;
        for (std::size_t depth = 1; depth <= ghost_depth; ++depth) {
            const auto upper = plane_index + plane_cells * (surface_z - depth);
            const auto lower = plane_index + plane_cells * (surface_z + depth);
            szz[upper] = -szz[lower];
            sxx[upper] = sxx[lower];
            syy[upper] = syy[lower];
            sxy[upper] = sxy[lower];
        }
        for (std::size_t depth = 0; depth < ghost_depth; ++depth) {
            const auto upper =
                plane_index + plane_cells * (surface_z - 1 - depth);
            const auto lower = plane_index + plane_cells * (surface_z + depth);
            sxz[upper] = -sxz[lower];
            syz[upper] = -syz[lower];
        }
    }
}

__global__ void free_surface_velocity_kernel(
    std::size_t plane_cells,
    std::size_t surface_z,
    std::size_t ghost_depth,
    float* vx,
    float* vy,
    float* vz) {
    const auto start = static_cast<std::size_t>(blockIdx.x) * blockDim.x +
                       threadIdx.x;
    const auto stride = static_cast<std::size_t>(blockDim.x) * gridDim.x;
    for (auto plane_index = start; plane_index < plane_cells;
         plane_index += stride) {
        for (std::size_t depth = 1; depth <= ghost_depth; ++depth) {
            const auto upper = plane_index + plane_cells * (surface_z - depth);
            const auto lower = plane_index + plane_cells * (surface_z + depth);
            vx[upper] = vx[lower];
            vy[upper] = vy[lower];
        }
        for (std::size_t depth = 0; depth < ghost_depth; ++depth) {
            const auto upper =
                plane_index + plane_cells * (surface_z - 1 - depth);
            const auto lower = plane_index + plane_cells * (surface_z + depth);
            vz[upper] = vz[lower];
        }
    }
}

void require_matching_surface(
    const DeviceElasticWavefield& wavefield,
    const DeviceTractionFreeSurface& surface) {
    if (!same_grid_geometry(wavefield.grid(), surface.grid()) ||
        surface.surface_storage_z() != wavefield.grid().physical_origin_z() ||
        surface.ghost_depth() != 6) {
        throw std::invalid_argument(
            "CUDA traction-free surface does not match wavefield");
    }
}

} // namespace

void apply_traction_free_stresses(
    DeviceElasticWavefield& wavefield,
    const DeviceTractionFreeSurface& surface) {
    require_matching_surface(wavefield, surface);
    const auto plane_cells =
        wavefield.grid().allocated_nx() * wavefield.grid().allocated_ny();
    free_surface_stress_kernel<<<
        launch_block_count(plane_cells),
        threads_per_block>>>(
        plane_cells,
        surface.surface_storage_z(),
        surface.ghost_depth(),
        wavefield.sxx_pa_.get(), wavefield.syy_pa_.get(),
        wavefield.szz_pa_.get(), wavefield.sxy_pa_.get(),
        wavefield.sxz_pa_.get(), wavefield.syz_pa_.get());
    check_last_launch("free_surface_stress_kernel launch");
}

void apply_traction_free_velocities(
    DeviceElasticWavefield& wavefield,
    const DeviceTractionFreeSurface& surface) {
    require_matching_surface(wavefield, surface);
    const auto plane_cells =
        wavefield.grid().allocated_nx() * wavefield.grid().allocated_ny();
    free_surface_velocity_kernel<<<
        launch_block_count(plane_cells),
        threads_per_block>>>(
        plane_cells,
        surface.surface_storage_z(),
        surface.ghost_depth(),
        wavefield.vx_m_s_.get(), wavefield.vy_m_s_.get(),
        wavefield.vz_m_s_.get());
    check_last_launch("free_surface_velocity_kernel launch");
}

void advance_elastic_free_surface_step(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceMomentTensorSource& source,
    const DeviceReceiverSet& receivers,
    DeviceReceiverTraces& traces,
    const DeviceCpmlProfile& profile,
    DeviceCpmlState& state,
    const DeviceTractionFreeSurface& surface,
    std::size_t step_index_n) {
    if (profile.sides().z_min) {
        throw std::invalid_argument(
            "free-surface composition requires top CPML disabled");
    }
    if (!same_grid_geometry(wavefield.grid(), surface.grid()) ||
        !same_grid_geometry(wavefield.grid(), coefficients.grid()) ||
        !same_grid_geometry(wavefield.grid(), source.grid()) ||
        !same_grid_geometry(wavefield.grid(), receivers.grid()) ||
        !same_grid_geometry(wavefield.grid(), profile.grid()) ||
        !same_grid_geometry(wavefield.grid(), state.grid())) {
        throw std::invalid_argument(
            "free-surface composition inputs must share one grid");
    }
    if (step_index_n >= traces.sample_count()) {
        throw std::out_of_range(
            "CUDA free-surface step is outside trace storage");
    }
    if (receivers.receiver_count() != traces.receiver_count() ||
        traces.dt_s() != profile.dt_s()) {
        throw std::invalid_argument(
            "free-surface receiver traces do not match acquisition/profile");
    }

    update_elastic_stresses_cpml(wavefield, coefficients, profile, state);
    inject_moment_tensor_source(
        wavefield, source, step_index_n, profile.dt_s());
    apply_traction_free_stresses(wavefield, surface);
    update_elastic_velocities_cpml(wavefield, coefficients, profile, state);
    apply_traction_free_velocities(wavefield, surface);
    sample_receivers_after_velocity_step(
        wavefield, receivers, traces, step_index_n);
}

} // namespace wave3d::cuda
