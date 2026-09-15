#include "wave3d/cuda/visualization.hpp"

#include "wave3d/cuda/cuda_error.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace wave3d::cuda {
namespace {

constexpr unsigned int threads_per_block = 256;
constexpr std::size_t maximum_block_count = 65535;

__device__ __constant__ double visualization_derivative_coefficients[6] = {
    160083.0 / 131072.0,
    -12705.0 / 131072.0,
    22869.0 / 1310720.0,
    -5445.0 / 1835008.0,
    847.0 / 2359296.0,
    -63.0 / 2883584.0};

struct VisualizationKernelGrid {
    std::size_t allocated_nx{0};
    std::size_t allocated_ny{0};
    std::size_t physical_nx{0};
    std::size_t physical_ny{0};
    std::size_t physical_nz{0};
    std::size_t physical_cells{0};
    std::size_t origin_x{0};
    std::size_t origin_y{0};
    std::size_t origin_z{0};
    double inverse_dx{0.0};
    double inverse_dy{0.0};
    double inverse_dz{0.0};
};

[[nodiscard]] VisualizationKernelGrid make_kernel_grid(const Grid3D& grid) {
    require_valid_grid_geometry(grid);
    return {
        grid.allocated_nx(),
        grid.allocated_ny(),
        grid.nx,
        grid.ny,
        grid.nz,
        grid.physical_cell_count(),
        grid.physical_origin_x(),
        grid.physical_origin_y(),
        grid.physical_origin_z(),
        1.0 / static_cast<double>(grid.dx_m),
        1.0 / static_cast<double>(grid.dy_m),
        1.0 / static_cast<double>(grid.dz_m)};
}

[[nodiscard]] unsigned int launch_block_count(std::size_t work_items) {
    const auto required = (work_items - 1) / threads_per_block + 1;
    return static_cast<unsigned int>(
        std::min(required, maximum_block_count));
}

__device__ std::size_t storage_index(
    const VisualizationKernelGrid& grid,
    std::size_t x,
    std::size_t y,
    std::size_t z) {
    return x + grid.allocated_nx * (y + grid.allocated_ny * z);
}

__device__ double staggered_derivative(
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
        value += visualization_derivative_coefficients[coefficient] *
                 (static_cast<double>(field[positive]) -
                  static_cast<double>(field[negative]));
    }
    return value * inverse_spacing;
}

__device__ double centred_vx(
    const VisualizationKernelGrid& grid,
    const float* vx,
    std::size_t x,
    std::size_t y,
    std::size_t z) {
    return 0.5 *
           (static_cast<double>(vx[storage_index(grid, x - 1, y, z)]) +
            static_cast<double>(vx[storage_index(grid, x, y, z)]));
}

__device__ double centred_vy(
    const VisualizationKernelGrid& grid,
    const float* vy,
    std::size_t x,
    std::size_t y,
    std::size_t z) {
    return 0.5 *
           (static_cast<double>(vy[storage_index(grid, x, y - 1, z)]) +
            static_cast<double>(vy[storage_index(grid, x, y, z)]));
}

__device__ double centred_vz(
    const VisualizationKernelGrid& grid,
    const float* vz,
    std::size_t x,
    std::size_t y,
    std::size_t z) {
    return 0.5 *
           (static_cast<double>(vz[storage_index(grid, x, y, z - 1)]) +
            static_cast<double>(vz[storage_index(grid, x, y, z)]));
}

__device__ double curl_x_at_yz_half(
    const VisualizationKernelGrid& grid,
    const float* vy,
    const float* vz,
    std::size_t x,
    std::size_t y,
    std::size_t z) {
    const auto index = storage_index(grid, x, y, z);
    const auto z_stride = grid.allocated_nx * grid.allocated_ny;
    return staggered_derivative(
               vz, index, grid.allocated_nx, grid.inverse_dy, true) -
           staggered_derivative(
               vy, index, z_stride, grid.inverse_dz, true);
}

__device__ double curl_y_at_xz_half(
    const VisualizationKernelGrid& grid,
    const float* vx,
    const float* vz,
    std::size_t x,
    std::size_t y,
    std::size_t z) {
    const auto index = storage_index(grid, x, y, z);
    const auto z_stride = grid.allocated_nx * grid.allocated_ny;
    return staggered_derivative(
               vx, index, z_stride, grid.inverse_dz, true) -
           staggered_derivative(vz, index, 1, grid.inverse_dx, true);
}

__device__ double curl_z_at_xy_half(
    const VisualizationKernelGrid& grid,
    const float* vx,
    const float* vy,
    std::size_t x,
    std::size_t y,
    std::size_t z) {
    const auto index = storage_index(grid, x, y, z);
    return staggered_derivative(vy, index, 1, grid.inverse_dx, true) -
           staggered_derivative(
               vx, index, grid.allocated_nx, grid.inverse_dy, true);
}

__device__ double centred_curl_x(
    const VisualizationKernelGrid& grid,
    const float* vy,
    const float* vz,
    std::size_t x,
    std::size_t y,
    std::size_t z) {
    return 0.25 *
           (curl_x_at_yz_half(grid, vy, vz, x, y - 1, z - 1) +
            curl_x_at_yz_half(grid, vy, vz, x, y, z - 1) +
            curl_x_at_yz_half(grid, vy, vz, x, y - 1, z) +
            curl_x_at_yz_half(grid, vy, vz, x, y, z));
}

