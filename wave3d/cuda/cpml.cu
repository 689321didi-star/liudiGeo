#include "wave3d/cuda/cpml.hpp"

#include "wave3d/cuda/cuda_error.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace wave3d::cuda {
namespace {

constexpr unsigned int threads_per_block = 256;
constexpr std::size_t maximum_block_count = 65535;

__device__ __constant__ double derivative_coefficients[6] = {
    160083.0 / 131072.0,
    -12705.0 / 131072.0,
    22869.0 / 1310720.0,
    -5445.0 / 1835008.0,
    847.0 / 2359296.0,
    -63.0 / 2883584.0};

struct KernelGrid {
    std::size_t nx{0};
    std::size_t ny{0};
    std::size_t nz{0};
    std::size_t cell_count{0};
    double inverse_dx{0.0};
    double inverse_dy{0.0};
    double inverse_dz{0.0};
};

[[nodiscard]] KernelGrid make_kernel_grid(const Grid3D& grid) {
    require_valid_grid_geometry(grid);
    return {
        grid.allocated_nx(),
        grid.allocated_ny(),
        grid.allocated_nz(),
        grid.allocated_cell_count(),
        1.0 / static_cast<double>(grid.dx_m),
        1.0 / static_cast<double>(grid.dy_m),
        1.0 / static_cast<double>(grid.dz_m)};
}

[[nodiscard]] unsigned int launch_block_count(std::size_t work_items) {
    const auto required = (work_items - 1) / threads_per_block + 1;
    return static_cast<unsigned int>(std::min(required, maximum_block_count));
}

__device__ bool in_integer_to_half_range(
    std::size_t coordinate,
    std::size_t extent) {
    return coordinate >= 5 && coordinate < extent - 6;
}

__device__ bool in_half_to_integer_range(
    std::size_t coordinate,
    std::size_t extent) {
    return coordinate >= 6 && coordinate < extent - 5;
}

__device__ double derivative(
    const float* field,
    std::size_t target,
    std::size_t stride,
    double inverse_spacing,
    bool integer_to_half) {
    double value = 0.0;
#pragma unroll
    for (std::size_t coefficient = 0; coefficient < 6; ++coefficient) {
        std::size_t positive = 0;
        std::size_t negative = 0;
        if (integer_to_half) {
            positive = target + (coefficient + 1) * stride;
            negative = target - coefficient * stride;
        } else {
            positive = target + coefficient * stride;
            negative = target - (coefficient + 1) * stride;
        }
        value += derivative_coefficients[coefficient] *
                 (static_cast<double>(field[positive]) -
                  static_cast<double>(field[negative]));
    }
    return value * inverse_spacing;
}

__device__ double cpml_correct(
    double raw_derivative,
    float* memory,
    std::size_t cell_index,
    const DeviceCpmlAxisView& axis,
    std::size_t coordinate,
    bool target_is_half) {
    const float* a = target_is_half ? axis.a_half : axis.a_integer;
    const float* b = target_is_half ? axis.b_half : axis.b_integer;
    const float* inverse_kappa = target_is_half
                                     ? axis.inverse_kappa_half
                                     : axis.inverse_kappa_integer;
    const double updated = static_cast<double>(b[coordinate]) *
                               static_cast<double>(memory[cell_index]) +
                           static_cast<double>(a[coordinate]) * raw_derivative;
    memory[cell_index] = static_cast<float>(updated);
    return static_cast<double>(inverse_kappa[coordinate]) * raw_derivative +
           static_cast<double>(memory[cell_index]);
}

__global__ void update_stresses_cpml_kernel(
    KernelGrid grid,
    const float* vx,
    const float* vy,
    const float* vz,
    float* sxx,
    float* syy,
    float* szz,
    float* sxy,
    float* sxz,
    float* syz,
    const float* lambda,
    const float* mu,
    const float* mu_xy,
    const float* mu_xz,
    const float* mu_yz,
    DeviceCpmlAxisView cpml_x,
    DeviceCpmlAxisView cpml_y,
    DeviceCpmlAxisView cpml_z,
    float* memory_dvx_dx,
    float* memory_dvy_dy,
    float* memory_dvz_dz,
    float* memory_dvx_dy,
    float* memory_dvy_dx,
    float* memory_dvx_dz,
    float* memory_dvz_dx,
    float* memory_dvy_dz,
    float* memory_dvz_dy,
    double dt_s) {
    const auto start = static_cast<std::size_t>(blockIdx.x) * blockDim.x +
                       threadIdx.x;
    const auto step = static_cast<std::size_t>(blockDim.x) * gridDim.x;
    const auto xy_stride = grid.nx * grid.ny;
    for (auto index = start; index < grid.cell_count; index += step) {
        const auto x = index % grid.nx;
        const auto yz = index / grid.nx;
        const auto y = yz % grid.ny;
        const auto z = yz / grid.ny;
        const bool x_i2h = in_integer_to_half_range(x, grid.nx);
        const bool y_i2h = in_integer_to_half_range(y, grid.ny);
        const bool z_i2h = in_integer_to_half_range(z, grid.nz);
        const bool x_h2i = in_half_to_integer_range(x, grid.nx);
        const bool y_h2i = in_half_to_integer_range(y, grid.ny);
        const bool z_h2i = in_half_to_integer_range(z, grid.nz);

        if (x_h2i && y_h2i && z_h2i) {
            const double dvx_dx = cpml_correct(
                derivative(vx, index, 1, grid.inverse_dx, false),
                memory_dvx_dx, index, cpml_x, x, false);
            const double dvy_dy = cpml_correct(
                derivative(vy, index, grid.nx, grid.inverse_dy, false),
                memory_dvy_dy, index, cpml_y, y, false);
            const double dvz_dz = cpml_correct(
                derivative(vz, index, xy_stride, grid.inverse_dz, false),
                memory_dvz_dz, index, cpml_z, z, false);
            const double lambda_value = lambda[index];
            const double mu_value = mu[index];
            sxx[index] = static_cast<float>(
                static_cast<double>(sxx[index]) +
                dt_s * ((lambda_value + 2.0 * mu_value) * dvx_dx +
                        lambda_value * (dvy_dy + dvz_dz)));
            syy[index] = static_cast<float>(
                static_cast<double>(syy[index]) +
                dt_s * ((lambda_value + 2.0 * mu_value) * dvy_dy +
                        lambda_value * (dvx_dx + dvz_dz)));
            szz[index] = static_cast<float>(
                static_cast<double>(szz[index]) +
                dt_s * ((lambda_value + 2.0 * mu_value) * dvz_dz +
                        lambda_value * (dvx_dx + dvy_dy)));
        }
        if (x_i2h && y_i2h) {
            const double dvx_dy = cpml_correct(
                derivative(vx, index, grid.nx, grid.inverse_dy, true),
                memory_dvx_dy, index, cpml_y, y, true);
            const double dvy_dx = cpml_correct(
                derivative(vy, index, 1, grid.inverse_dx, true),
                memory_dvy_dx, index, cpml_x, x, true);
            sxy[index] = static_cast<float>(
                static_cast<double>(sxy[index]) + dt_s * mu_xy[index] *
                    (dvx_dy + dvy_dx));
        }
        if (x_i2h && z_i2h) {
            const double dvx_dz = cpml_correct(
                derivative(vx, index, xy_stride, grid.inverse_dz, true),
                memory_dvx_dz, index, cpml_z, z, true);
            const double dvz_dx = cpml_correct(
                derivative(vz, index, 1, grid.inverse_dx, true),
                memory_dvz_dx, index, cpml_x, x, true);
            sxz[index] = static_cast<float>(
                static_cast<double>(sxz[index]) + dt_s * mu_xz[index] *
                    (dvx_dz + dvz_dx));
        }
        if (y_i2h && z_i2h) {
            const double dvy_dz = cpml_correct(
                derivative(vy, index, xy_stride, grid.inverse_dz, true),
                memory_dvy_dz, index, cpml_z, z, true);
            const double dvz_dy = cpml_correct(
                derivative(vz, index, grid.nx, grid.inverse_dy, true),
                memory_dvz_dy, index, cpml_y, y, true);
            syz[index] = static_cast<float>(
                static_cast<double>(syz[index]) + dt_s * mu_yz[index] *
                    (dvy_dz + dvz_dy));
        }
    }
}

__global__ void update_velocities_cpml_kernel(
    KernelGrid grid,
    float* vx,
    float* vy,
    float* vz,
    const float* sxx,
    const float* syy,
    const float* szz,
    const float* sxy,
    const float* sxz,
    const float* syz,
    const float* buoyancy_x,
    const float* buoyancy_y,
    const float* buoyancy_z,
    DeviceCpmlAxisView cpml_x,
    DeviceCpmlAxisView cpml_y,
    DeviceCpmlAxisView cpml_z,
    float* memory_dsxx_dx,
    float* memory_dsxy_dy,
    float* memory_dsxz_dz,
    float* memory_dsxy_dx,
    float* memory_dsyy_dy,
    float* memory_dsyz_dz,
    float* memory_dsxz_dx,
    float* memory_dsyz_dy,
    float* memory_dszz_dz,
    double dt_s) {
    const auto start = static_cast<std::size_t>(blockIdx.x) * blockDim.x +
                       threadIdx.x;
    const auto step = static_cast<std::size_t>(blockDim.x) * gridDim.x;
    const auto xy_stride = grid.nx * grid.ny;
    for (auto index = start; index < grid.cell_count; index += step) {
        const auto x = index % grid.nx;
        const auto yz = index / grid.nx;
        const auto y = yz % grid.ny;
        const auto z = yz / grid.ny;
        const bool x_i2h = in_integer_to_half_range(x, grid.nx);
        const bool y_i2h = in_integer_to_half_range(y, grid.ny);
        const bool z_i2h = in_integer_to_half_range(z, grid.nz);
        const bool x_h2i = in_half_to_integer_range(x, grid.nx);
        const bool y_h2i = in_half_to_integer_range(y, grid.ny);
        const bool z_h2i = in_half_to_integer_range(z, grid.nz);

        if (x_i2h && y_h2i && z_h2i) {
            const double dsxx_dx = cpml_correct(
                derivative(sxx, index, 1, grid.inverse_dx, true),
                memory_dsxx_dx, index, cpml_x, x, true);
            const double dsxy_dy = cpml_correct(
                derivative(sxy, index, grid.nx, grid.inverse_dy, false),
                memory_dsxy_dy, index, cpml_y, y, false);
            const double dsxz_dz = cpml_correct(
                derivative(sxz, index, xy_stride, grid.inverse_dz, false),
                memory_dsxz_dz, index, cpml_z, z, false);
            vx[index] = static_cast<float>(
                static_cast<double>(vx[index]) + dt_s * buoyancy_x[index] *
                    (dsxx_dx + dsxy_dy + dsxz_dz));
        }
        if (x_h2i && y_i2h && z_h2i) {
            const double dsxy_dx = cpml_correct(
                derivative(sxy, index, 1, grid.inverse_dx, false),
                memory_dsxy_dx, index, cpml_x, x, false);
            const double dsyy_dy = cpml_correct(
                derivative(syy, index, grid.nx, grid.inverse_dy, true),
                memory_dsyy_dy, index, cpml_y, y, true);
            const double dsyz_dz = cpml_correct(
                derivative(syz, index, xy_stride, grid.inverse_dz, false),
                memory_dsyz_dz, index, cpml_z, z, false);
            vy[index] = static_cast<float>(
                static_cast<double>(vy[index]) + dt_s * buoyancy_y[index] *
                    (dsxy_dx + dsyy_dy + dsyz_dz));
        }
        if (x_h2i && y_h2i && z_i2h) {
            const double dsxz_dx = cpml_correct(
                derivative(sxz, index, 1, grid.inverse_dx, false),
                memory_dsxz_dx, index, cpml_x, x, false);
            const double dsyz_dy = cpml_correct(
                derivative(syz, index, grid.nx, grid.inverse_dy, false),
                memory_dsyz_dy, index, cpml_y, y, false);
            const double dszz_dz = cpml_correct(
                derivative(szz, index, xy_stride, grid.inverse_dz, true),
                memory_dszz_dz, index, cpml_z, z, true);
            vz[index] = static_cast<float>(
                static_cast<double>(vz[index]) + dt_s * buoyancy_z[index] *
                    (dsxz_dx + dsyz_dy + dszz_dz));
        }
    }
}

void require_cpml_device_inputs(
    const DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceCpmlProfile& profile,
    const DeviceCpmlState& state) {
    if (!same_grid_geometry(wavefield.grid(), coefficients.grid()) ||
        !same_grid_geometry(wavefield.grid(), profile.grid()) ||
        !same_grid_geometry(wavefield.grid(), state.grid())) {
        throw std::invalid_argument("CUDA CPML inputs must use one exact grid");
    }
    if (!std::isfinite(profile.dt_s()) || !(profile.dt_s() > 0.0)) {
        throw std::invalid_argument("CUDA CPML dt must be finite and positive");
    }
}

} // namespace

