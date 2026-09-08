#pragma once

#include "wave3d/acquisition/source.hpp"
#include "wave3d/acquisition/trilinear_stencil.hpp"
#include "wave3d/wave/elastic_wavefield.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace wave3d {

struct PreparedMomentTensorSource {
    Grid3D grid{};
    MomentTensorSource source{};
    TrilinearStencil sxx;
    TrilinearStencil syy;
    TrilinearStencil szz;
    TrilinearStencil sxy;
    TrilinearStencil sxz;
    TrilinearStencil syz;
};

namespace detail {

inline void require_source_storage_location_matches_grid(
    const Grid3D& grid,
    const MomentTensorSource& source) {
    const auto expected =
        physical_to_storage_coordinate(grid, source.physical_location);
    if (source.storage_location.x != expected.x ||
        source.storage_location.y != expected.y ||
        source.storage_location.z != expected.z) {
        throw std::invalid_argument(
            "source storage coordinate does not match its preparation grid");
    }
}

inline void require_source_stencil_lattice(
    const TrilinearStencil& stencil,
    ElasticLattice expected) {
    if (stencil.lattice != expected) {
        throw std::invalid_argument(
            "moment-source stencil uses the wrong staggered lattice");
    }
}

[[nodiscard]] inline std::array<float, 8> prepare_source_component_update(
    const std::vector<float>& field,
    const TrilinearStencil& stencil,
    double full_component_increment_pa) {
    std::array<float, 8> result{};
    for (std::size_t node_number = 0;
         node_number < stencil.nodes.size();
         ++node_number) {
        const auto& node = stencil.nodes[node_number];
        const double old_value = static_cast<double>(field[node.linear_index]);
        const double updated =
            old_value + full_component_increment_pa * node.weight;
        if (!std::isfinite(old_value) || !std::isfinite(updated) ||
            updated > static_cast<double>(std::numeric_limits<float>::max()) ||
            updated < -static_cast<double>(std::numeric_limits<float>::max())) {
            throw std::overflow_error(
                "moment-source injection is not representable as finite float32");
        }
        result[node_number] = static_cast<float>(updated);
    }
    return result;
}

inline void apply_source_component_update(
    std::vector<float>& field,
    const TrilinearStencil& stencil,
    const std::array<float, 8>& values) noexcept {
    for (std::size_t node_number = 0;
         node_number < stencil.nodes.size();
         ++node_number) {
        field[stencil.nodes[node_number].linear_index] = values[node_number];
    }
}

} // namespace detail

inline void require_valid_prepared_moment_tensor_source(
    const PreparedMomentTensorSource& prepared) {
    require_valid_grid_geometry(prepared.grid);
    require_valid_moment_tensor(prepared.source.moment);
    require_valid_ricker_wavelet(prepared.source.wavelet);
    if (!std::isfinite(prepared.source.origin_time_s) ||
        prepared.source.origin_time_s < 0.0) {
        throw std::invalid_argument(
            "source origin time must be finite and non-negative");
    }
    detail::require_source_storage_location_matches_grid(
        prepared.grid,
        prepared.source);

    detail::require_source_stencil_lattice(
        prepared.sxx,
        ElasticLattice::Integer);
    detail::require_source_stencil_lattice(
        prepared.syy,
        ElasticLattice::Integer);
    detail::require_source_stencil_lattice(
        prepared.szz,
        ElasticLattice::Integer);
    detail::require_source_stencil_lattice(
        prepared.sxy,
        ElasticLattice::XYHalf);
    detail::require_source_stencil_lattice(
        prepared.sxz,
        ElasticLattice::XZHalf);
    detail::require_source_stencil_lattice(
        prepared.syz,
        ElasticLattice::YZHalf);
    require_valid_trilinear_stencil(prepared.grid, prepared.sxx);
    require_valid_trilinear_stencil(prepared.grid, prepared.syy);
    require_valid_trilinear_stencil(prepared.grid, prepared.szz);
    require_valid_trilinear_stencil(prepared.grid, prepared.sxy);
    require_valid_trilinear_stencil(prepared.grid, prepared.sxz);
    require_valid_trilinear_stencil(prepared.grid, prepared.syz);
}

