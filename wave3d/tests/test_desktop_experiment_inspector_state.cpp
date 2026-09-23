#include "wave3d/desktop/experiment_inspector_state.hpp"

#include <QCoreApplication>

#include <stdexcept>

namespace {

using namespace wave3d;
using namespace wave3d::desktop;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Grid3D grid() {
    return {
        200, 200, 187,
        25.0F, 25.0F, 25.0F,
        6, {20, 20}, {20, 20}, {0, 20}};
}

PhysicalModelExtrema extrema() {
    return {
        {2445.75928F, 1412.05981F, 2180.04321F},
        {6000.0F, 3464.10156F, 2728.34644F}};
}

void test_unconfigured_snapshot() {
    const auto state = make_experiment_inspector_state(
        QStringLiteral("project-a"), QStringLiteral("shot-001"),
        std::nullopt, std::nullopt, false, {}, QStringLiteral("Speed magnitude"),
        true, true);
    expect(!state.source.configured && !state.receivers.configured &&
               !state.simulation.configured && !state.boundary.configured &&
               !state.output.configured,
           "unconfigured experiment exposed default values as configured");
    expect(state.source.object_id == QStringLiteral("shot-001:source") &&
               state.receivers.object_id == QStringLiteral("shot-001:receivers") &&
               state.simulation.object_id ==
                   QStringLiteral("project-a:elastic-forward"),
           "snapshot identities do not match Project Navigator identities");
    expect(state.simulation.preflight_status == QStringLiteral("Not run"),
           "unconfigured snapshot has the wrong preflight state");
}

void test_resolved_snapshot() {
    const auto draft = ExperimentDraftStore::defaults(
        QStringLiteral("shot-001"), QStringLiteral("models/overthrust.h5"),
        grid(), extrema());
    const auto resolved = ExperimentDraftStore::resolve(draft, grid(), extrema());
    const auto state = make_experiment_inspector_state(
        QStringLiteral("project-a"), draft.shot_id, draft, resolved, true,
        QStringLiteral("/tmp/project-a/runs/run-001"), QStringLiteral("Vz"),
        true, true);

    expect(state.source.configured &&
               state.source.physical_position_m.x_m == 2500.0 &&
               state.source.physical_position_m.y_m == 2500.0 &&
               state.source.physical_position_m.z_m == 1150.0 &&
               state.source.wavelet.dominant_frequency_hz == 3.0,
           "resolved source values were not projected");
    expect(state.receivers.configured &&
               state.receivers.receiver_count == 10201 &&
               state.receivers.sample_interval_s == 0.001 &&
               state.receivers.sample_count == 3000 &&
               state.receivers.maximum_position_m.x_m == 4975.0 &&
               state.receivers.maximum_position_m.y_m == 4975.0,
           "resolved acquisition values were not projected");
    expect(state.simulation.configured && state.simulation.spatial_order == 12 &&
               state.simulation.stencil_radius == 6 &&
               state.simulation.dt_s == 0.001 &&
               state.simulation.step_count == 3000 &&
               state.simulation.preflight_status == QStringLiteral("Passed"),
           "resolved simulation numerics were not projected");
    expect(state.boundary.configured && state.boundary.x_min == 20 &&
               state.boundary.x_max == 20 && state.boundary.y_min == 20 &&
               state.boundary.y_max == 20 && state.boundary.z_min == 0 &&
               state.boundary.z_max == 20 && state.boundary.halo == 6 &&
               state.boundary.free_surface,
           "resolved boundary values were not projected");
    expect(state.output.configured && state.output.segy_enabled &&
               state.output.sample_count == 3000 &&
               state.output.receiver_components == QStringLiteral("Vx, Vy, Vz") &&
               state.output.segy_sample_format.contains(QStringLiteral("code 5")) &&
               state.output.visualization_component == QStringLiteral("Vz") &&
               state.output.run_directory.endsWith(QStringLiteral("run-001")),
           "resolved output values were not projected");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        test_unconfigured_snapshot();
        test_resolved_snapshot();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