void update_elastic_stresses_cpml(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceCpmlProfile& profile,
    DeviceCpmlState& state) {
    require_cpml_device_inputs(wavefield, coefficients, profile, state);
    const auto grid = make_kernel_grid(wavefield.grid_);
    update_stresses_cpml_kernel<<<
        launch_block_count(grid.cell_count),
        threads_per_block>>>(
        grid,
        wavefield.vx_m_s_.get(), wavefield.vy_m_s_.get(),
        wavefield.vz_m_s_.get(), wavefield.sxx_pa_.get(),
        wavefield.syy_pa_.get(), wavefield.szz_pa_.get(),
        wavefield.sxy_pa_.get(), wavefield.sxz_pa_.get(),
        wavefield.syz_pa_.get(), coefficients.lambda_pa_.get(),
        coefficients.shear_modulus_pa_.get(),
        coefficients.shear_modulus_xy_pa_.get(),
        coefficients.shear_modulus_xz_pa_.get(),
        coefficients.shear_modulus_yz_pa_.get(), profile.axis(0),
        profile.axis(1), profile.axis(2),
        state.fields_[0].get(), state.fields_[1].get(),
        state.fields_[2].get(), state.fields_[3].get(),
        state.fields_[4].get(), state.fields_[5].get(),
        state.fields_[6].get(), state.fields_[7].get(),
        state.fields_[8].get(), profile.dt_s());
    check_last_launch("update_stresses_cpml_kernel launch");
}

