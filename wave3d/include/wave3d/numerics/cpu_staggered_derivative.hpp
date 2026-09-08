#pragma once

#include "wave3d/core/coordinates.hpp"
#include "wave3d/numerics/staggered_grid.hpp"

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace wave3d {

enum class DerivativeAxis {
    X,
    Y,
    Z,
};

enum class StaggeredDerivativeMapping {
    IntegerToHalf,
    HalfToInteger,
};

struct StencilTargetRange {
    std::size_t begin{0};
    std::size_t end{0};

    [[nodiscard]] bool contains(std::size_t index) const noexcept {
        return index >= begin && index < end;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return end - begin;
    }
};

namespace detail {

[[nodiscard]] inline std::size_t derivative_axis_extent(
    const Grid3D& grid,
    DerivativeAxis axis) {
    switch (axis) {
    case DerivativeAxis::X:
        return grid.allocated_nx();
    case DerivativeAxis::Y:
        return grid.allocated_ny();
    case DerivativeAxis::Z:
        return grid.allocated_nz();
    }
    throw std::invalid_argument("unknown derivative axis");
}

[[nodiscard]] inline std::size_t derivative_axis_stride(
    const Grid3D& grid,
    DerivativeAxis axis) {
    switch (axis) {
    case DerivativeAxis::X:
        return 1;
    case DerivativeAxis::Y:
        return grid.allocated_nx();
    case DerivativeAxis::Z:
        return grid.allocated_nx() * grid.allocated_ny();
    }
    throw std::invalid_argument("unknown derivative axis");
}

[[nodiscard]] inline double derivative_axis_spacing(
    const Grid3D& grid,
    DerivativeAxis axis) {
    switch (axis) {
    case DerivativeAxis::X:
        return static_cast<double>(grid.dx_m);
    case DerivativeAxis::Y:
        return static_cast<double>(grid.dy_m);
    case DerivativeAxis::Z:
        return static_cast<double>(grid.dz_m);
    }
    throw std::invalid_argument("unknown derivative axis");
}

[[nodiscard]] inline std::size_t target_axis_index(
    const StorageIndex3D& target,
    DerivativeAxis axis) {
    switch (axis) {
    case DerivativeAxis::X:
        return target.x;
    case DerivativeAxis::Y:
        return target.y;
    case DerivativeAxis::Z:
        return target.z;
    }
    throw std::invalid_argument("unknown derivative axis");
}

} // namespace detail

[[nodiscard]] inline StencilTargetRange complete_stencil_target_range(
    const Grid3D& grid,
    DerivativeAxis axis,
    StaggeredDerivativeMapping mapping) {
    require_valid_grid_geometry(grid);
    const auto extent = detail::derivative_axis_extent(grid, axis);
    if (extent < 2 * staggered_fd_radius) {
        throw std::invalid_argument(
            "selected axis has fewer than twelve allocated samples");
    }

    switch (mapping) {
    case StaggeredDerivativeMapping::IntegerToHalf:
        return {staggered_fd_radius - 1, extent - staggered_fd_radius};
    case StaggeredDerivativeMapping::HalfToInteger:
        return {staggered_fd_radius, extent - staggered_fd_radius + 1};
    }
    throw std::invalid_argument("unknown staggered derivative mapping");
}

template <typename T>
[[nodiscard]] double cpu_staggered_derivative_at(
    const T* field,
    std::size_t field_size,
    const Grid3D& grid,
    const StorageIndex3D& target,
    DerivativeAxis axis,
    StaggeredDerivativeMapping mapping) {
    static_assert(
        std::is_same<T, float>::value || std::is_same<T, double>::value,
        "staggered derivative field values must be float or double");
    const auto expected_size = grid.allocated_cell_count();
    if (field == nullptr) {
        throw std::invalid_argument("staggered derivative field is null");
    }
    if (field_size != expected_size) {
        throw std::invalid_argument(
            "staggered derivative field size does not match allocated grid");
    }

    const auto target_index = grid.linear_index(target.x, target.y, target.z);
    const auto range = complete_stencil_target_range(grid, axis, mapping);
    if (!range.contains(detail::target_axis_index(target, axis))) {
        throw std::out_of_range(
            "staggered derivative target lacks a complete radius-six stencil");
    }

    const auto stride = detail::derivative_axis_stride(grid, axis);
    double derivative = 0.0;
    for (std::size_t coefficient_index = 0;
         coefficient_index < staggered_fd_radius;
         ++coefficient_index) {
        std::size_t positive_index = 0;
        std::size_t negative_index = 0;
        switch (mapping) {
        case StaggeredDerivativeMapping::IntegerToHalf:
            positive_index =
                target_index + (coefficient_index + 1) * stride;
            negative_index = target_index - coefficient_index * stride;
            break;
        case StaggeredDerivativeMapping::HalfToInteger:
            positive_index = target_index + coefficient_index * stride;
            negative_index =
                target_index - (coefficient_index + 1) * stride;
            break;
        default:
            throw std::invalid_argument("unknown staggered derivative mapping");
        }
        derivative += staggered_first_derivative_coefficients[coefficient_index] *
                      (static_cast<double>(field[positive_index]) -
                       static_cast<double>(field[negative_index]));
    }
    return derivative / detail::derivative_axis_spacing(grid, axis);
}

template <typename T>
[[nodiscard]] double cpu_staggered_derivative_at(
    const std::vector<T>& field,
    const Grid3D& grid,
    const StorageIndex3D& target,
    DerivativeAxis axis,
    StaggeredDerivativeMapping mapping) {
    return cpu_staggered_derivative_at(
        field.data(), field.size(), grid, target, axis, mapping);
}

} // namespace wave3d
