#include "wave3d/cuda/sponge.hpp"

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

__global__ void apply_stress_sponge_kernel(
    float* sxx,
    float* syy,
    float* szz,
    float* sxy,
    float* sxz,
    float* syz,
    const float* damping,
    std::size_t cell_count) {
    const auto start = static_cast<std::size_t>(blockIdx.x) * blockDim.x +
                       threadIdx.x;
    const auto stride = static_cast<std::size_t>(blockDim.x) * gridDim.x;
    for (auto index = start; index < cell_count; index += stride) {
        const float factor = damping[index];
        sxx[index] *= factor;
        syy[index] *= factor;
        szz[index] *= factor;
        sxy[index] *= factor;
        sxz[index] *= factor;
        syz[index] *= factor;
    }
}

__global__ void apply_velocity_sponge_kernel(
    float* vx,
    float* vy,
    float* vz,
    const float* damping,
    std::size_t cell_count) {
    const auto start = static_cast<std::size_t>(blockIdx.x) * blockDim.x +
                       threadIdx.x;
    const auto stride = static_cast<std::size_t>(blockDim.x) * gridDim.x;
    for (auto index = start; index < cell_count; index += stride) {
        const float factor = damping[index];
        vx[index] *= factor;
        vy[index] *= factor;
        vz[index] *= factor;
    }
}

void require_matching_sponge_grid(
    const DeviceElasticWavefield& wavefield,
    const DeviceSpongeProfile& profile) {
    if (!same_grid_geometry(wavefield.grid(), profile.grid())) {
        throw std::invalid_argument(
            "CUDA sponge profile and wavefield grids must match");
    }
}

} // namespace

void apply_sponge_to_stresses(
    DeviceElasticWavefield& wavefield,
    const DeviceSpongeProfile& profile) {
    require_matching_sponge_grid(wavefield, profile);
    apply_stress_sponge_kernel<<<
        launch_block_count(wavefield.cell_count()),
        threads_per_block>>>(
        wavefield.sxx_pa_.get(),
        wavefield.syy_pa_.get(),
        wavefield.szz_pa_.get(),
        wavefield.sxy_pa_.get(),
        wavefield.sxz_pa_.get(),
        wavefield.syz_pa_.get(),
        profile.damping_.get(),
        wavefield.cell_count());
    check_last_launch("apply_stress_sponge_kernel launch");
}

void apply_sponge_to_velocities(
    DeviceElasticWavefield& wavefield,
    const DeviceSpongeProfile& profile) {
    require_matching_sponge_grid(wavefield, profile);
    apply_velocity_sponge_kernel<<<
        launch_block_count(wavefield.cell_count()),
        threads_per_block>>>(
        wavefield.vx_m_s_.get(),
        wavefield.vy_m_s_.get(),
        wavefield.vz_m_s_.get(),
        profile.damping_.get(),
        wavefield.cell_count());
    check_last_launch("apply_velocity_sponge_kernel launch");
}

void advance_elastic_sponge_step(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceMomentTensorSource& source,
    const DeviceReceiverSet& receivers,
    DeviceReceiverTraces& traces,
    const DeviceSpongeProfile& profile,
    std::size_t step_index_n) {
    if (step_index_n >= traces.sample_count()) {
        throw std::out_of_range(
            "CUDA elastic sponge step is outside trace storage");
    }
    if (!same_grid_geometry(wavefield.grid(), coefficients.grid()) ||
        !same_grid_geometry(wavefield.grid(), source.grid()) ||
        !same_grid_geometry(wavefield.grid(), receivers.grid()) ||
        !same_grid_geometry(wavefield.grid(), profile.grid())) {
        throw std::invalid_argument(
            "CUDA elastic sponge step inputs must use one exact grid");
    }
    if (receivers.receiver_count() != traces.receiver_count()) {
        throw std::invalid_argument(
            "CUDA elastic sponge receiver and trace counts must match");
    }

    update_elastic_stresses(wavefield, coefficients, traces.dt_s());
    inject_moment_tensor_source(
        wavefield, source, step_index_n, traces.dt_s());
    apply_sponge_to_stresses(wavefield, profile);
    update_elastic_velocities(wavefield, coefficients, traces.dt_s());
    apply_sponge_to_velocities(wavefield, profile);
    sample_receivers_after_velocity_step(
        wavefield, receivers, traces, step_index_n);
}

} // namespace wave3d::cuda