void update_elastic_velocities_cpml(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceCpmlProfile& profile,
    DeviceCpmlState& state) {
    require_cpml_device_inputs(wavefield, coefficients, profile, state);
    const auto grid = make_kernel_grid(wavefield.grid_);
    update_velocities_cpml_kernel<<<
        launch_block_count(grid.cell_count),
        threads_per_block>>>(
        grid,
        wavefield.vx_m_s_.get(), wavefield.vy_m_s_.get(),
        wavefield.vz_m_s_.get(), wavefield.sxx_pa_.get(),
        wavefield.syy_pa_.get(), wavefield.szz_pa_.get(),
        wavefield.sxy_pa_.get(), wavefield.sxz_pa_.get(),
        wavefield.syz_pa_.get(), coefficients.buoyancy_x_m3_kg_.get(),
        coefficients.buoyancy_y_m3_kg_.get(),
        coefficients.buoyancy_z_m3_kg_.get(), profile.axis(0),
        profile.axis(1), profile.axis(2),
        state.fields_[9].get(), state.fields_[10].get(),
        state.fields_[11].get(), state.fields_[12].get(),
        state.fields_[13].get(), state.fields_[14].get(),
        state.fields_[15].get(), state.fields_[16].get(),
        state.fields_[17].get(), profile.dt_s());
    check_last_launch("update_velocities_cpml_kernel launch");
}

