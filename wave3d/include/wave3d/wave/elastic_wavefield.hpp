#pragma once

#include "wave3d/core/coordinates.hpp"

#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace wave3d {

namespace detail {

[[nodiscard]] inline std::size_t validated_wavefield_cell_count(
    const Grid3D& grid) {
    require_valid_grid_geometry(grid);
    return grid.allocated_cell_count();
}

} // namespace detail

struct ElasticWavefield {
    Grid3D grid{};
    std::vector<float> vx_m_s;
    std::vector<float> vy_m_s;
    std::vector<float> vz_m_s;
    std::vector<float> sxx_pa;
    std::vector<float> syy_pa;
    std::vector<float> szz_pa;
    std::vector<float> sxy_pa;
    std::vector<float> sxz_pa;
    std::vector<float> syz_pa;

    explicit ElasticWavefield(const Grid3D& source_grid)
        : grid(source_grid),
          vx_m_s(detail::validated_wavefield_cell_count(source_grid)),
          vy_m_s(vx_m_s.size()),
          vz_m_s(vx_m_s.size()),
          sxx_pa(vx_m_s.size()),
          syy_pa(vx_m_s.size()),
          szz_pa(vx_m_s.size()),
          sxy_pa(vx_m_s.size()),
          sxz_pa(vx_m_s.size()),
          syz_pa(vx_m_s.size()) {}

    ElasticWavefield(const ElasticWavefield&) = delete;
    ElasticWavefield& operator=(const ElasticWavefield&) = delete;
    ElasticWavefield(ElasticWavefield&&) noexcept = default;
    ElasticWavefield& operator=(ElasticWavefield&&) noexcept = default;

    [[nodiscard]] std::size_t cell_count() const noexcept {
        return vx_m_s.size();
    }
};

inline void require_valid_elastic_wavefield_layout(
    const ElasticWavefield& wavefield) {
    require_valid_grid_geometry(wavefield.grid);
    const auto cells = wavefield.grid.allocated_cell_count();
    for (const auto size : {
             wavefield.vx_m_s.size(),
             wavefield.vy_m_s.size(),
             wavefield.vz_m_s.size(),
             wavefield.sxx_pa.size(),
             wavefield.syy_pa.size(),
             wavefield.szz_pa.size(),
             wavefield.sxy_pa.size(),
             wavefield.sxz_pa.size(),
             wavefield.syz_pa.size()}) {
        if (size != cells) {
            throw std::invalid_argument(
                "elastic wavefield array size does not match allocated grid");
        }
    }
}

} // namespace wave3d
