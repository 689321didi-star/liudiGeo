#pragma once

#include "wave3d/core/grid.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace wave3d {

enum class TopBoundary {
    Absorbing,
    FreeSurface,
};

struct TimeConfig {
    double dt_s{0.0};
    double total_time_s{0.0};

    [[nodiscard]] std::size_t step_count() const {
        if (!std::isfinite(dt_s) || !std::isfinite(total_time_s) ||
            !(dt_s > 0.0F) || !(total_time_s > 0.0F)) {
            throw std::invalid_argument(
                "dt_s and total_time_s must be finite and positive");
        }
        const auto steps = std::ceil(
            static_cast<long double>(total_time_s) /
            static_cast<long double>(dt_s));
        if (steps > static_cast<long double>(
                        std::numeric_limits<std::size_t>::max())) {
            throw std::overflow_error("Wave3D time-step count overflows size_t");
        }
        return static_cast<std::size_t>(steps);
    }
};

struct MaterialExtrema {
    float min_vp_m_s{0.0F};
    float max_vp_m_s{0.0F};
    float min_vs_m_s{0.0F};
    float max_vs_m_s{0.0F};
    float min_density_kg_m3{0.0F};
    float max_density_kg_m3{0.0F};
};

struct NumericalConfig {
    double cfl_safety_factor{0.9};
    double design_frequency_hz{0.0};
};

struct SimulationConfig {
    Grid3D grid{};
    TimeConfig time{};
    MaterialExtrema material{};
    TopBoundary top_boundary{TopBoundary::FreeSurface};
    NumericalConfig numerics{};
};

[[nodiscard]] inline std::vector<std::string> validate(
    const SimulationConfig& config) {
    std::vector<std::string> errors;
    const auto& grid = config.grid;
    const auto& material = config.material;

    if (grid.nx == 0 || grid.ny == 0 || grid.nz == 0) {
        errors.emplace_back("grid dimensions must be positive");
    }
    if (!std::isfinite(grid.dx_m) || !std::isfinite(grid.dy_m) ||
        !std::isfinite(grid.dz_m) || !(grid.dx_m > 0.0F) ||
        !(grid.dy_m > 0.0F) || !(grid.dz_m > 0.0F)) {
        errors.emplace_back("grid spacing must be finite and positive");
    }
    if (grid.halo == 0) {
        errors.emplace_back("halo width must be positive");
    }
    if (!std::isfinite(config.time.dt_s) ||
        !std::isfinite(config.time.total_time_s) ||
        !(config.time.dt_s > 0.0F) || !(config.time.total_time_s > 0.0F)) {
        errors.emplace_back("time step and total time must be finite and positive");
    }
    if (!std::isfinite(material.min_vp_m_s) ||
        !std::isfinite(material.max_vp_m_s) ||
        !(material.min_vp_m_s > 0.0F) ||
        !(material.max_vp_m_s >= material.min_vp_m_s)) {
        errors.emplace_back("Vp bounds are invalid");
    }
    if (!std::isfinite(material.min_vs_m_s) ||
        !std::isfinite(material.max_vs_m_s) ||
        !(material.min_vs_m_s > 0.0F) ||
        !(material.max_vs_m_s >= material.min_vs_m_s)) {
        errors.emplace_back("Vs bounds are invalid");
    }
    if (!std::isfinite(material.min_density_kg_m3) ||
        !std::isfinite(material.max_density_kg_m3) ||
        !(material.min_density_kg_m3 > 0.0F) ||
        !(material.max_density_kg_m3 >= material.min_density_kg_m3)) {
        errors.emplace_back("density bounds are invalid");
    }
    if (config.top_boundary == TopBoundary::FreeSurface &&
        grid.z_boundary.lower_absorbing != 0) {
        errors.emplace_back("a free top surface cannot have a top absorbing layer");
    }
    if (config.top_boundary == TopBoundary::Absorbing &&
        grid.z_boundary.lower_absorbing == 0) {
        errors.emplace_back("an absorbing top boundary requires a non-zero layer");
    }
    if (!std::isfinite(config.numerics.cfl_safety_factor) ||
        !(config.numerics.cfl_safety_factor > 0.0) ||
        !(config.numerics.cfl_safety_factor < 1.0)) {
        errors.emplace_back(
            "CFL safety factor must lie between zero and one");
    }
    if (!std::isfinite(config.numerics.design_frequency_hz) ||
        !(config.numerics.design_frequency_hz > 0.0)) {
        errors.emplace_back("design frequency must be finite and positive");
    }

    try {
        static_cast<void>(grid.allocated_cell_count());
        static_cast<void>(config.time.step_count());
    } catch (const std::exception& error) {
        errors.emplace_back(error.what());
    }
    return errors;
}

} // namespace wave3d