void advance_elastic_cpml_step(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceMomentTensorSource& source,
    const DeviceReceiverSet& receivers,
    DeviceReceiverTraces& traces,
    const DeviceCpmlProfile& profile,
    DeviceCpmlState& state,
    std::size_t step_index_n) {
    if (step_index_n >= traces.sample_count()) {
        throw std::out_of_range("CUDA CPML step is outside trace storage");
    }
    require_cpml_device_inputs(wavefield, coefficients, profile, state);
    if (!same_grid_geometry(wavefield.grid(), source.grid()) ||
        !same_grid_geometry(wavefield.grid(), receivers.grid()) ||
        receivers.receiver_count() != traces.receiver_count()) {
        throw std::invalid_argument(
            "CUDA CPML step source/receiver inputs do not match");
    }
    if (traces.dt_s() != profile.dt_s()) {
        throw std::invalid_argument("CUDA CPML trace dt does not match profile");
    }

    update_elastic_stresses_cpml(wavefield, coefficients, profile, state);
    inject_moment_tensor_source(
        wavefield, source, step_index_n, profile.dt_s());
    update_elastic_velocities_cpml(wavefield, coefficients, profile, state);
    sample_receivers_after_velocity_step(
        wavefield, receivers, traces, step_index_n);
}

} // namespace wave3d::cuda
