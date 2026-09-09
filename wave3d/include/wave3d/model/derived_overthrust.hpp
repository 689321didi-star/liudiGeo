#pragma once

#include "wave3d/core/checked_size.hpp"
#include "wave3d/model/physical_model.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace wave3d {

struct PhysicalVolumeWindow3D {
    std::size_t x_begin{0};
    std::size_t y_begin{0};
    std::size_t z_begin{0};
    std::size_t nx{0};
    std::size_t ny{0};
    std::size_t nz{0};
};

struct OutputStorageGeometry {
    std::size_t halo{0};
    AxisBoundary x_boundary{};
    AxisBoundary y_boundary{};
    AxisBoundary z_boundary{};
};

[[nodiscard]] inline PhysicalModel make_derived_overthrust_elastic_model(
    const Grid3D& source_grid,
    const std::vector<float>& source_vp_zyx,
    const PhysicalVolumeWindow3D& window,
    const OutputStorageGeometry& output_storage) {
    require_valid_grid_geometry(source_grid);
    if (source_vp_zyx.size() != source_grid.physical_cell_count()) {
        throw std::invalid_argument(
            "Overthrust Vp size does not match the declared source grid");
    }
    if (window.nx == 0 || window.ny == 0 || window.nz == 0) {
        throw std::invalid_argument(
            "Overthrust crop dimensions must be positive");
    }

    const auto x_end = detail::checked_size_add(
        window.x_begin,
        window.nx,
        "Overthrust crop x range overflows size_t");
    const auto y_end = detail::checked_size_add(
        window.y_begin,
        window.ny,
        "Overthrust crop y range overflows size_t");
    const auto z_end = detail::checked_size_add(
        window.z_begin,
        window.nz,
        "Overthrust crop z range overflows size_t");
    if (x_end > source_grid.nx || y_end > source_grid.ny ||
        z_end > source_grid.nz) {
        throw std::out_of_range(
            "Overthrust crop is outside the declared source grid");
    }

    const Grid3D output_grid{
        window.nx,
        window.ny,
        window.nz,
        source_grid.dx_m,
        source_grid.dy_m,
        source_grid.dz_m,
        output_storage.halo,
        output_storage.x_boundary,
        output_storage.y_boundary,
        output_storage.z_boundary};
    require_valid_grid_geometry(output_grid);

    const auto output_cells = output_grid.physical_cell_count();
    PhysicalModel output{
        output_grid,
        std::vector<float>(output_cells),
        std::vector<float>(output_cells),
        std::vector<float>(output_cells)};

    constexpr double gardner_coefficient = 1000.0 * 0.31;
    const double vp_vs_ratio = std::sqrt(3.0);
    for (std::size_t z = 0; z < window.nz; ++z) {
        for (std::size_t y = 0; y < window.ny; ++y) {
            for (std::size_t x = 0; x < window.nx; ++x) {
                const auto source_index = source_grid.physical_linear_index(
                    window.x_begin + x,
                    window.y_begin + y,
                    window.z_begin + z);
                const float vp = source_vp_zyx[source_index];
                if (!std::isfinite(vp) || !(vp > 0.0F)) {
                    throw std::invalid_argument(
                        "Overthrust source Vp must be finite and positive");
                }

                const double vs_value = static_cast<double>(vp) / vp_vs_ratio;
                const double density_value = gardner_coefficient *
                    std::pow(static_cast<double>(vp), 0.25);
                if (!std::isfinite(vs_value) || !std::isfinite(density_value) ||
                    vs_value > std::numeric_limits<float>::max() ||
                    density_value > std::numeric_limits<float>::max()) {
                    throw std::overflow_error(
                        "derived Overthrust elastic value exceeds float32");
                }

                const ElasticMaterial material{
                    vp,
                    static_cast<float>(vs_value),
                    static_cast<float>(density_value)};
                require_valid_elastic_material(material);
                const auto output_index =
                    output_grid.physical_linear_index(x, y, z);
                output.vp_m_s[output_index] = material.vp_m_s;
                output.vs_m_s[output_index] = material.vs_m_s;
                output.density_kg_m3[output_index] = material.density_kg_m3;
            }
        }
    }

    require_valid_physical_model(output);
    return output;
}

} // namespace wave3d
