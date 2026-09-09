#pragma once

#include "wave3d/core/grid.hpp"
#include "wave3d/wave/elastic_wavefield.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace wave3d {

struct SpongeProfile {
    Grid3D grid{};
    float outer_damping_per_application{0.0F};
    std::vector<float> damping;

    [[nodiscard]] std::size_t bytes() const noexcept {
        return damping.size() * sizeof(float);
    }
};

namespace detail {

[[nodiscard]] inline double sponge_axis_factor(
    std::size_t coordinate,
    std::size_t physical_origin,
    std::size_t physical_count,
    const AxisBoundary& boundary,
    double logarithmic_outer_damping) {
    if (coordinate < physical_origin && boundary.lower_absorbing != 0) {
        const auto depth = std::min(
            physical_origin - coordinate,
            boundary.lower_absorbing);
        const double normalized = static_cast<double>(depth) /
                                  static_cast<double>(boundary.lower_absorbing);
        return std::exp(logarithmic_outer_damping * normalized * normalized);
    }

    const auto physical_last = physical_origin + physical_count - 1;
    if (coordinate > physical_last && boundary.upper_absorbing != 0) {
        const auto depth = std::min(
            coordinate - physical_last,
            boundary.upper_absorbing);
        const double normalized = static_cast<double>(depth) /
                                  static_cast<double>(boundary.upper_absorbing);
        return std::exp(logarithmic_outer_damping * normalized * normalized);
    }
    return 1.0;
}

inline void require_valid_sponge_fields(
    const SpongeProfile& profile,
    const ElasticWavefield& wavefield,
    std::initializer_list<const std::vector<float>*> fields) {
    require_valid_grid_geometry(profile.grid);
    require_valid_elastic_wavefield_layout(wavefield);
    if (!same_grid_geometry(profile.grid, wavefield.grid)) {
        throw std::invalid_argument(
            "sponge profile and elastic wavefield grids must match");
    }
    if (profile.damping.size() != wavefield.cell_count()) {
        throw std::invalid_argument(
            "sponge profile size does not match allocated grid");
    }
    if (!std::isfinite(profile.outer_damping_per_application) ||
        !(profile.outer_damping_per_application > 0.0F) ||
        profile.outer_damping_per_application > 1.0F) {
        throw std::invalid_argument(
            "sponge outer damping must be finite and in (0, 1]");
    }
    for (std::size_t index = 0; index < profile.damping.size(); ++index) {
        const float damping = profile.damping[index];
        if (!std::isfinite(damping) || !(damping > 0.0F) || damping > 1.0F) {
            throw std::invalid_argument(
                "sponge coefficients must be finite and in (0, 1]");
        }
        for (const auto* field : fields) {
            if (!std::isfinite((*field)[index])) {
                throw std::invalid_argument(
                    "sponge cannot be applied to a non-finite wavefield");
            }
        }
    }
}

inline void apply_sponge_fields(
    const SpongeProfile& profile,
    std::initializer_list<std::vector<float>*> fields) noexcept {
    for (std::size_t index = 0; index < profile.damping.size(); ++index) {
        for (auto* field : fields) {
            (*field)[index] *= profile.damping[index];
        }
    }
}

} // namespace detail

[[nodiscard]] inline SpongeProfile prepare_sponge_profile(
    const Grid3D& grid,
    double outer_damping_per_application) {
    require_valid_grid_geometry(grid);
    if (!std::isfinite(outer_damping_per_application) ||
        !(outer_damping_per_application > 0.0) ||
        outer_damping_per_application > 1.0) {
        throw std::invalid_argument(
            "sponge outer damping must be finite and in (0, 1]");
    }

    SpongeProfile profile{
        grid,
        static_cast<float>(outer_damping_per_application),
        std::vector<float>(grid.allocated_cell_count())};
    const double logarithmic_outer_damping =
        std::log(outer_damping_per_application);
    for (std::size_t z = 0; z < grid.allocated_nz(); ++z) {
        const double z_factor = detail::sponge_axis_factor(
            z,
            grid.physical_origin_z(),
            grid.nz,
            grid.z_boundary,
            logarithmic_outer_damping);
        for (std::size_t y = 0; y < grid.allocated_ny(); ++y) {
            const double y_factor = detail::sponge_axis_factor(
                y,
                grid.physical_origin_y(),
                grid.ny,
                grid.y_boundary,
                logarithmic_outer_damping);
            for (std::size_t x = 0; x < grid.allocated_nx(); ++x) {
                const double x_factor = detail::sponge_axis_factor(
                    x,
                    grid.physical_origin_x(),
                    grid.nx,
                    grid.x_boundary,
                    logarithmic_outer_damping);
                profile.damping[grid.linear_index(x, y, z)] =
                    static_cast<float>(x_factor * y_factor * z_factor);
            }
        }
    }
    return profile;
}

inline void apply_sponge_to_stresses(
    ElasticWavefield& wavefield,
    const SpongeProfile& profile) {
    detail::require_valid_sponge_fields(
        profile,
        wavefield,
        {&wavefield.sxx_pa,
         &wavefield.syy_pa,
         &wavefield.szz_pa,
         &wavefield.sxy_pa,
         &wavefield.sxz_pa,
         &wavefield.syz_pa});
    detail::apply_sponge_fields(
        profile,
        {&wavefield.sxx_pa,
         &wavefield.syy_pa,
         &wavefield.szz_pa,
         &wavefield.sxy_pa,
         &wavefield.sxz_pa,
         &wavefield.syz_pa});
}

inline void apply_sponge_to_velocities(
    ElasticWavefield& wavefield,
    const SpongeProfile& profile) {
    detail::require_valid_sponge_fields(
        profile,
        wavefield,
        {&wavefield.vx_m_s, &wavefield.vy_m_s, &wavefield.vz_m_s});
    detail::apply_sponge_fields(
        profile,
        {&wavefield.vx_m_s, &wavefield.vy_m_s, &wavefield.vz_m_s});
}

} // namespace wave3d
