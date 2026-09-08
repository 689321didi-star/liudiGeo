#include "wave3d/cuda/elastic_propagator.hpp"

#include "wave3d/cuda/cuda_error.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
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

struct MomentValues {
    double values[6]{};
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
    if (work_items == 0) {
        return 0;
    }
    const auto required = (work_items - 1) / threads_per_block + 1;
    return static_cast<unsigned int>(
        std::min(required, maximum_block_count));
}

inline void require_matching_grid(
    const Grid3D& first,
    const Grid3D& second,
    const char* message) {
    if (!same_grid_geometry(first, second)) {
        throw std::invalid_argument(message);
    }
}

inline void require_time_step(double dt_s) {
    if (!std::isfinite(dt_s) || !(dt_s > 0.0)) {
        throw std::invalid_argument(
            "CUDA elastic time step must be finite and positive");
    }
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

__global__ void update_stresses_kernel(
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
            const double dvx_dx =
                derivative(vx, index, 1, grid.inverse_dx, false);
            const double dvy_dy =
                derivative(vy, index, grid.nx, grid.inverse_dy, false);
            const double dvz_dz =
                derivative(vz, index, xy_stride, grid.inverse_dz, false);
            const double lambda_value = static_cast<double>(lambda[index]);
            const double mu_value = static_cast<double>(mu[index]);
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
            const double dvx_dy =
                derivative(vx, index, grid.nx, grid.inverse_dy, true);
            const double dvy_dx =
                derivative(vy, index, 1, grid.inverse_dx, true);
            sxy[index] = static_cast<float>(
                static_cast<double>(sxy[index]) + dt_s *
                    static_cast<double>(mu_xy[index]) * (dvx_dy + dvy_dx));
        }
        if (x_i2h && z_i2h) {
            const double dvx_dz =
                derivative(vx, index, xy_stride, grid.inverse_dz, true);
            const double dvz_dx =
                derivative(vz, index, 1, grid.inverse_dx, true);
            sxz[index] = static_cast<float>(
                static_cast<double>(sxz[index]) + dt_s *
                    static_cast<double>(mu_xz[index]) * (dvx_dz + dvz_dx));
        }
        if (y_i2h && z_i2h) {
            const double dvy_dz =
                derivative(vy, index, xy_stride, grid.inverse_dz, true);
            const double dvz_dy =
                derivative(vz, index, grid.nx, grid.inverse_dy, true);
            syz[index] = static_cast<float>(
                static_cast<double>(syz[index]) + dt_s *
                    static_cast<double>(mu_yz[index]) * (dvy_dz + dvz_dy));
        }
    }
}

__global__ void inject_source_kernel(
    float* sxx,
    float* syy,
    float* szz,
    float* sxy,
    float* sxz,
    float* syz,
    const std::size_t* indices,
    const double* weights,
    MomentValues moment,
    double scale) {
    const auto flat = static_cast<std::size_t>(blockIdx.x) * blockDim.x +
                      threadIdx.x;
    if (flat >= 48) {
        return;
    }
    const auto component = flat / 8;
    const auto index = indices[flat];
    const double increment = scale * moment.values[component] * weights[flat];
    float* field = nullptr;
    switch (component) {
    case 0:
        field = sxx;
        break;
    case 1:
        field = syy;
        break;
    case 2:
        field = szz;
        break;
    case 3:
        field = sxy;
        break;
    case 4:
        field = sxz;
        break;
    default:
        field = syz;
        break;
    }
    field[index] = static_cast<float>(
        static_cast<double>(field[index]) + increment);
}

