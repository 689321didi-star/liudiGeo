#pragma once

#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/numerics/cpu_staggered_derivative.hpp"
#include "wave3d/physics/cpu_elastic_update.hpp"
#include "wave3d/wave/elastic_wavefield.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace wave3d {

struct CpmlSides {
    bool x_min{true};
    bool x_max{true};
    bool y_min{true};
    bool y_max{true};
    bool z_min{true};
    bool z_max{true};
};

struct CpmlParameters {
    double dt_s{0.0};
    double maximum_velocity_m_s{0.0};
    double dominant_frequency_hz{0.0};
    double target_reflection{1.0e-3};
    double polynomial_power{2.0};
    double kappa_max{1.0};
    CpmlSides sides{};
};

struct CpmlAxisCoefficients {
    std::vector<float> a_integer;
    std::vector<float> b_integer;
    std::vector<float> inverse_kappa_integer;
    std::vector<float> a_half;
    std::vector<float> b_half;
    std::vector<float> inverse_kappa_half;

    [[nodiscard]] std::size_t bytes() const noexcept {
        return (a_integer.size() + b_integer.size() +
                inverse_kappa_integer.size() + a_half.size() + b_half.size() +
                inverse_kappa_half.size()) * sizeof(float);
    }
};

struct CpmlProfile {
    Grid3D grid{};
    CpmlParameters parameters{};
    std::array<CpmlAxisCoefficients, 3> axes;

    [[nodiscard]] std::size_t bytes() const noexcept {
        return axes[0].bytes() + axes[1].bytes() + axes[2].bytes();
    }
};

inline void require_valid_cpml_profile(const CpmlProfile& profile);

enum class CpmlMemory : std::size_t {
    DvxDx,
    DvyDy,
    DvzDz,
    DvxDy,
    DvyDx,
    DvxDz,
    DvzDx,
    DvyDz,
    DvzDy,
    DsxxDx,
    DsxyDy,
    DsxzDz,
    DsxyDx,
    DsyyDy,
    DsyzDz,
    DsxzDx,
    DsyzDy,
    DszzDz,
    Count
};

inline constexpr std::size_t cpml_memory_field_count =
    static_cast<std::size_t>(CpmlMemory::Count);

struct CpmlState {
    Grid3D grid{};
    std::array<std::vector<float>, cpml_memory_field_count> fields;

    explicit CpmlState(const Grid3D& source_grid) : grid(source_grid) {
        require_valid_grid_geometry(grid);
        const auto cells = grid.allocated_cell_count();
        for (auto& field : fields) {
            field.resize(cells);
        }
    }

    CpmlState(const CpmlState&) = delete;
    CpmlState& operator=(const CpmlState&) = delete;
    CpmlState(CpmlState&&) noexcept = default;
    CpmlState& operator=(CpmlState&&) noexcept = default;

    [[nodiscard]] std::size_t bytes() const {
        return detail::checked_size_product(
            cpml_memory_field_count,
            detail::checked_size_product(
                grid.allocated_cell_count(),
                sizeof(float),
                "CPML state field bytes overflow"),
            "CPML state bytes overflow");
    }
};

namespace detail {

inline void require_cpml_parameters(
    const Grid3D& grid,
    const CpmlParameters& parameters) {
    require_valid_grid_geometry(grid);
    for (const double value : {
             parameters.dt_s,
             parameters.maximum_velocity_m_s,
             parameters.dominant_frequency_hz,
             parameters.target_reflection,
             parameters.polynomial_power,
             parameters.kappa_max}) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("CPML parameters must be finite");
        }
    }
    if (!(parameters.dt_s > 0.0) ||
        !(parameters.maximum_velocity_m_s > 0.0) ||
        !(parameters.dominant_frequency_hz > 0.0) ||
        !(parameters.target_reflection > 0.0) ||
        !(parameters.target_reflection < 1.0) ||
        !(parameters.polynomial_power >= 1.0) ||
        !(parameters.kappa_max >= 1.0)) {
        throw std::invalid_argument(
            "CPML requires positive dt/velocity/frequency, reflection in (0,1), power >= 1, and kappa >= 1");
    }
    const std::array<std::pair<bool, std::size_t>, 6> selected{{
        {parameters.sides.x_min, grid.x_boundary.lower_absorbing},
        {parameters.sides.x_max, grid.x_boundary.upper_absorbing},
        {parameters.sides.y_min, grid.y_boundary.lower_absorbing},
        {parameters.sides.y_max, grid.y_boundary.upper_absorbing},
        {parameters.sides.z_min, grid.z_boundary.lower_absorbing},
        {parameters.sides.z_max, grid.z_boundary.upper_absorbing}}};
    bool any_enabled = false;
    for (const auto& side : selected) {
        if (side.first && side.second == 0) {
            throw std::invalid_argument(
                "an enabled CPML side requires positive absorbing width");
        }
        any_enabled = any_enabled || side.first;
    }
    if (!any_enabled) {
        throw std::invalid_argument("at least one CPML side must be enabled");
    }
}

