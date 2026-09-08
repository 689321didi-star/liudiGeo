#pragma once

#include "wave3d/core/checked_size.hpp"
#include "wave3d/core/coordinates.hpp"
#include "wave3d/core/grid.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace wave3d {

struct ElasticMaterial {
    float vp_m_s{0.0F};
    float vs_m_s{0.0F};
    float density_kg_m3{0.0F};
};

struct HorizontalLayer {
    double top_depth_m{0.0};
    ElasticMaterial material{};
};

struct PhysicalModel {
    Grid3D grid{};
    std::vector<float> vp_m_s;
    std::vector<float> vs_m_s;
    std::vector<float> density_kg_m3;

    [[nodiscard]] std::size_t cell_count() const noexcept {
        return vp_m_s.size();
    }
};

struct PhysicalModelExtrema {
    ElasticMaterial minimum{};
    ElasticMaterial maximum{};
};

inline void require_valid_elastic_material(const ElasticMaterial& material) {
    if (!std::isfinite(material.vp_m_s) || !(material.vp_m_s > 0.0F)) {
        throw std::invalid_argument("Vp must be finite and positive");
    }
    if (!std::isfinite(material.vs_m_s) || !(material.vs_m_s >= 0.0F)) {
        throw std::invalid_argument("Vs must be finite and non-negative");
    }
    if (!std::isfinite(material.density_kg_m3) ||
        !(material.density_kg_m3 > 0.0F)) {
        throw std::invalid_argument("density must be finite and positive");
    }
    if (!(material.vp_m_s > material.vs_m_s)) {
        throw std::invalid_argument("Vp must be greater than Vs at every cell");
    }
}

[[nodiscard]] inline std::vector<std::string> validate(
    const PhysicalModel& model) {
    std::vector<std::string> errors;
    std::size_t expected_cells = 0;
    try {
        require_valid_grid_geometry(model.grid);
        expected_cells = model.grid.physical_cell_count();
    } catch (const std::exception& error) {
        errors.emplace_back(error.what());
        return errors;
    }

    if (model.vp_m_s.size() != expected_cells) {
        errors.emplace_back("Vp array size does not match the physical grid");
    }
    if (model.vs_m_s.size() != expected_cells) {
        errors.emplace_back("Vs array size does not match the physical grid");
    }
    if (model.density_kg_m3.size() != expected_cells) {
        errors.emplace_back("density array size does not match the physical grid");
    }
    if (!errors.empty()) {
        return errors;
    }

    for (std::size_t index = 0; index < expected_cells; ++index) {
        try {
            require_valid_elastic_material(
                {model.vp_m_s[index],
                 model.vs_m_s[index],
                 model.density_kg_m3[index]});
        } catch (const std::exception& error) {
            errors.emplace_back(
                "invalid material at physical linear index " +
                std::to_string(index) + ": " + error.what());
            break;
        }
    }
    return errors;
}

inline void require_valid_physical_model(const PhysicalModel& model) {
    const auto errors = validate(model);
    if (!errors.empty()) {
        throw std::invalid_argument(errors.front());
    }
}

[[nodiscard]] inline PhysicalModelExtrema physical_model_extrema(
    const PhysicalModel& model) {
    require_valid_physical_model(model);
    const auto vp = std::minmax_element(model.vp_m_s.begin(), model.vp_m_s.end());
    const auto vs = std::minmax_element(model.vs_m_s.begin(), model.vs_m_s.end());
    const auto density = std::minmax_element(
        model.density_kg_m3.begin(), model.density_kg_m3.end());
    return {
        {*vp.first, *vs.first, *density.first},
        {*vp.second, *vs.second, *density.second}};
}

[[nodiscard]] inline PhysicalModel make_homogeneous_model(
    const Grid3D& grid,
    const ElasticMaterial& material) {
    require_valid_grid_geometry(grid);
    require_valid_elastic_material(material);
    const auto cells = grid.physical_cell_count();
    return {
        grid,
        std::vector<float>(cells, material.vp_m_s),
        std::vector<float>(cells, material.vs_m_s),
        std::vector<float>(cells, material.density_kg_m3)};
}

[[nodiscard]] inline PhysicalModel make_horizontal_layered_model(
    const Grid3D& grid,
    const std::vector<HorizontalLayer>& layers) {
    require_valid_grid_geometry(grid);
    if (layers.empty()) {
        throw std::invalid_argument("horizontal model requires at least one layer");
    }
    if (!std::isfinite(layers.front().top_depth_m) ||
        layers.front().top_depth_m != 0.0) {
        throw std::invalid_argument("first horizontal layer must start at z=0");
    }

    const auto maximum = physical_domain_max(grid);
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const auto& layer = layers[index];
        if (!std::isfinite(layer.top_depth_m) || layer.top_depth_m < 0.0 ||
            layer.top_depth_m > maximum.z_m) {
            throw std::invalid_argument("horizontal layer depth is outside the model");
        }
        if (index != 0 &&
            !(layer.top_depth_m > layers[index - 1].top_depth_m)) {
            throw std::invalid_argument(
                "horizontal layer depths must be strictly increasing");
        }
        require_valid_elastic_material(layer.material);
    }

    const auto cells = grid.physical_cell_count();
    PhysicalModel model{
        grid,
        std::vector<float>(cells),
        std::vector<float>(cells),
        std::vector<float>(cells)};
    const auto plane_cells = detail::checked_size_product(
        grid.nx, grid.ny, "Wave3D physical plane size overflows size_t");

    std::size_t layer_index = 0;
    for (std::size_t z = 0; z < grid.nz; ++z) {
        const auto depth =
            static_cast<double>(z) * static_cast<double>(grid.dz_m);
        while (layer_index + 1 < layers.size() &&
               depth >= layers[layer_index + 1].top_depth_m) {
            ++layer_index;
        }
        const auto offset = detail::checked_size_product(
            z, plane_cells, "Wave3D physical layer offset overflows size_t");
        const auto end = offset + plane_cells;
        const auto& material = layers[layer_index].material;
        std::fill(
            model.vp_m_s.begin() + static_cast<std::ptrdiff_t>(offset),
            model.vp_m_s.begin() + static_cast<std::ptrdiff_t>(end),
            material.vp_m_s);
        std::fill(
            model.vs_m_s.begin() + static_cast<std::ptrdiff_t>(offset),
            model.vs_m_s.begin() + static_cast<std::ptrdiff_t>(end),
            material.vs_m_s);
        std::fill(
            model.density_kg_m3.begin() + static_cast<std::ptrdiff_t>(offset),
            model.density_kg_m3.begin() + static_cast<std::ptrdiff_t>(end),
            material.density_kg_m3);
    }
    return model;
}

} // namespace wave3d