__global__ void update_velocities_kernel(
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
            const double dsxx_dx =
                derivative(sxx, index, 1, grid.inverse_dx, true);
            const double dsxy_dy =
                derivative(sxy, index, grid.nx, grid.inverse_dy, false);
            const double dsxz_dz =
                derivative(sxz, index, xy_stride, grid.inverse_dz, false);
            vx[index] = static_cast<float>(
                static_cast<double>(vx[index]) + dt_s *
                    static_cast<double>(buoyancy_x[index]) *
                    (dsxx_dx + dsxy_dy + dsxz_dz));
        }
        if (x_h2i && y_i2h && z_h2i) {
            const double dsxy_dx =
                derivative(sxy, index, 1, grid.inverse_dx, false);
            const double dsyy_dy =
                derivative(syy, index, grid.nx, grid.inverse_dy, true);
            const double dsyz_dz =
                derivative(syz, index, xy_stride, grid.inverse_dz, false);
            vy[index] = static_cast<float>(
                static_cast<double>(vy[index]) + dt_s *
                    static_cast<double>(buoyancy_y[index]) *
                    (dsxy_dx + dsyy_dy + dsyz_dz));
        }
        if (x_h2i && y_h2i && z_i2h) {
            const double dsxz_dx =
                derivative(sxz, index, 1, grid.inverse_dx, false);
            const double dsyz_dy =
                derivative(syz, index, grid.nx, grid.inverse_dy, false);
            const double dszz_dz =
                derivative(szz, index, xy_stride, grid.inverse_dz, true);
            vz[index] = static_cast<float>(
                static_cast<double>(vz[index]) + dt_s *
                    static_cast<double>(buoyancy_z[index]) *
                    (dsxz_dx + dsyz_dy + dszz_dz));
        }
    }
}

__global__ void sample_receivers_kernel(
    const float* vx,
    const float* vy,
    const float* vz,
    const std::size_t* indices,
    const double* weights,
    std::size_t receiver_count,
    std::size_t sample_count,
    std::size_t sample,
    float* vx_traces,
    float* vy_traces,
    float* vz_traces) {
    const auto receiver =
        static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (receiver >= receiver_count) {
        return;
    }

    double values[3] = {0.0, 0.0, 0.0};
    const float* fields[3] = {vx, vy, vz};
#pragma unroll
    for (std::size_t component = 0; component < 3; ++component) {
#pragma unroll
        for (std::size_t node = 0; node < 8; ++node) {
            const auto flat = (receiver * 3 + component) * 8 + node;
            values[component] += weights[flat] *
                                 static_cast<double>(fields[component][indices[flat]]);
        }
    }
    const auto trace_index = receiver * sample_count + sample;
    vx_traces[trace_index] = static_cast<float>(values[0]);
    vy_traces[trace_index] = static_cast<float>(values[1]);
    vz_traces[trace_index] = static_cast<float>(values[2]);
}

} // namespace

void update_elastic_stresses(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    double dt_s) {
    require_matching_grid(
        wavefield.grid_,
        coefficients.grid_,
        "CUDA wavefield and coefficient grids must match");
    require_time_step(dt_s);
    const auto grid = make_kernel_grid(wavefield.grid_);
    update_stresses_kernel<<<
        launch_block_count(grid.cell_count),
        threads_per_block>>>(
        grid,
        wavefield.vx_m_s_.get(),
        wavefield.vy_m_s_.get(),
        wavefield.vz_m_s_.get(),
        wavefield.sxx_pa_.get(),
        wavefield.syy_pa_.get(),
        wavefield.szz_pa_.get(),
        wavefield.sxy_pa_.get(),
        wavefield.sxz_pa_.get(),
        wavefield.syz_pa_.get(),
        coefficients.lambda_pa_.get(),
        coefficients.shear_modulus_pa_.get(),
        coefficients.shear_modulus_xy_pa_.get(),
        coefficients.shear_modulus_xz_pa_.get(),
        coefficients.shear_modulus_yz_pa_.get(),
        dt_s);
    check_last_launch("update_stresses_kernel launch");
}

void inject_moment_tensor_source(
    DeviceElasticWavefield& wavefield,
    const DeviceMomentTensorSource& source,
    std::size_t step_index_n,
    double dt_s) {
    require_matching_grid(
        wavefield.grid_,
        source.grid_,
        "CUDA wavefield and source grids must match");
    require_time_step(dt_s);
    const double stress_time_s =
        static_cast<double>(step_index_n) * dt_s;
    if (!std::isfinite(stress_time_s)) {
        throw std::overflow_error("CUDA source sample time is not finite");
    }
    const double q_s_inv = source_time_value(source.source_, stress_time_s);
    const double cell_volume_m3 =
        static_cast<double>(wavefield.grid_.dx_m) *
        static_cast<double>(wavefield.grid_.dy_m) *
        static_cast<double>(wavefield.grid_.dz_m);
    const double scale = -dt_s * q_s_inv / cell_volume_m3;
    if (!std::isfinite(scale)) {
        throw std::overflow_error("CUDA source scale is not finite");
    }
    const MomentValues moment{{
        source.source_.moment.m_xx_nm,
        source.source_.moment.m_yy_nm,
        source.source_.moment.m_zz_nm,
        source.source_.moment.m_xy_nm,
        source.source_.moment.m_xz_nm,
        source.source_.moment.m_yz_nm}};
    inject_source_kernel<<<1, 64>>>(
        wavefield.sxx_pa_.get(),
        wavefield.syy_pa_.get(),
        wavefield.szz_pa_.get(),
        wavefield.sxy_pa_.get(),
        wavefield.sxz_pa_.get(),
        wavefield.syz_pa_.get(),
        source.linear_indices_.get(),
        source.weights_.get(),
        moment,
        scale);
    check_last_launch("inject_source_kernel launch");
}