struct CpmlCoefficientTriple {
    float a{0.0F};
    float b{1.0F};
    float inverse_kappa{1.0F};
};

[[nodiscard]] inline CpmlCoefficientTriple cpml_coefficient_at(
    double logical_coordinate,
    std::size_t physical_count,
    double spacing_m,
    const AxisBoundary& boundary,
    bool lower_enabled,
    bool upper_enabled,
    const CpmlParameters& parameters) {
    double normalized_depth = 0.0;
    std::size_t width = 0;
    if (lower_enabled && logical_coordinate < 0.0) {
        width = boundary.lower_absorbing;
        normalized_depth = -logical_coordinate / static_cast<double>(width);
    } else if (
        upper_enabled &&
        logical_coordinate > static_cast<double>(physical_count - 1)) {
        width = boundary.upper_absorbing;
        normalized_depth =
            (logical_coordinate - static_cast<double>(physical_count - 1)) /
            static_cast<double>(width);
    } else {
        return {};
    }
    normalized_depth = std::min(normalized_depth, 1.0);
    const double shaped =
        std::pow(normalized_depth, parameters.polynomial_power);
    const double thickness_m = static_cast<double>(width) * spacing_m;
    const double sigma_max =
        -(parameters.polynomial_power + 1.0) *
        parameters.maximum_velocity_m_s *
        std::log(parameters.target_reflection) / (2.0 * thickness_m);
    constexpr double pi = 3.141592653589793238462643383279502884;
    const double sigma = sigma_max * shaped;
    const double kappa = 1.0 + (parameters.kappa_max - 1.0) * shaped;
    const double alpha = pi * parameters.dominant_frequency_hz *
                         (1.0 - normalized_depth);
    const double b = std::exp(-(sigma / kappa + alpha) * parameters.dt_s);
    const double a = sigma == 0.0
                         ? 0.0
                         : sigma * (b - 1.0) /
                               (kappa * (sigma + kappa * alpha));
    const double inverse_kappa = 1.0 / kappa;
    if (!std::isfinite(a) || !std::isfinite(b) ||
        !std::isfinite(inverse_kappa)) {
        throw std::overflow_error("CPML coefficient is not finite");
    }
    return {
        static_cast<float>(a),
        static_cast<float>(b),
        static_cast<float>(inverse_kappa)};
}

inline void fill_cpml_axis(
    CpmlAxisCoefficients& axis,
    std::size_t allocated_count,
    std::size_t physical_origin,
    std::size_t physical_count,
    double spacing_m,
    const AxisBoundary& boundary,
    bool lower_enabled,
    bool upper_enabled,
    const CpmlParameters& parameters) {
    axis.a_integer.resize(allocated_count);
    axis.b_integer.resize(allocated_count);
    axis.inverse_kappa_integer.resize(allocated_count);
    axis.a_half.resize(allocated_count);
    axis.b_half.resize(allocated_count);
    axis.inverse_kappa_half.resize(allocated_count);
    for (std::size_t coordinate = 0; coordinate < allocated_count; ++coordinate) {
        const double logical = static_cast<double>(coordinate) -
                               static_cast<double>(physical_origin);
        const auto integer = cpml_coefficient_at(
            logical,
            physical_count,
            spacing_m,
            boundary,
            lower_enabled,
            upper_enabled,
            parameters);
        const auto half = cpml_coefficient_at(
            logical + 0.5,
            physical_count,
            spacing_m,
            boundary,
            lower_enabled,
            upper_enabled,
            parameters);
        axis.a_integer[coordinate] = integer.a;
        axis.b_integer[coordinate] = integer.b;
        axis.inverse_kappa_integer[coordinate] = integer.inverse_kappa;
        axis.a_half[coordinate] = half.a;
        axis.b_half[coordinate] = half.b;
        axis.inverse_kappa_half[coordinate] = half.inverse_kappa;
    }
}

