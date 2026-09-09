#pragma once

#include "wave3d/wave/elastic_wavefield.hpp"

#include <cstddef>
#include <stdexcept>

namespace wave3d {

struct TractionFreeSurface {
    Grid3D grid{};
    std::size_t surface_storage_z{0};
    std::size_t ghost_depth{0};
};

[[nodiscard]] inline TractionFreeSurface prepare_traction_free_surface(
    const Grid3D& grid) {
    require_valid_grid_geometry(grid);
    if (grid.z_boundary.lower_absorbing != 0) {
        throw std::invalid_argument(
            "traction-free top requires zero lower-z absorbing width");
    }
    if (grid.halo < 6) {
        throw std::invalid_argument(
            "traction-free top requires at least six ghost cells");
    }
    return {grid, grid.physical_origin_z(), 6};
}

inline void require_free_surface_layout(
    const TractionFreeSurface& surface,
    const ElasticWavefield& wavefield) {
    require_valid_grid_geometry(surface.grid);
    if (surface.grid.z_boundary.lower_absorbing != 0 ||
        surface.surface_storage_z != surface.grid.physical_origin_z() ||
        surface.ghost_depth != 6 ||
        surface.surface_storage_z < surface.ghost_depth ||
        surface.surface_storage_z + surface.ghost_depth >=
            surface.grid.allocated_nz()) {
        throw std::invalid_argument(
            "traction-free surface metadata is invalid");
    }
    require_valid_elastic_wavefield_layout(wavefield);
    if (!same_grid_geometry(surface.grid, wavefield.grid)) {
        throw std::invalid_argument(
            "traction-free surface metadata does not match wavefield");
    }
}

inline void require_valid_traction_free_surface(
    const TractionFreeSurface& surface) {
    require_valid_grid_geometry(surface.grid);
    if (surface.grid.z_boundary.lower_absorbing != 0 ||
        surface.surface_storage_z != surface.grid.physical_origin_z() ||
        surface.ghost_depth != 6 ||
        surface.surface_storage_z < surface.ghost_depth ||
        surface.surface_storage_z + surface.ghost_depth >=
            surface.grid.allocated_nz()) {
        throw std::invalid_argument(
            "traction-free surface metadata is invalid");
    }
}

inline void apply_traction_free_stresses(
    ElasticWavefield& wavefield,
    const TractionFreeSurface& surface) {
    require_free_surface_layout(surface, wavefield);
    const auto& grid = wavefield.grid;
    const auto k0 = surface.surface_storage_z;
    for (std::size_t y = 0; y < grid.allocated_ny(); ++y) {
        for (std::size_t x = 0; x < grid.allocated_nx(); ++x) {
            const auto surface_index = grid.linear_index(x, y, k0);
            wavefield.szz_pa[surface_index] = 0.0F;
            for (std::size_t depth = 1; depth <= surface.ghost_depth; ++depth) {
                const auto upper = grid.linear_index(x, y, k0 - depth);
                const auto lower = grid.linear_index(x, y, k0 + depth);
                wavefield.szz_pa[upper] = -wavefield.szz_pa[lower];
                wavefield.sxx_pa[upper] = wavefield.sxx_pa[lower];
                wavefield.syy_pa[upper] = wavefield.syy_pa[lower];
                wavefield.sxy_pa[upper] = wavefield.sxy_pa[lower];
            }
            for (std::size_t depth = 0; depth < surface.ghost_depth; ++depth) {
                const auto upper = grid.linear_index(x, y, k0 - 1 - depth);
                const auto lower = grid.linear_index(x, y, k0 + depth);
                wavefield.sxz_pa[upper] = -wavefield.sxz_pa[lower];
                wavefield.syz_pa[upper] = -wavefield.syz_pa[lower];
            }
        }
    }
}

inline void apply_traction_free_velocities(
    ElasticWavefield& wavefield,
    const TractionFreeSurface& surface) {
    require_free_surface_layout(surface, wavefield);
    const auto& grid = wavefield.grid;
    const auto k0 = surface.surface_storage_z;
    for (std::size_t y = 0; y < grid.allocated_ny(); ++y) {
        for (std::size_t x = 0; x < grid.allocated_nx(); ++x) {
            for (std::size_t depth = 1; depth <= surface.ghost_depth; ++depth) {
                const auto upper = grid.linear_index(x, y, k0 - depth);
                const auto lower = grid.linear_index(x, y, k0 + depth);
                wavefield.vx_m_s[upper] = wavefield.vx_m_s[lower];
                wavefield.vy_m_s[upper] = wavefield.vy_m_s[lower];
            }
            for (std::size_t depth = 0; depth < surface.ghost_depth; ++depth) {
                const auto upper = grid.linear_index(x, y, k0 - 1 - depth);
                const auto lower = grid.linear_index(x, y, k0 + depth);
                wavefield.vz_m_s[upper] = wavefield.vz_m_s[lower];
            }
        }
    }
}

} // namespace wave3d