void update_elastic_velocities(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    double dt_s) {
    require_matching_grid(
        wavefield.grid_,
        coefficients.grid_,
        "CUDA wavefield and coefficient grids must match");
    require_time_step(dt_s);
    const auto grid = make_kernel_grid(wavefield.grid_);
    update_velocities_kernel<<<
        launch_block_count(grid.cell_count),
        threads_per_block>>>(
        grid,
        wavefield.vx_m_s_.get(),
        wavefield.vy_m_s_.get(),
        wavefield.vz_m_s_.get(),
        wavefield.sxx_pa_.get(),
        wavefield.syy_pa_.get(),
        wavefield.szz_pa_.get(),
        wavefield.sxy_pa_.get(),
        wavefield.sxz_pa_.get(),
        wavefield.syz_pa_.get(),
        coefficients.buoyancy_x_m3_kg_.get(),
        coefficients.buoyancy_y_m3_kg_.get(),
        coefficients.buoyancy_z_m3_kg_.get(),
        dt_s);
    check_last_launch("update_velocities_kernel launch");
}

void sample_receivers_after_velocity_step(
    const DeviceElasticWavefield& wavefield,
    const DeviceReceiverSet& receivers,
    DeviceReceiverTraces& traces,
    std::size_t step_index_n) {
    require_matching_grid(
        wavefield.grid_,
        receivers.grid_,
        "CUDA wavefield and receiver grids must match");
    if (receivers.receiver_count_ != traces.receiver_count_) {
        throw std::invalid_argument(
            "CUDA receiver and trace counts must match");
    }
    if (step_index_n >= traces.sample_count_) {
        throw std::out_of_range("CUDA receiver sample index is outside traces");
    }
    sample_receivers_kernel<<<
        launch_block_count(receivers.receiver_count_),
        threads_per_block>>>(
        wavefield.vx_m_s_.get(),
        wavefield.vy_m_s_.get(),
        wavefield.vz_m_s_.get(),
        receivers.linear_indices_.get(),
        receivers.weights_.get(),
        receivers.receiver_count_,
        traces.sample_count_,
        step_index_n,
        traces.vx_m_s_.get(),
        traces.vy_m_s_.get(),
        traces.vz_m_s_.get());
    check_last_launch("sample_receivers_kernel launch");
}

void advance_elastic_interior_step(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceMomentTensorSource& source,
    const DeviceReceiverSet& receivers,
    DeviceReceiverTraces& traces,
    std::size_t step_index_n) {
    if (step_index_n >= traces.sample_count()) {
        throw std::out_of_range("CUDA elastic step is outside trace storage");
    }
    require_matching_grid(
        wavefield.grid(),
        coefficients.grid(),
        "CUDA wavefield and coefficient grids must match");
    require_matching_grid(
        wavefield.grid(),
        source.grid(),
        "CUDA wavefield and source grids must match");
    require_matching_grid(
        wavefield.grid(),
        receivers.grid(),
        "CUDA wavefield and receiver grids must match");
    if (receivers.receiver_count() != traces.receiver_count()) {
        throw std::invalid_argument(
            "CUDA receiver and trace counts must match");
    }
    update_elastic_stresses(wavefield, coefficients, traces.dt_s());
    inject_moment_tensor_source(
        wavefield,
        source,
        step_index_n,
        traces.dt_s());
    update_elastic_velocities(wavefield, coefficients, traces.dt_s());
    sample_receivers_after_velocity_step(
        wavefield,
        receivers,
        traces,
        step_index_n);
}

} // namespace wave3d::cuda
