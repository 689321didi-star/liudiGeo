#pragma once

#include "wave3d/desktop/experiment_draft.hpp"

#include <QString>

#include <cstddef>
#include <optional>

namespace wave3d::desktop {

struct SourceInspectorState final {
    QString object_id;
    QString shot_id;
    bool configured{false};
    QString source_type;
    PhysicalPoint3D physical_position_m{};
    FractionalStorageCoordinate3D storage_position{};
    double origin_time_s{0.0};
    RickerWavelet wavelet{};
    SymmetricMomentTensor moment_nm{};
    QString mechanism_detail;
};

struct ReceiverSetInspectorState final {
    QString object_id;
    bool configured{false};
    QString geometry_type;
    QString geometry_summary;
    QString spacing_summary;
    QString components;
    std::size_t receiver_count{0};
    PhysicalPoint3D minimum_position_m{};
    PhysicalPoint3D maximum_position_m{};
    double sample_interval_s{0.0};
    std::size_t sample_count{0};
    double recording_duration_s{0.0};
};

struct SimulationInspectorState final {
    QString object_id;
    bool configured{false};
    std::size_t spatial_order{0};
    std::size_t stencil_radius{0};
    QString physics;
    QString formulation;
    QString grid_scheme;
    double dt_s{0.0};
    std::size_t step_count{0};
    double total_time_s{0.0};
    QString backend;
    QString device;
    QString precision;
    QString preflight_status;
};

struct BoundaryInspectorState final {
    QString object_id;
    bool configured{false};
    std::size_t x_min{0};
    std::size_t x_max{0};
    std::size_t y_min{0};
    std::size_t y_max{0};
    std::size_t z_min{0};
    std::size_t z_max{0};
    std::size_t halo{0};
    bool free_surface{false};
};

struct OutputInspectorState final {
    QString object_id;
    bool configured{false};
    double sample_interval_s{0.0};
    std::size_t sample_count{0};
    QString receiver_components;
    QString recording_format;
    QString segy_revision;
    QString segy_sample_format;
    QString segy_output_naming;
    QString visualization_component;
    bool segy_enabled{false};
    QString run_directory;
    QString result_manifest;
};

struct ExperimentInspectorState final {
    SourceInspectorState source;
    ReceiverSetInspectorState receivers;
    SimulationInspectorState simulation;
    BoundaryInspectorState boundary;
    OutputInspectorState output;
};

[[nodiscard]] ExperimentInspectorState make_experiment_inspector_state(
    const QString& project_id,
    const QString& shot_id,
    const std::optional<ExperimentDraft>& draft,
    const std::optional<ResolvedExperimentDraft>& resolved,
    bool preflight_prepared,
    const QString& prepared_run_directory,
    const QString& visualization_component,
    bool segy_enabled,
    bool cuda_enabled);

} // namespace wave3d::desktop