[[nodiscard]] inline PreparedMomentTensorSource
prepare_moment_tensor_source_stencils(
    const Grid3D& grid,
    const MomentTensorSource& source) {
    require_valid_moment_tensor(source.moment);
    require_valid_ricker_wavelet(source.wavelet);
    if (!std::isfinite(source.origin_time_s) || source.origin_time_s < 0.0) {
        throw std::invalid_argument(
            "source origin time must be finite and non-negative");
    }
    detail::require_source_storage_location_matches_grid(grid, source);

    PreparedMomentTensorSource result{
        grid,
        source,
        prepare_trilinear_stencil(
            grid,
            source.storage_location,
            ElasticLattice::Integer),
        prepare_trilinear_stencil(
            grid,
            source.storage_location,
            ElasticLattice::Integer),
        prepare_trilinear_stencil(
            grid,
            source.storage_location,
            ElasticLattice::Integer),
        prepare_trilinear_stencil(
            grid,
            source.storage_location,
            ElasticLattice::XYHalf),
        prepare_trilinear_stencil(
            grid,
            source.storage_location,
            ElasticLattice::XZHalf),
        prepare_trilinear_stencil(
            grid,
            source.storage_location,
            ElasticLattice::YZHalf)};
    require_valid_prepared_moment_tensor_source(result);
    return result;
}

inline void inject_moment_tensor_source(
    ElasticWavefield& wavefield,
    const PreparedMomentTensorSource& prepared,
    std::size_t step_index_n,
    double dt_s) {
    require_valid_elastic_wavefield_layout(wavefield);
    require_valid_prepared_moment_tensor_source(prepared);
    if (!same_grid_geometry(wavefield.grid, prepared.grid)) {
        throw std::invalid_argument(
            "moment source and wavefield grids must match exactly");
    }
    if (!std::isfinite(dt_s) || !(dt_s > 0.0)) {
        throw std::invalid_argument(
            "moment-source time step must be finite and positive");
    }
    const double cell_volume_m3 =
        static_cast<double>(wavefield.grid.dx_m) *
        static_cast<double>(wavefield.grid.dy_m) *
        static_cast<double>(wavefield.grid.dz_m);
    const double stress_sample_time_s =
        static_cast<double>(step_index_n) * dt_s;
    if (!std::isfinite(stress_sample_time_s)) {
        throw std::overflow_error(
            "moment-source stress sample time is not finite");
    }
    const double moment_rate_s_inv =
        source_time_value(prepared.source, stress_sample_time_s);

    const auto component_increment = [&](double moment_nm) {
        const double value =
            -dt_s * moment_nm * moment_rate_s_inv / cell_volume_m3;
        if (!std::isfinite(value)) {
            throw std::overflow_error(
                "moment-source stress increment is not finite");
        }
        return value;
    };

    const auto sxx = detail::prepare_source_component_update(
        wavefield.sxx_pa,
        prepared.sxx,
        component_increment(prepared.source.moment.m_xx_nm));
    const auto syy = detail::prepare_source_component_update(
        wavefield.syy_pa,
        prepared.syy,
        component_increment(prepared.source.moment.m_yy_nm));
    const auto szz = detail::prepare_source_component_update(
        wavefield.szz_pa,
        prepared.szz,
        component_increment(prepared.source.moment.m_zz_nm));
    const auto sxy = detail::prepare_source_component_update(
        wavefield.sxy_pa,
        prepared.sxy,
        component_increment(prepared.source.moment.m_xy_nm));
    const auto sxz = detail::prepare_source_component_update(
        wavefield.sxz_pa,
        prepared.sxz,
        component_increment(prepared.source.moment.m_xz_nm));
    const auto syz = detail::prepare_source_component_update(
        wavefield.syz_pa,
        prepared.syz,
        component_increment(prepared.source.moment.m_yz_nm));

    detail::apply_source_component_update(
        wavefield.sxx_pa,
        prepared.sxx,
        sxx);
    detail::apply_source_component_update(
        wavefield.syy_pa,
        prepared.syy,
        syy);
    detail::apply_source_component_update(
        wavefield.szz_pa,
        prepared.szz,
        szz);
    detail::apply_source_component_update(
        wavefield.sxy_pa,
        prepared.sxy,
        sxy);
    detail::apply_source_component_update(
        wavefield.sxz_pa,
        prepared.sxz,
        sxz);
    detail::apply_source_component_update(
        wavefield.syz_pa,
        prepared.syz,
        syz);
}

} // namespace wave3d