inline void require_cpml_layout(
    const CpmlProfile& profile,
    const CpmlState& state,
    const ElasticWavefield& wavefield,
    const ElasticCoefficients& coefficients) {
    require_valid_cpml_profile(profile);
    require_valid_elastic_wavefield_layout(wavefield);
    require_valid_elastic_coefficient_layout(coefficients);
    if (!same_grid_geometry(profile.grid, state.grid) ||
        !same_grid_geometry(profile.grid, wavefield.grid) ||
        !same_grid_geometry(profile.grid, coefficients.grid)) {
        throw std::invalid_argument("CPML inputs must use one exact grid");
    }
    if (profile.parameters.dt_s <= 0.0 ||
        !std::isfinite(profile.parameters.dt_s)) {
        throw std::invalid_argument("CPML profile dt must be positive");
    }
    const auto cells = profile.grid.allocated_cell_count();
    for (const auto& field : state.fields) {
        if (field.size() != cells) {
            throw std::invalid_argument("CPML state field has invalid size");
        }
    }
    const std::array<std::size_t, 3> counts{{
        profile.grid.allocated_nx(),
        profile.grid.allocated_ny(),
        profile.grid.allocated_nz()}};
    for (std::size_t axis_number = 0; axis_number < 3; ++axis_number) {
        const auto& axis = profile.axes[axis_number];
        for (const auto size : {
                 axis.a_integer.size(),
                 axis.b_integer.size(),
                 axis.inverse_kappa_integer.size(),
                 axis.a_half.size(),
                 axis.b_half.size(),
                 axis.inverse_kappa_half.size()}) {
            if (size != counts[axis_number]) {
                throw std::invalid_argument(
                    "CPML axis coefficient has invalid size");
            }
        }
    }
}

[[nodiscard]] inline double cpml_corrected_derivative(
    CpmlState& state,
    const CpmlProfile& profile,
    CpmlMemory memory,
    std::size_t cell_index,
    DerivativeAxis derivative_axis,
    StaggeredDerivativeMapping mapping,
    std::size_t axis_coordinate,
    double raw_derivative) {
    const auto axis_number = static_cast<std::size_t>(derivative_axis);
    const auto& axis = profile.axes[axis_number];
    const bool target_is_half =
        mapping == StaggeredDerivativeMapping::IntegerToHalf;
    const auto& a = target_is_half ? axis.a_half : axis.a_integer;
    const auto& b = target_is_half ? axis.b_half : axis.b_integer;
    const auto& inverse_kappa = target_is_half
                                    ? axis.inverse_kappa_half
                                    : axis.inverse_kappa_integer;
    auto& value = state.fields[static_cast<std::size_t>(memory)][cell_index];
    const double updated = static_cast<double>(b[axis_coordinate]) *
                               static_cast<double>(value) +
                           static_cast<double>(a[axis_coordinate]) *
                               raw_derivative;
    if (!std::isfinite(updated) ||
        std::abs(updated) > static_cast<double>(
                                std::numeric_limits<float>::max())) {
        throw std::overflow_error("CPML memory update exceeds float32");
    }
    value = static_cast<float>(updated);
    return static_cast<double>(inverse_kappa[axis_coordinate]) *
               raw_derivative +
           static_cast<double>(value);
}

} // namespace detail

[[nodiscard]] inline CpmlProfile prepare_cpml_profile(
    const Grid3D& grid,
    const CpmlParameters& parameters) {
    detail::require_cpml_parameters(grid, parameters);
    CpmlProfile profile{grid, parameters, {}};
    detail::fill_cpml_axis(
        profile.axes[0],
        grid.allocated_nx(),
        grid.physical_origin_x(),
        grid.nx,
        grid.dx_m,
        grid.x_boundary,
        parameters.sides.x_min,
        parameters.sides.x_max,
        parameters);
    detail::fill_cpml_axis(
        profile.axes[1],
        grid.allocated_ny(),
        grid.physical_origin_y(),
        grid.ny,
        grid.dy_m,
        grid.y_boundary,
        parameters.sides.y_min,
        parameters.sides.y_max,
        parameters);
    detail::fill_cpml_axis(
        profile.axes[2],
        grid.allocated_nz(),
        grid.physical_origin_z(),
        grid.nz,
        grid.dz_m,
        grid.z_boundary,
        parameters.sides.z_min,
        parameters.sides.z_max,
        parameters);
    return profile;
}

