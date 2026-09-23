#include "wave3d/desktop/experiment_inspector_state.hpp"

#include "wave3d/numerics/staggered_grid.hpp"

#include <algorithm>
#include <cmath>

namespace wave3d::desktop {
namespace {

QString source_type(DraftSourceMode mode) {
    switch (mode) {
    case DraftSourceMode::IsotropicExplosion:
        return QStringLiteral("Isotropic explosion moment tensor");
    case DraftSourceMode::MomentTensor:
        return QStringLiteral("Symmetric moment tensor");
    case DraftSourceMode::DoubleCouple:
        return QStringLiteral("Double couple (strike/dip/rake)");
    }
    return QStringLiteral("Unknown");
}

QString mechanism_detail(const ExperimentDraft& draft) {
    switch (draft.source_mode) {
    case DraftSourceMode::IsotropicExplosion:
        return QStringLiteral("Scalar moment %1 N·m")
            .arg(QString::number(draft.explosion_moment_nm, 'g', 8));
    case DraftSourceMode::MomentTensor:
        return QStringLiteral("User-defined symmetric tensor");
    case DraftSourceMode::DoubleCouple:
        return QStringLiteral("M₀ %1 N·m · strike %2° · dip %3° · rake %4°")
            .arg(QString::number(draft.double_couple.scalar_moment_nm, 'g', 8))
            .arg(QString::number(draft.double_couple.strike_deg, 'g', 8))
            .arg(QString::number(draft.double_couple.dip_deg, 'g', 8))
            .arg(QString::number(draft.double_couple.rake_deg, 'g', 8));
    }
    return QStringLiteral("Unknown");
}

QString geometry_type(ReceiverGeometryMode mode) {
    switch (mode) {
    case ReceiverGeometryMode::SurfaceRectangular:
        return QStringLiteral("Surface rectangular grid");
    case ReceiverGeometryMode::SurfaceLine:
        return QStringLiteral("Surface line");
    case ReceiverGeometryMode::ExplicitCoordinates:
        return QStringLiteral("Explicit coordinates");
    }
    return QStringLiteral("Unknown");
}

QString point(double x, double y, double z) {
    return QStringLiteral("(%1, %2, %3) m")
        .arg(QString::number(x, 'g', 8))
        .arg(QString::number(y, 'g', 8))
        .arg(QString::number(z, 'g', 8));
}

QString geometry_summary(const AcquisitionGeometry& acquisition) {
    if (acquisition.mode == ReceiverGeometryMode::SurfaceRectangular) {
        const auto& grid = acquisition.rectangular;
        return QStringLiteral("%1 × %2 points · X %3–%4 m · Y %5–%6 m")
            .arg(grid.count_x)
            .arg(grid.count_y)
            .arg(QString::number(grid.minimum_x_m + acquisition.translate_x_m, 'g', 8))
            .arg(QString::number(grid.maximum_x_m + acquisition.translate_x_m, 'g', 8))
            .arg(QString::number(grid.minimum_y_m + acquisition.translate_y_m, 'g', 8))
            .arg(QString::number(grid.maximum_y_m + acquisition.translate_y_m, 'g', 8));
    }
    if (acquisition.mode == ReceiverGeometryMode::SurfaceLine) {
        const auto& line = acquisition.line;
        return QStringLiteral("%1 points · %2 → %3")
            .arg(line.count)
            .arg(point(
                line.first_x_m + acquisition.translate_x_m,
                line.first_y_m + acquisition.translate_y_m,
                line.depth_m))
            .arg(point(
                line.last_x_m + acquisition.translate_x_m,
                line.last_y_m + acquisition.translate_y_m,
                line.depth_m));
    }
    return QStringLiteral("%1 explicit points")
        .arg(acquisition.explicit_coordinates.size());
}

QString spacing_summary(const AcquisitionGeometry& acquisition) {
    if (acquisition.mode == ReceiverGeometryMode::SurfaceRectangular) {
        const auto& grid = acquisition.rectangular;
        return QStringLiteral("dx %1 m · dy %2 m")
            .arg(QString::number(
                (grid.maximum_x_m - grid.minimum_x_m) /
                    static_cast<double>(grid.count_x - 1),
                'g', 8))
            .arg(QString::number(
                (grid.maximum_y_m - grid.minimum_y_m) /
                    static_cast<double>(grid.count_y - 1),
                'g', 8));
    }
    if (acquisition.mode == ReceiverGeometryMode::SurfaceLine) {
        const auto& line = acquisition.line;
        const auto distance = std::hypot(
            line.last_x_m - line.first_x_m,
            line.last_y_m - line.first_y_m);
        return QStringLiteral("%1 m along line")
            .arg(QString::number(
                distance / static_cast<double>(line.count - 1), 'g', 8));
    }
    return QStringLiteral("Irregular / explicit");
}

} // namespace

ExperimentInspectorState make_experiment_inspector_state(
    const QString& project_id,
    const QString& shot_id,
    const std::optional<ExperimentDraft>& draft,
    const std::optional<ResolvedExperimentDraft>& resolved,
    bool preflight_prepared,
    const QString& prepared_run_directory,
    const QString& visualization_component,
    bool segy_enabled,
    bool cuda_enabled) {
    const auto simulation_id =
        project_id + QStringLiteral(":elastic-forward");
    ExperimentInspectorState state;
    state.source.object_id = shot_id + QStringLiteral(":source");
    state.source.shot_id = shot_id;
    state.receivers.object_id = shot_id + QStringLiteral(":receivers");
    state.simulation.object_id = simulation_id;
    state.boundary.object_id = simulation_id + QStringLiteral(":boundary");
    state.output.object_id = simulation_id + QStringLiteral(":output");
    state.simulation.preflight_status =
        preflight_prepared ? QStringLiteral("Passed") : QStringLiteral("Not run");
    state.simulation.spatial_order = staggered_fd_spatial_order;
    state.simulation.stencil_radius = staggered_fd_radius;
    state.simulation.physics = QStringLiteral("3D isotropic elastic wave");
    state.simulation.formulation = QStringLiteral("Velocity–stress");
    state.simulation.grid_scheme = QStringLiteral("Staggered grid");
    state.simulation.backend =
        cuda_enabled ? QStringLiteral("CUDA") : QStringLiteral("Unavailable");
    state.simulation.device = QStringLiteral("Selected at run time");
    state.simulation.precision = QStringLiteral("float32 wavefields");
    state.output.visualization_component = visualization_component;
    state.output.segy_enabled = segy_enabled;
    state.receivers.components = QStringLiteral("Vx, Vy, Vz");
    state.output.receiver_components = QStringLiteral("Vx, Vy, Vz");
    state.output.recording_format = QStringLiteral("Three component SEG-Y files");
    state.output.segy_revision = QStringLiteral("SEG-Y Revision 1");
    state.output.segy_sample_format =
        QStringLiteral("Big-endian IEEE float32 (format code 5)");
    state.output.segy_output_naming =
        QStringLiteral("record_vx.sgy, record_vy.sgy, record_vz.sgy");
    state.output.run_directory = preflight_prepared
                                     ? prepared_run_directory
                                     : QStringLiteral("Generated at run time");
    state.output.result_manifest =
        preflight_prepared
            ? prepared_run_directory + QStringLiteral("/result.json (at completion)")
            : QStringLiteral("Generated at run completion");
    if (!draft || !resolved) return state;

    state.source.configured = true;
    state.source.source_type = source_type(draft->source_mode);
    state.source.physical_position_m = resolved->source.physical_location;
    state.source.storage_position = resolved->source.storage_location;
    state.source.origin_time_s = resolved->source.origin_time_s;
    state.source.wavelet = resolved->source.wavelet;
    state.source.moment_nm = resolved->source.moment;
    state.source.mechanism_detail = mechanism_detail(*draft);

    state.receivers.configured = true;
    state.receivers.geometry_type = geometry_type(draft->acquisition->mode);
    state.receivers.geometry_summary = geometry_summary(*draft->acquisition);
    state.receivers.spacing_summary = spacing_summary(*draft->acquisition);
    state.receivers.receiver_count = resolved->receivers.size();
    state.receivers.sample_interval_s = resolved->simulation.time.dt_s;
    state.receivers.sample_count = resolved->acquisition.sample_count;
    state.receivers.recording_duration_s = resolved->simulation.time.total_time_s;
    state.receivers.minimum_position_m = resolved->receivers.front();
    state.receivers.maximum_position_m = resolved->receivers.front();
    for (const auto& receiver : resolved->receivers) {
        state.receivers.minimum_position_m.x_m =
            std::min(state.receivers.minimum_position_m.x_m, receiver.x_m);
        state.receivers.minimum_position_m.y_m =
            std::min(state.receivers.minimum_position_m.y_m, receiver.y_m);
        state.receivers.minimum_position_m.z_m =
            std::min(state.receivers.minimum_position_m.z_m, receiver.z_m);
        state.receivers.maximum_position_m.x_m =
            std::max(state.receivers.maximum_position_m.x_m, receiver.x_m);
        state.receivers.maximum_position_m.y_m =
            std::max(state.receivers.maximum_position_m.y_m, receiver.y_m);
        state.receivers.maximum_position_m.z_m =
            std::max(state.receivers.maximum_position_m.z_m, receiver.z_m);
    }

    state.simulation.configured = true;
    state.simulation.dt_s = resolved->simulation.time.dt_s;
    state.simulation.step_count = resolved->simulation.time.step_count();
    state.simulation.total_time_s = resolved->simulation.time.total_time_s;

    const auto& grid = resolved->simulation.grid;
    state.boundary.configured = true;
    state.boundary.x_min = grid.x_boundary.lower_absorbing;
    state.boundary.x_max = grid.x_boundary.upper_absorbing;
    state.boundary.y_min = grid.y_boundary.lower_absorbing;
    state.boundary.y_max = grid.y_boundary.upper_absorbing;
    state.boundary.z_min = grid.z_boundary.lower_absorbing;
    state.boundary.z_max = grid.z_boundary.upper_absorbing;
    state.boundary.halo = grid.halo;
    state.boundary.free_surface =
        resolved->simulation.top_boundary == TopBoundary::FreeSurface;

    state.output.configured = true;
    state.output.sample_interval_s = resolved->simulation.time.dt_s;
    state.output.sample_count = resolved->acquisition.sample_count;
    return state;
}

} // namespace wave3d::desktop
