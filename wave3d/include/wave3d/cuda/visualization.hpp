#pragma once

#include "wave3d/core/grid.hpp"
#include "wave3d/cuda/device_buffer.hpp"
#include "wave3d/cuda/elastic_propagator.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace wave3d::cuda {

enum class VisualizationField {
    Vx,
    Vy,
    Vz,
    Speed,
    Divergence,
    CurlMagnitude
};

class DeviceVisualizationVolume {
public:
    explicit DeviceVisualizationVolume(const Grid3D& physical_grid)
        : grid_(validated_grid(physical_grid)),
          values_(grid_.physical_cell_count()) {}

    DeviceVisualizationVolume(const DeviceVisualizationVolume&) = delete;
    DeviceVisualizationVolume& operator=(const DeviceVisualizationVolume&) =
        delete;
    DeviceVisualizationVolume(DeviceVisualizationVolume&&) noexcept = default;
    DeviceVisualizationVolume& operator=(
        DeviceVisualizationVolume&&) noexcept = default;

    [[nodiscard]] const Grid3D& grid() const noexcept { return grid_; }
    [[nodiscard]] std::size_t value_count() const noexcept {
        return values_.size();
    }
    [[nodiscard]] std::size_t bytes() const { return values_.bytes(); }
    [[nodiscard]] const float* data() const noexcept { return values_.get(); }

    void download(std::vector<float>& destination) const {
        if (destination.size() != values_.size()) {
            throw std::invalid_argument(
                "visualization host volume has an incorrect size");
        }
        values_.copy_to_host(destination.data(), destination.size());
    }

    void download(float* destination, std::size_t value_count) const {
        if (value_count != values_.size()) {
            throw std::invalid_argument(
                "visualization host volume has an incorrect size");
        }
        values_.copy_to_host(destination, value_count);
    }

private:
    friend void extract_physical_visualization_volume(
        const DeviceElasticWavefieldConstView&,
        VisualizationField,
        DeviceVisualizationVolume&);

    [[nodiscard]] static Grid3D validated_grid(const Grid3D& grid) {
        require_valid_grid_geometry(grid);
        return grid;
    }

    Grid3D grid_{};
    DeviceBuffer<float> values_;
};

void extract_physical_visualization_volume(
    const DeviceElasticWavefieldConstView& wavefield,
    VisualizationField field,
    DeviceVisualizationVolume& destination);

} // namespace wave3d::cuda