inline void require_valid_cpml_profile(const CpmlProfile& profile) {
    detail::require_cpml_parameters(profile.grid, profile.parameters);
    const std::array<std::size_t, 3> counts{{
        profile.grid.allocated_nx(),
        profile.grid.allocated_ny(),
        profile.grid.allocated_nz()}};
    for (std::size_t axis_number = 0; axis_number < 3; ++axis_number) {
        const auto& axis = profile.axes[axis_number];
        const std::array<const std::vector<float>*, 6> arrays{{
            &axis.a_integer,
            &axis.b_integer,
            &axis.inverse_kappa_integer,
            &axis.a_half,
            &axis.b_half,
            &axis.inverse_kappa_half}};
        for (const auto* values : arrays) {
            if (values->size() != counts[axis_number]) {
                throw std::invalid_argument(
                    "CPML axis coefficient has invalid size");
            }
            for (const float value : *values) {
                if (!std::isfinite(value)) {
                    throw std::invalid_argument(
                        "CPML axis coefficient must be finite");
                }
            }
        }
    }
}

inline void cpu_update_elastic_stresses_cpml(
    ElasticWavefield& wavefield,
    const ElasticCoefficients& coefficients,
    const CpmlProfile& profile,
    CpmlState& state) {
    detail::require_cpml_layout(profile, state, wavefield, coefficients);
    const auto& grid = wavefield.grid;
    constexpr auto i2h = StaggeredDerivativeMapping::IntegerToHalf;
    constexpr auto h2i = StaggeredDerivativeMapping::HalfToInteger;
    const auto i2h_x = complete_stencil_target_range(grid, DerivativeAxis::X, i2h);
    const auto i2h_y = complete_stencil_target_range(grid, DerivativeAxis::Y, i2h);
    const auto i2h_z = complete_stencil_target_range(grid, DerivativeAxis::Z, i2h);
    const auto h2i_x = complete_stencil_target_range(grid, DerivativeAxis::X, h2i);
    const auto h2i_y = complete_stencil_target_range(grid, DerivativeAxis::Y, h2i);
    const auto h2i_z = complete_stencil_target_range(grid, DerivativeAxis::Z, h2i);
    const double dt_s = profile.parameters.dt_s;

    for (std::size_t z = 0; z < grid.allocated_nz(); ++z) {
        for (std::size_t y = 0; y < grid.allocated_ny(); ++y) {
            for (std::size_t x = 0; x < grid.allocated_nx(); ++x) {
                const StorageIndex3D target{x, y, z};
                const auto index = grid.linear_index(x, y, z);
                if (x >= h2i_x.begin && x < h2i_x.end &&
                    y >= h2i_y.begin && y < h2i_y.end &&
                    z >= h2i_z.begin && z < h2i_z.end) {
                    const double dvx_dx = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DvxDx, index,
                        DerivativeAxis::X, h2i, x,
                        cpu_staggered_derivative_at(
                            wavefield.vx_m_s, grid, target,
                            DerivativeAxis::X, h2i));
                    const double dvy_dy = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DvyDy, index,
                        DerivativeAxis::Y, h2i, y,
                        cpu_staggered_derivative_at(
                            wavefield.vy_m_s, grid, target,
                            DerivativeAxis::Y, h2i));
                    const double dvz_dz = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DvzDz, index,
                        DerivativeAxis::Z, h2i, z,
                        cpu_staggered_derivative_at(
                            wavefield.vz_m_s, grid, target,
                            DerivativeAxis::Z, h2i));
                    const double lambda = coefficients.lambda_pa[index];
                    const double mu = coefficients.shear_modulus_pa[index];
                    wavefield.sxx_pa[index] = detail::checked_wavefield_update(
                        wavefield.sxx_pa[index],
                        dt_s * ((lambda + 2.0 * mu) * dvx_dx +
                                lambda * (dvy_dy + dvz_dz)), "sxx CPML");
                    wavefield.syy_pa[index] = detail::checked_wavefield_update(
                        wavefield.syy_pa[index],
                        dt_s * ((lambda + 2.0 * mu) * dvy_dy +
                                lambda * (dvx_dx + dvz_dz)), "syy CPML");
                    wavefield.szz_pa[index] = detail::checked_wavefield_update(
                        wavefield.szz_pa[index],
                        dt_s * ((lambda + 2.0 * mu) * dvz_dz +
                                lambda * (dvx_dx + dvy_dy)), "szz CPML");
                }
                if (x >= i2h_x.begin && x < i2h_x.end &&
                    y >= i2h_y.begin && y < i2h_y.end) {
                    const double dvx_dy = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DvxDy, index,
                        DerivativeAxis::Y, i2h, y,
                        cpu_staggered_derivative_at(
                            wavefield.vx_m_s, grid, target,
                            DerivativeAxis::Y, i2h));
                    const double dvy_dx = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DvyDx, index,
                        DerivativeAxis::X, i2h, x,
                        cpu_staggered_derivative_at(
                            wavefield.vy_m_s, grid, target,
                            DerivativeAxis::X, i2h));
                    wavefield.sxy_pa[index] = detail::checked_wavefield_update(
                        wavefield.sxy_pa[index],
                        dt_s * coefficients.shear_modulus_xy_pa[index] *
                            (dvx_dy + dvy_dx), "sxy CPML");
                }
                if (x >= i2h_x.begin && x < i2h_x.end &&
                    z >= i2h_z.begin && z < i2h_z.end) {
                    const double dvx_dz = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DvxDz, index,
                        DerivativeAxis::Z, i2h, z,
                        cpu_staggered_derivative_at(
                            wavefield.vx_m_s, grid, target,
                            DerivativeAxis::Z, i2h));
                    const double dvz_dx = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DvzDx, index,
                        DerivativeAxis::X, i2h, x,
                        cpu_staggered_derivative_at(
                            wavefield.vz_m_s, grid, target,
                            DerivativeAxis::X, i2h));
                    wavefield.sxz_pa[index] = detail::checked_wavefield_update(
                        wavefield.sxz_pa[index],
                        dt_s * coefficients.shear_modulus_xz_pa[index] *
                            (dvx_dz + dvz_dx), "sxz CPML");
                }
                if (y >= i2h_y.begin && y < i2h_y.end &&
                    z >= i2h_z.begin && z < i2h_z.end) {
                    const double dvy_dz = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DvyDz, index,
                        DerivativeAxis::Z, i2h, z,
                        cpu_staggered_derivative_at(
                            wavefield.vy_m_s, grid, target,
                            DerivativeAxis::Z, i2h));
                    const double dvz_dy = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DvzDy, index,
                        DerivativeAxis::Y, i2h, y,
                        cpu_staggered_derivative_at(
                            wavefield.vz_m_s, grid, target,
                            DerivativeAxis::Y, i2h));
                    wavefield.syz_pa[index] = detail::checked_wavefield_update(
                        wavefield.syz_pa[index],
                        dt_s * coefficients.shear_modulus_yz_pa[index] *
                            (dvy_dz + dvz_dy), "syz CPML");
                }
            }
        }
    }
}

