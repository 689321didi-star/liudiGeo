#pragma once

#include "wave3d/core/coordinates.hpp"
#include "wave3d/core/simulation_config.hpp"
#include "wave3d/numerics/staggered_grid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wave3d {

inline constexpr double minimum_design_points_per_wavelength = 5.0;
inline constexpr double minimum_design_time_samples_per_period = 20.0;

struct ElasticNumericalReport {
    double cfl_dt_limit_s{0.0};
    double cfl_fraction{0.0};
    std::array<double, 3> shear_points_per_wavelength{};
    double time_samples_per_period{0.0};
    double worst_axis_phase_velocity_ratio{0.0};
    double worst_axis_group_velocity_ratio{0.0};
};

[[nodiscard]] inline double elastic_cfl_dt_limit_s(
    const Grid3D& grid,
    double maximum_vp_m_s,
    double safety_factor) {
    require_valid_grid_geometry(grid);
    if (!std::isfinite(maximum_vp_m_s) || !(maximum_vp_m_s > 0.0)) {
        throw std::invalid_argument("maximum Vp must be finite and positive");
    }
    if (!std::isfinite(safety_factor) || !(safety_factor > 0.0) ||
        !(safety_factor < 1.0)) {
        throw std::invalid_argument(
            "CFL safety factor must lie between zero and one");
    }

    const auto dx = static_cast<double>(grid.dx_m);
    const auto dy = static_cast<double>(grid.dy_m);
    const auto dz = static_cast<double>(grid.dz_m);
    const auto inverse_spacing_norm = std::sqrt(
        1.0 / (dx * dx) + 1.0 / (dy * dy) + 1.0 / (dz * dz));
    return safety_factor /
           (staggered_symbol_max * maximum_vp_m_s * inverse_spacing_norm);
}

[[nodiscard]] inline ElasticNumericalReport elastic_numerical_report(
    const SimulationConfig& config) {
    const auto structural_errors = validate(config);
    if (!structural_errors.empty()) {
        throw std::invalid_argument(structural_errors.front());
    }

    ElasticNumericalReport report{};
    report.cfl_dt_limit_s = elastic_cfl_dt_limit_s(
        config.grid,
        static_cast<double>(config.material.max_vp_m_s),
        config.numerics.cfl_safety_factor);
    report.cfl_fraction = config.time.dt_s / report.cfl_dt_limit_s;

    const auto shear_speed = static_cast<double>(config.material.min_vs_m_s);
    const auto frequency = config.numerics.design_frequency_hz;
    report.shear_points_per_wavelength = {
        shear_speed / (frequency * static_cast<double>(config.grid.dx_m)),
        shear_speed / (frequency * static_cast<double>(config.grid.dy_m)),
        shear_speed / (frequency * static_cast<double>(config.grid.dz_m))};
    report.time_samples_per_period = 1.0 / (frequency * config.time.dt_s);

    const auto worst_points = *std::min_element(
        report.shear_points_per_wavelength.begin(),
        report.shear_points_per_wavelength.end());
    if (worst_points > 2.0) {
        report.worst_axis_phase_velocity_ratio =
            staggered_spatial_phase_velocity_ratio(worst_points);
        report.worst_axis_group_velocity_ratio =
            staggered_spatial_group_velocity_ratio(worst_points);
    }
    return report;
}

[[nodiscard]] inline std::vector<std::string> validate_staggered_elastic(
    const SimulationConfig& config) {
    auto errors = validate(config);
    if (!errors.empty()) {
        return errors;
    }

    const auto report = elastic_numerical_report(config);
    if (config.time.dt_s > report.cfl_dt_limit_s) {
        errors.emplace_back(
            "configured time step exceeds the radius-six elastic CFL limit");
    }

    constexpr std::array<const char*, 3> axes{{"x", "y", "z"}};
    for (std::size_t axis = 0; axis < axes.size(); ++axis) {
        if (report.shear_points_per_wavelength[axis] <
            minimum_design_points_per_wavelength) {
            errors.emplace_back(
                std::string("design band has fewer than five shear-wave ") +
                "points per wavelength along " + axes[axis]);
        }
    }
    if (report.time_samples_per_period <
        minimum_design_time_samples_per_period) {
        errors.emplace_back(
            "design band has fewer than twenty time samples per period");
    }
    return errors;
}

[[nodiscard]] inline std::string resolved_elastic_numerical_metadata(
    const SimulationConfig& config) {
    const auto report = elastic_numerical_report(config);
    std::ostringstream output;
    output << std::setprecision(12)
           << "elastic_operator=staggered_standard"
           << ", radius=" << staggered_fd_radius
           << ", spatial_order=" << staggered_fd_spatial_order
           << ", time_order=2"
           << ", cfl_safety=" << config.numerics.cfl_safety_factor
           << ", cfl_dt_limit_s=" << report.cfl_dt_limit_s
           << ", cfl_fraction=" << report.cfl_fraction
           << ", design_frequency_hz="
           << config.numerics.design_frequency_hz
           << ", shear_ppw_xyz=("
           << report.shear_points_per_wavelength[0] << ','
           << report.shear_points_per_wavelength[1] << ','
           << report.shear_points_per_wavelength[2] << ')'
           << ", time_samples_per_period="
           << report.time_samples_per_period
           << ", worst_axis_phase_error_percent="
           << 100.0 * (report.worst_axis_phase_velocity_ratio - 1.0)
           << ", worst_axis_group_error_percent="
           << 100.0 * (report.worst_axis_group_velocity_ratio - 1.0);
    return output.str();
}

} // namespace wave3d
