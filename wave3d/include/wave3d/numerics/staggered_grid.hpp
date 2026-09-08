#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace wave3d {

struct ExactRatio {
    std::int64_t numerator{0};
    std::int64_t denominator{1};

    [[nodiscard]] constexpr double value() const noexcept {
        return static_cast<double>(numerator) /
               static_cast<double>(denominator);
    }
};

inline constexpr std::size_t staggered_fd_radius = 6;
inline constexpr std::size_t staggered_fd_spatial_order = 12;

inline constexpr std::array<ExactRatio, staggered_fd_radius>
    staggered_first_derivative_exact{{
        {160083, 131072},
        {-12705, 131072},
        {22869, 1310720},
        {-5445, 1835008},
        {847, 2359296},
        {-63, 2883584},
    }};

inline constexpr std::array<double, staggered_fd_radius>
    staggered_first_derivative_coefficients{{
        staggered_first_derivative_exact[0].value(),
        staggered_first_derivative_exact[1].value(),
        staggered_first_derivative_exact[2].value(),
        staggered_first_derivative_exact[3].value(),
        staggered_first_derivative_exact[4].value(),
        staggered_first_derivative_exact[5].value(),
    }};

inline constexpr ExactRatio staggered_symbol_max_exact{1187803, 887040};
inline constexpr double staggered_symbol_max =
    staggered_symbol_max_exact.value();

[[nodiscard]] inline double staggered_symbol(double theta) {
    if (!std::isfinite(theta)) {
        throw std::invalid_argument("Fourier angle must be finite");
    }
    double value = 0.0;
    for (std::size_t index = 0; index < staggered_fd_radius; ++index) {
        const auto half_offset = static_cast<double>(index) + 0.5;
        value += staggered_first_derivative_coefficients[index] *
                 std::sin(half_offset * theta);
    }
    return value;
}

[[nodiscard]] inline double staggered_spatial_phase_velocity_ratio(
    double points_per_wavelength) {
    if (!std::isfinite(points_per_wavelength) ||
        !(points_per_wavelength > 2.0)) {
        throw std::invalid_argument(
            "points per wavelength must be finite and greater than two");
    }
    const auto theta =
        2.0 * std::acos(-1.0) / points_per_wavelength;
    return 2.0 * staggered_symbol(theta) / theta;
}

[[nodiscard]] inline double staggered_spatial_group_velocity_ratio(
    double points_per_wavelength) {
    if (!std::isfinite(points_per_wavelength) ||
        !(points_per_wavelength > 2.0)) {
        throw std::invalid_argument(
            "points per wavelength must be finite and greater than two");
    }
    const auto theta =
        2.0 * std::acos(-1.0) / points_per_wavelength;
    double derivative = 0.0;
    for (std::size_t index = 0; index < staggered_fd_radius; ++index) {
        const auto half_offset = static_cast<double>(index) + 0.5;
        derivative += staggered_first_derivative_coefficients[index] *
                      half_offset * std::cos(half_offset * theta);
    }
    return 2.0 * derivative;
}

} // namespace wave3d