inline void cpu_update_elastic_velocities_cpml(
    ElasticWavefield& wavefield,
    const ElasticCoefficients& coefficients,
    const CpmlProfile& profile,
    CpmlState& state) {
    detail::require_cpml_layout(profile, state, wavefield, coefficients);
    const auto& grid = wavefield.grid;
    constexpr auto i2h = StaggeredDerivativeMapping::IntegerToHalf;
    constexpr auto h2i = StaggeredDerivativeMapping::HalfToInteger;
    const auto i2h_x = complete_stencil_target_range(grid, DerivativeAxis::X, i2h);
    const auto i2h_y = complete_stencil_target_range(grid, DerivativeAxis::Y, i2h);
    const auto i2h_z = complete_stencil_target_range(grid, DerivativeAxis::Z, i2h);
    const auto h2i_x = complete_stencil_target_range(grid, DerivativeAxis::X, h2i);
    const auto h2i_y = complete_stencil_target_range(grid, DerivativeAxis::Y, h2i);
    const auto h2i_z = complete_stencil_target_range(grid, DerivativeAxis::Z, h2i);
    const double dt_s = profile.parameters.dt_s;

    for (std::size_t z = 0; z < grid.allocated_nz(); ++z) {
        for (std::size_t y = 0; y < grid.allocated_ny(); ++y) {
            for (std::size_t x = 0; x < grid.allocated_nx(); ++x) {
                const StorageIndex3D target{x, y, z};
                const auto index = grid.linear_index(x, y, z);
                if (x >= i2h_x.begin && x < i2h_x.end &&
                    y >= h2i_y.begin && y < h2i_y.end &&
                    z >= h2i_z.begin && z < h2i_z.end) {
                    const double dsxx_dx = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DsxxDx, index,
                        DerivativeAxis::X, i2h, x,
                        cpu_staggered_derivative_at(
                            wavefield.sxx_pa, grid, target,
                            DerivativeAxis::X, i2h));
                    const double dsxy_dy = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DsxyDy, index,
                        DerivativeAxis::Y, h2i, y,
                        cpu_staggered_derivative_at(
                            wavefield.sxy_pa, grid, target,
                            DerivativeAxis::Y, h2i));
                    const double dsxz_dz = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DsxzDz, index,
                        DerivativeAxis::Z, h2i, z,
                        cpu_staggered_derivative_at(
                            wavefield.sxz_pa, grid, target,
                            DerivativeAxis::Z, h2i));
                    wavefield.vx_m_s[index] = detail::checked_wavefield_update(
                        wavefield.vx_m_s[index],
                        dt_s * coefficients.buoyancy_x_m3_kg[index] *
                            (dsxx_dx + dsxy_dy + dsxz_dz), "vx CPML");
                }
                if (x >= h2i_x.begin && x < h2i_x.end &&
                    y >= i2h_y.begin && y < i2h_y.end &&
                    z >= h2i_z.begin && z < h2i_z.end) {
                    const double dsxy_dx = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DsxyDx, index,
                        DerivativeAxis::X, h2i, x,
                        cpu_staggered_derivative_at(
                            wavefield.sxy_pa, grid, target,
                            DerivativeAxis::X, h2i));
                    const double dsyy_dy = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DsyyDy, index,
                        DerivativeAxis::Y, i2h, y,
                        cpu_staggered_derivative_at(
                            wavefield.syy_pa, grid, target,
                            DerivativeAxis::Y, i2h));
                    const double dsyz_dz = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DsyzDz, index,
                        DerivativeAxis::Z, h2i, z,
                        cpu_staggered_derivative_at(
                            wavefield.syz_pa, grid, target,
                            DerivativeAxis::Z, h2i));
                    wavefield.vy_m_s[index] = detail::checked_wavefield_update(
                        wavefield.vy_m_s[index],
                        dt_s * coefficients.buoyancy_y_m3_kg[index] *
                            (dsxy_dx + dsyy_dy + dsyz_dz), "vy CPML");
                }
                if (x >= h2i_x.begin && x < h2i_x.end &&
                    y >= h2i_y.begin && y < h2i_y.end &&
                    z >= i2h_z.begin && z < i2h_z.end) {
                    const double dsxz_dx = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DsxzDx, index,
                        DerivativeAxis::X, h2i, x,
                        cpu_staggered_derivative_at(
                            wavefield.sxz_pa, grid, target,
                            DerivativeAxis::X, h2i));
                    const double dsyz_dy = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DsyzDy, index,
                        DerivativeAxis::Y, h2i, y,
                        cpu_staggered_derivative_at(
                            wavefield.syz_pa, grid, target,
                            DerivativeAxis::Y, h2i));
                    const double dszz_dz = detail::cpml_corrected_derivative(
                        state, profile, CpmlMemory::DszzDz, index,
                        DerivativeAxis::Z, i2h, z,
                        cpu_staggered_derivative_at(
                            wavefield.szz_pa, grid, target,
                            DerivativeAxis::Z, i2h));
                    wavefield.vz_m_s[index] = detail::checked_wavefield_update(
                        wavefield.vz_m_s[index],
                        dt_s * coefficients.buoyancy_z_m3_kg[index] *
                            (dsxz_dx + dsyz_dy + dszz_dz), "vz CPML");
                }
            }
        }
    }
}

} // namespace wave3d