__device__ double centred_curl_y(
    const VisualizationKernelGrid& grid,
    const float* vx,
    const float* vz,
    std::size_t x,
    std::size_t y,
    std::size_t z) {
    return 0.25 *
           (curl_y_at_xz_half(grid, vx, vz, x - 1, y, z - 1) +
            curl_y_at_xz_half(grid, vx, vz, x, y, z - 1) +
            curl_y_at_xz_half(grid, vx, vz, x - 1, y, z) +
            curl_y_at_xz_half(grid, vx, vz, x, y, z));
}

__device__ double centred_curl_z(
    const VisualizationKernelGrid& grid,
    const float* vx,
    const float* vy,
    std::size_t x,
    std::size_t y,
    std::size_t z) {
    return 0.25 *
           (curl_z_at_xy_half(grid, vx, vy, x - 1, y - 1, z) +
            curl_z_at_xy_half(grid, vx, vy, x, y - 1, z) +
            curl_z_at_xy_half(grid, vx, vy, x - 1, y, z) +
            curl_z_at_xy_half(grid, vx, vy, x, y, z));
}

__global__ void extract_visualization_kernel(
    VisualizationKernelGrid grid,
    const float* vx,
    const float* vy,
    const float* vz,
    VisualizationField field,
    float* output) {
    const auto start = static_cast<std::size_t>(blockIdx.x) * blockDim.x +
                       threadIdx.x;
    const auto step = static_cast<std::size_t>(blockDim.x) * gridDim.x;
    for (auto output_index = start;
         output_index < grid.physical_cells;
         output_index += step) {
        const auto physical_x = output_index % grid.physical_nx;
        const auto physical_yz = output_index / grid.physical_nx;
        const auto physical_y = physical_yz % grid.physical_ny;
        const auto physical_z = physical_yz / grid.physical_ny;
        const auto x = grid.origin_x + physical_x;
        const auto y = grid.origin_y + physical_y;
        const auto z = grid.origin_z + physical_z;

        const double vx_value = centred_vx(grid, vx, x, y, z);
        const double vy_value = centred_vy(grid, vy, x, y, z);
        const double vz_value = centred_vz(grid, vz, x, y, z);
        double value = 0.0;
        switch (field) {
        case VisualizationField::Vx:
            value = vx_value;
            break;
        case VisualizationField::Vy:
            value = vy_value;
            break;
        case VisualizationField::Vz:
            value = vz_value;
            break;
        case VisualizationField::Speed:
            value = sqrt(
                vx_value * vx_value + vy_value * vy_value +
                vz_value * vz_value);
            break;
        case VisualizationField::Divergence: {
            const auto index = storage_index(grid, x, y, z);
            const auto z_stride = grid.allocated_nx * grid.allocated_ny;
            value =
                staggered_derivative(vx, index, 1, grid.inverse_dx, false) +
                staggered_derivative(
                    vy,
                    index,
                    grid.allocated_nx,
                    grid.inverse_dy,
                    false) +
                staggered_derivative(
                    vz, index, z_stride, grid.inverse_dz, false);
            break;
        }
        case VisualizationField::CurlMagnitude: {
            const double curl_x = centred_curl_x(grid, vy, vz, x, y, z);
            const double curl_y = centred_curl_y(grid, vx, vz, x, y, z);
            const double curl_z = centred_curl_z(grid, vx, vy, x, y, z);
            value = sqrt(
                curl_x * curl_x + curl_y * curl_y + curl_z * curl_z);
            break;
        }
        }
        output[output_index] = static_cast<float>(value);
    }
}

void require_known_field(VisualizationField field) {
    switch (field) {
    case VisualizationField::Vx:
    case VisualizationField::Vy:
    case VisualizationField::Vz:
    case VisualizationField::Speed:
    case VisualizationField::Divergence:
    case VisualizationField::CurlMagnitude:
        return;
    }
    throw std::invalid_argument("unknown CUDA visualization field");
}

} // namespace

void extract_physical_visualization_volume(
    const DeviceElasticWavefieldConstView& wavefield,
    VisualizationField field,
    DeviceVisualizationVolume& destination) {
    require_valid_device_elastic_wavefield_view(wavefield);
    require_known_field(field);
    if (!same_grid_geometry(wavefield.grid, destination.grid_)) {
        throw std::invalid_argument(
            "CUDA visualization wavefield and destination grids must match");
    }
    if (wavefield.grid.halo < 6) {
        throw std::invalid_argument(
            "CUDA visualization derivatives require a radius-six halo");
    }
    const auto grid = make_kernel_grid(wavefield.grid);
    extract_visualization_kernel<<<
        launch_block_count(grid.physical_cells),
        threads_per_block>>>(
        grid,
        wavefield.vx_m_s.data(),
        wavefield.vy_m_s.data(),
        wavefield.vz_m_s.data(),
        field,
        destination.values_.get());
    check_last_launch("extract_visualization_kernel launch");
}

} // namespace wave3d::cuda
