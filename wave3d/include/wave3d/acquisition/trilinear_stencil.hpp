#pragma once

#include "wave3d/core/coordinates.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace wave3d {

enum class ElasticLattice {
    Integer,
    XHalf,
    YHalf,
    ZHalf,
    XYHalf,
    XZHalf,
    YZHalf
};

struct LatticeOffset3D {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct TrilinearNode {
    StorageIndex3D storage_index{};
    std::size_t linear_index{0};
    double weight{0.0};
};

struct TrilinearStencil {
    ElasticLattice lattice{ElasticLattice::Integer};
    std::array<TrilinearNode, 8> nodes{};
};

[[nodiscard]] constexpr LatticeOffset3D lattice_offset(
    ElasticLattice lattice) {
    switch (lattice) {
    case ElasticLattice::Integer:
        return {0.0, 0.0, 0.0};
    case ElasticLattice::XHalf:
        return {0.5, 0.0, 0.0};
    case ElasticLattice::YHalf:
        return {0.0, 0.5, 0.0};
    case ElasticLattice::ZHalf:
        return {0.0, 0.0, 0.5};
    case ElasticLattice::XYHalf:
        return {0.5, 0.5, 0.0};
    case ElasticLattice::XZHalf:
        return {0.5, 0.0, 0.5};
    case ElasticLattice::YZHalf:
        return {0.0, 0.5, 0.5};
    }
    throw std::invalid_argument("unknown elastic lattice");
}

[[nodiscard]] inline double trilinear_weight_sum(
    const TrilinearStencil& stencil) noexcept {
    double result = 0.0;
    for (const auto& node : stencil.nodes) {
        result += node.weight;
    }
    return result;
}

inline void require_valid_trilinear_stencil(
    const Grid3D& grid,
    const TrilinearStencil& stencil) {
    require_valid_grid_geometry(grid);
    const auto cell_count = grid.allocated_cell_count();
    for (std::size_t node_number = 0;
         node_number < stencil.nodes.size();
         ++node_number) {
        const auto& node = stencil.nodes[node_number];
        if (!std::isfinite(node.weight) || node.weight < 0.0 ||
            node.weight > 1.0) {
            throw std::invalid_argument(
                "trilinear interpolation weights must be finite and non-negative");
        }
        if (node.storage_index.x >= grid.allocated_nx() ||
            node.storage_index.y >= grid.allocated_ny() ||
            node.storage_index.z >= grid.allocated_nz() ||
            node.linear_index >= cell_count ||
            grid.linear_index(
                node.storage_index.x,
                node.storage_index.y,
                node.storage_index.z) != node.linear_index) {
            throw std::invalid_argument(
                "trilinear interpolation node is outside its grid");
        }
        for (std::size_t earlier = 0; earlier < node_number; ++earlier) {
            if (stencil.nodes[earlier].linear_index == node.linear_index) {
                throw std::invalid_argument(
                    "trilinear interpolation nodes must be distinct");
            }
        }
    }

    constexpr double tolerance =
        64.0 * std::numeric_limits<double>::epsilon();
    if (std::abs(trilinear_weight_sum(stencil) - 1.0) > tolerance) {
        throw std::invalid_argument(
            "trilinear interpolation weights must sum to one");
    }
}

namespace detail {

struct LinearInterpolationAxis {
    std::size_t lower{0};
    double fraction{0.0};
};

[[nodiscard]] inline LinearInterpolationAxis prepare_interpolation_axis(
    double coordinate,
    std::size_t extent) {
    if (!std::isfinite(coordinate)) {
        throw std::invalid_argument(
            "trilinear interpolation coordinate must be finite");
    }
    if (extent < 2 || coordinate < 0.0 ||
        !(coordinate < static_cast<double>(extent - 1))) {
        throw std::out_of_range(
            "complete trilinear interpolation support is unavailable");
    }

    const double lower_value = std::floor(coordinate);
    const auto lower = static_cast<std::size_t>(lower_value);
    if (lower >= extent - 1) {
        throw std::out_of_range(
            "complete trilinear interpolation support is unavailable");
    }
    return {lower, coordinate - lower_value};
}

} // namespace detail

[[nodiscard]] inline TrilinearStencil prepare_trilinear_stencil(
    const Grid3D& grid,
    const FractionalStorageCoordinate3D& integer_lattice_coordinate,
    ElasticLattice lattice) {
    require_valid_grid_geometry(grid);
    const auto offset = lattice_offset(lattice);
    const auto x = detail::prepare_interpolation_axis(
        integer_lattice_coordinate.x - offset.x,
        grid.allocated_nx());
    const auto y = detail::prepare_interpolation_axis(
        integer_lattice_coordinate.y - offset.y,
        grid.allocated_ny());
    const auto z = detail::prepare_interpolation_axis(
        integer_lattice_coordinate.z - offset.z,
        grid.allocated_nz());

    TrilinearStencil result{};
    result.lattice = lattice;
    std::size_t node_number = 0;
    for (std::size_t z_corner = 0; z_corner < 2; ++z_corner) {
        const double z_weight = z_corner == 0 ? 1.0 - z.fraction : z.fraction;
        for (std::size_t y_corner = 0; y_corner < 2; ++y_corner) {
            const double y_weight =
                y_corner == 0 ? 1.0 - y.fraction : y.fraction;
            for (std::size_t x_corner = 0; x_corner < 2; ++x_corner) {
                const double x_weight =
                    x_corner == 0 ? 1.0 - x.fraction : x.fraction;
                const StorageIndex3D index{
                    x.lower + x_corner,
                    y.lower + y_corner,
                    z.lower + z_corner};
                result.nodes[node_number] = {
                    index,
                    grid.linear_index(index.x, index.y, index.z),
                    x_weight * y_weight * z_weight};
                ++node_number;
            }
        }
    }
    require_valid_trilinear_stencil(grid, result);
    return result;
}

[[nodiscard]] inline TrilinearStencil prepare_trilinear_stencil(
    const Grid3D& grid,
    const PhysicalPoint3D& physical_location,
    ElasticLattice lattice) {
    return prepare_trilinear_stencil(
        grid,
        physical_to_storage_coordinate(grid, physical_location),
        lattice);
}

} // namespace wave3d
