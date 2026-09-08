#pragma once

#include "wave3d/core/checked_size.hpp"
#include "wave3d/core/grid.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace wave3d {

struct PhysicalPoint3D {
    double x_m{0.0};
    double y_m{0.0};
    double z_m{0.0};
};

struct FractionalGridCoordinate3D {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct FractionalStorageCoordinate3D {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct PhysicalGridIndex3D {
    std::size_t x{0};
    std::size_t y{0};
    std::size_t z{0};
};

struct StorageIndex3D {
    std::size_t x{0};
    std::size_t y{0};
    std::size_t z{0};
};

[[nodiscard]] constexpr std::string_view coordinate_convention() noexcept {
    return "x=east, y=north, z=down, units=metres, surface_z=0";
}

inline void require_valid_grid_geometry(const Grid3D& grid) {
    if (grid.nx == 0 || grid.ny == 0 || grid.nz == 0) {
        throw std::invalid_argument("physical grid dimensions must be positive");
    }
    if (!std::isfinite(grid.dx_m) || !std::isfinite(grid.dy_m) ||
        !std::isfinite(grid.dz_m) || !(grid.dx_m > 0.0F) ||
        !(grid.dy_m > 0.0F) || !(grid.dz_m > 0.0F)) {
        throw std::invalid_argument(
            "physical grid spacing must be finite and positive");
    }
    static_cast<void>(grid.physical_cell_count());
    static_cast<void>(grid.allocated_cell_count());
}

[[nodiscard]] inline PhysicalPoint3D physical_domain_max(const Grid3D& grid) {
    require_valid_grid_geometry(grid);
    const PhysicalPoint3D maximum{
        static_cast<double>(grid.nx - 1) * static_cast<double>(grid.dx_m),
        static_cast<double>(grid.ny - 1) * static_cast<double>(grid.dy_m),
        static_cast<double>(grid.nz - 1) * static_cast<double>(grid.dz_m)};
    if (!std::isfinite(maximum.x_m) || !std::isfinite(maximum.y_m) ||
        !std::isfinite(maximum.z_m)) {
        throw std::overflow_error("physical grid extent is not finite");
    }
    return maximum;
}

inline void require_point_in_physical_domain(
    const Grid3D& grid,
    const PhysicalPoint3D& point) {
    if (!std::isfinite(point.x_m) || !std::isfinite(point.y_m) ||
        !std::isfinite(point.z_m)) {
        throw std::invalid_argument("physical point coordinates must be finite");
    }
    const auto maximum = physical_domain_max(grid);
    if (point.x_m < 0.0 || point.y_m < 0.0 || point.z_m < 0.0 ||
        point.x_m > maximum.x_m || point.y_m > maximum.y_m ||
        point.z_m > maximum.z_m) {
        throw std::out_of_range("physical point is outside the physical grid");
    }
}

[[nodiscard]] inline FractionalGridCoordinate3D physical_to_grid_coordinate(
    const Grid3D& grid,
    const PhysicalPoint3D& point) {
    require_point_in_physical_domain(grid, point);
    return {
        point.x_m / static_cast<double>(grid.dx_m),
        point.y_m / static_cast<double>(grid.dy_m),
        point.z_m / static_cast<double>(grid.dz_m)};
}

[[nodiscard]] inline FractionalStorageCoordinate3D
physical_to_storage_coordinate(
    const Grid3D& grid,
    const PhysicalPoint3D& point) {
    const auto physical = physical_to_grid_coordinate(grid, point);
    return {
        physical.x + static_cast<double>(grid.physical_origin_x()),
        physical.y + static_cast<double>(grid.physical_origin_y()),
        physical.z + static_cast<double>(grid.physical_origin_z())};
}

[[nodiscard]] inline StorageIndex3D physical_to_storage_index(
    const Grid3D& grid,
    const PhysicalGridIndex3D& index) {
    require_valid_grid_geometry(grid);
    if (index.x >= grid.nx || index.y >= grid.ny || index.z >= grid.nz) {
        throw std::out_of_range("physical grid index is outside the physical grid");
    }
    return {
        detail::checked_size_add(
            grid.physical_origin_x(),
            index.x,
            "Wave3D storage x index overflows size_t"),
        detail::checked_size_add(
            grid.physical_origin_y(),
            index.y,
            "Wave3D storage y index overflows size_t"),
        detail::checked_size_add(
            grid.physical_origin_z(),
            index.z,
            "Wave3D storage z index overflows size_t")};
}

[[nodiscard]] inline PhysicalPoint3D physical_index_to_point(
    const Grid3D& grid,
    const PhysicalGridIndex3D& index) {
    static_cast<void>(physical_to_storage_index(grid, index));
    return {
        static_cast<double>(index.x) * static_cast<double>(grid.dx_m),
        static_cast<double>(index.y) * static_cast<double>(grid.dy_m),
        static_cast<double>(index.z) * static_cast<double>(grid.dz_m)};
}

} // namespace wave3d
