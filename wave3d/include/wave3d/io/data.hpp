#pragma once

#include "wave3d/acquisition/source.hpp"
#include "wave3d/core/checked_size.hpp"
#include "wave3d/core/coordinates.hpp"
#include "wave3d/core/simulation_config.hpp"
#include "wave3d/model/physical_model.hpp"
#include "wave3d/numerics/elastic_validation.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace wave3d::io {

inline void require_valid_source_metadata(const MomentTensorSource& source) {
    if (!std::isfinite(source.physical_location.x_m) ||
        !std::isfinite(source.physical_location.y_m) ||
        !std::isfinite(source.physical_location.z_m) ||
        !std::isfinite(source.storage_location.x) ||
        !std::isfinite(source.storage_location.y) ||
        !std::isfinite(source.storage_location.z) ||
        !std::isfinite(source.origin_time_s) || source.origin_time_s < 0.0) {
        throw std::invalid_argument(
            "source coordinates and non-negative origin time must be finite");
    }
    require_valid_moment_tensor(source.moment);
    require_valid_ricker_wavelet(source.wavelet);
}

struct ThreeComponentTraces {
    std::size_t receiver_count{0};
    std::size_t sample_count{0};
    double dt_s{0.0};
    std::vector<PhysicalPoint3D> receiver_coordinates_m;
    MomentTensorSource source{};
    std::vector<float> vx_m_s;
    std::vector<float> vy_m_s;
    std::vector<float> vz_m_s;

    [[nodiscard]] std::size_t linear_index(
        std::size_t receiver,
        std::size_t sample) const {
        if (receiver >= receiver_count || sample >= sample_count) {
            throw std::out_of_range("trace index is outside receiver/sample shape");
        }
        return receiver * sample_count + sample;
    }
};

inline void require_valid_traces(const ThreeComponentTraces& traces) {
    if (traces.receiver_count == 0 || traces.sample_count == 0 ||
        !std::isfinite(traces.dt_s) || !(traces.dt_s > 0.0)) {
        throw std::invalid_argument(
            "trace receiver/sample counts and finite dt must be positive");
    }
    const auto values = detail::checked_size_product(
        traces.receiver_count,
        traces.sample_count,
        "trace value count overflows size_t");
    if (traces.receiver_coordinates_m.size() != traces.receiver_count ||
        traces.vx_m_s.size() != values || traces.vy_m_s.size() != values ||
        traces.vz_m_s.size() != values) {
        throw std::invalid_argument("trace storage does not match declared shape");
    }
    require_valid_source_metadata(traces.source);
    for (const auto& point : traces.receiver_coordinates_m) {
        if (!std::isfinite(point.x_m) || !std::isfinite(point.y_m) ||
            !std::isfinite(point.z_m)) {
            throw std::invalid_argument("receiver coordinates must be finite");
        }
    }
    for (const auto* component : {&traces.vx_m_s, &traces.vy_m_s, &traces.vz_m_s}) {
        for (const float value : *component) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument("trace samples must be finite");
            }
        }
    }
}

struct SparseVelocitySnapshot {
    Grid3D grid{};
    std::size_t step_index{0};
    double time_s{0.0};
    std::vector<StorageIndex3D> storage_indices;
    std::vector<float> vx_m_s;
    std::vector<float> vy_m_s;
    std::vector<float> vz_m_s;
};

inline void require_valid_sparse_snapshot(
    const SparseVelocitySnapshot& snapshot) {
    require_valid_grid_geometry(snapshot.grid);
    if (!std::isfinite(snapshot.time_s) || snapshot.time_s < 0.0 ||
        snapshot.storage_indices.empty() ||
        snapshot.vx_m_s.size() != snapshot.storage_indices.size() ||
        snapshot.vy_m_s.size() != snapshot.storage_indices.size() ||
        snapshot.vz_m_s.size() != snapshot.storage_indices.size()) {
        throw std::invalid_argument("sparse snapshot metadata/shape is invalid");
    }
    for (std::size_t point = 0; point < snapshot.storage_indices.size(); ++point) {
        const auto& index = snapshot.storage_indices[point];
        static_cast<void>(
            snapshot.grid.linear_index(index.x, index.y, index.z));
        if (!std::isfinite(snapshot.vx_m_s[point]) ||
            !std::isfinite(snapshot.vy_m_s[point]) ||
            !std::isfinite(snapshot.vz_m_s[point])) {
            throw std::invalid_argument("sparse snapshot values must be finite");
        }
    }
}

struct ForwardRunConfiguration {
    SimulationConfig simulation{};
    ElasticMaterial homogeneous_material{};
    MomentTensorSource source{};
    std::vector<PhysicalPoint3D> receiver_coordinates_m;
    std::string model_hdf5_path;
    std::string output_directory;
};

inline void require_valid_run_configuration(
    const ForwardRunConfiguration& configuration) {
    const auto errors = validate_staggered_elastic(configuration.simulation);
    if (!errors.empty()) {
        throw std::invalid_argument(errors.front());
    }
    require_valid_elastic_material(configuration.homogeneous_material);
    require_valid_source_metadata(configuration.source);
    if (configuration.receiver_coordinates_m.empty()) {
        throw std::invalid_argument("run configuration needs receivers");
    }
    for (const auto& point : configuration.receiver_coordinates_m) {
        static_cast<void>(physical_to_storage_coordinate(
            configuration.simulation.grid, point));
    }
    static_cast<void>(physical_to_storage_coordinate(
        configuration.simulation.grid,
        configuration.source.physical_location));
    if (configuration.model_hdf5_path.empty() ||
        configuration.output_directory.empty()) {
        throw std::invalid_argument("run model and output paths must not be empty");
    }
}

} // namespace wave3d::io
