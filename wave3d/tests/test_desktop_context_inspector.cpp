#include "wave3d/desktop/context_inspector.hpp"
#include "wave3d/desktop/selection_controller.hpp"

#include <QApplication>
#include <QLabel>

#include <stdexcept>
#include <type_traits>

namespace {

using namespace wave3d::desktop;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

QLabel* label(ContextInspector& inspector, const char* name) {
    auto* value = inspector.findChild<QLabel*>(QString::fromUtf8(name));
    if (value == nullptr) throw std::runtime_error("inspector label is missing");
    return value;
}

ContextInspectorState state(
    const QString& project_id = QStringLiteral("project-a"),
    const QString& project_name = QStringLiteral("Project A")) {
    ContextInspectorState result;
    result.project = ProjectInspectorState{
        project_id, project_name, QStringLiteral("/tmp/project-a"),
        QStringLiteral("models/model.h5"), true, 1, 3, 2};
    result.model = ModelInspectorState{
        QStringLiteral("models/model.h5"),
        QStringLiteral("/tmp/project-a/models/model.h5"),
        200, 200, 187, 25.0, 25.0, 25.0,
        4975.0, 4975.0, 4650.0,
        2445.8F, 6000.0F, 1200.0F, 3500.0F, 2100.0F, 2700.0F};
    return result;
}

ExperimentInspectorState experiment_state(bool configured) {
    ExperimentInspectorState result;
    result.source.object_id = QStringLiteral("shot-001:source");
    result.source.shot_id = QStringLiteral("shot-001");
    result.receivers.object_id = QStringLiteral("shot-001:receivers");
    result.simulation.object_id = QStringLiteral("project-a:elastic-forward");
    result.boundary.object_id = QStringLiteral("project-a:elastic-forward:boundary");
    result.output.object_id = QStringLiteral("project-a:elastic-forward:output");
    result.source.configured = configured;
    result.receivers.configured = configured;
    result.simulation.configured = configured;
    result.boundary.configured = configured;
    result.output.configured = configured;
    result.simulation.spatial_order = 12;
    result.simulation.stencil_radius = 6;
    result.simulation.physics = QStringLiteral("3D isotropic elastic wave");
    result.simulation.formulation = QStringLiteral("Velocity–stress");
    result.simulation.grid_scheme = QStringLiteral("Staggered grid");
    result.simulation.backend = QStringLiteral("CUDA");
    result.simulation.device = QStringLiteral("Selected at run time");
    result.simulation.precision = QStringLiteral("float32 wavefields");
    result.simulation.preflight_status = QStringLiteral("Passed");
    result.output.visualization_component = QStringLiteral("Speed magnitude");
    result.output.segy_enabled = true;
    result.receivers.components = QStringLiteral("Vx, Vy, Vz");
    result.output.receiver_components = QStringLiteral("Vx, Vy, Vz");
    result.output.recording_format = QStringLiteral("Three component SEG-Y files");
    result.output.segy_revision = QStringLiteral("SEG-Y Revision 1");
    result.output.segy_sample_format =
        QStringLiteral("Big-endian IEEE float32 (format code 5)");
    result.output.segy_output_naming =
        QStringLiteral("record_vx.sgy, record_vy.sgy, record_vz.sgy");
    result.output.run_directory = QStringLiteral("Generated at run time");
    result.output.result_manifest = QStringLiteral("Generated at run completion");
    if (!configured) return result;
    result.source.source_type = QStringLiteral("Isotropic explosion moment tensor");
    result.source.physical_position_m = {2500.0, 2500.0, 1150.0};
    result.source.storage_position = {126.0, 126.0, 52.0};
    result.source.wavelet = {3.0, 1.0 / 3.0, 1.0};
    result.source.moment_nm = {1.0e12, 1.0e12, 1.0e12, 0.0, 0.0, 0.0};
    result.source.mechanism_detail = QStringLiteral("Scalar moment 1e+12 N·m");
    result.receivers.geometry_type = QStringLiteral("Surface rectangular grid");
    result.receivers.geometry_summary = QStringLiteral("101 × 101 points");
    result.receivers.spacing_summary = QStringLiteral("dx 49.75 m · dy 49.75 m");
    result.receivers.receiver_count = 10201;
    result.receivers.minimum_position_m = {0.0, 0.0, 0.0};
    result.receivers.maximum_position_m = {4975.0, 4975.0, 0.0};
    result.receivers.sample_interval_s = 0.001;
    result.receivers.sample_count = 3000;
    result.receivers.recording_duration_s = 3.0;
    result.simulation.dt_s = 0.001;
    result.simulation.step_count = 3000;
    result.simulation.total_time_s = 3.0;
    result.boundary = {
        result.boundary.object_id, true, 20, 20, 20, 20, 0, 20, 6, true};
    result.output.sample_interval_s = 0.001;
    result.output.sample_count = 3000;
    return result;
}

SelectionContext selection(
    SelectionKind kind,
    QString object_id,
    std::optional<SelectionModelProperty> property = std::nullopt,
    const QString& project_id = QStringLiteral("project-a")) {
    return SelectionContext{
        kind, std::move(object_id), project_id, std::nullopt, property};
}

void test_page_mapping_and_reuse() {
    SelectionController controller;
    ContextInspector inspector(&controller);
    inspector.setState(state());
    expect(inspector.currentPage() == InspectorPage::Empty, "None is not empty");

    controller.setSelection(selection(SelectionKind::Project, QStringLiteral("project-a")));
    expect(inspector.currentPage() == InspectorPage::Project, "Project page is missing");
    expect(label(inspector, "projectInspectorName")->text() == QStringLiteral("Project A"),
           "Project data is incorrect");

    controller.setSelection(selection(SelectionKind::Model, QStringLiteral("models/model.h5")));
    expect(inspector.currentPage() == InspectorPage::Model, "Model page is missing");
    expect(label(inspector, "modelInspectorDimensions")->text() ==
               QStringLiteral("200 × 200 × 187"),
           "Model dimensions are incorrect");

    controller.setSelection(selection(
        SelectionKind::ModelProperty, QStringLiteral("models/model.h5:vp"),
        SelectionModelProperty::Vp));
    auto* property_page = inspector.currentPageWidget();
    expect(inspector.currentPage() == InspectorPage::ModelProperty,
           "Vp did not select the property page");
    expect(label(inspector, "modelPropertyInspectorMinimum")->text().contains(
               QStringLiteral("2445.8")),
           "Vp range is incorrect");

    controller.setSelection(selection(
        SelectionKind::ModelProperty, QStringLiteral("models/model.h5:vs"),
        SelectionModelProperty::Vs));
    expect(inspector.currentPageWidget() == property_page,
           "Vs recreated or replaced the shared property page");
    expect(label(inspector, "modelPropertyInspectorName")->text() == QStringLiteral("Vs"),
           "Vs did not update the shared property page");

    controller.setSelection(selection(
        SelectionKind::ModelProperty, QStringLiteral("models/model.h5:density"),
        SelectionModelProperty::Density));
    expect(inspector.currentPageWidget() == property_page,
           "Density does not reuse the property page");
    expect(label(inspector, "modelPropertyInspectorUnit")->text() ==
               QStringLiteral("kg/m³"),
           "Density unit is incorrect");

    controller.setSelection(selection(SelectionKind::Grid, QStringLiteral("models/model.h5:grid")));
    expect(inspector.currentPage() == InspectorPage::Grid, "Grid page is missing");
    expect(label(inspector, "gridInspectorTotalCells")->text().contains(
               QStringLiteral("7480000")),
           "Grid cell count is incorrect");
}

void test_state_replacement_and_safe_fallback() {
    SelectionController controller;
    ContextInspector inspector(&controller);
    inspector.setState(state());
    controller.setSelection(selection(SelectionKind::Project, QStringLiteral("project-a")));

    inspector.setState({});
    expect(inspector.currentPage() == InspectorPage::Empty,
           "project close retained a stale page");
    expect(label(inspector, "projectInspectorName")->text() == QStringLiteral("—"),
           "project close retained stale data");

    auto next = state(QStringLiteral("project-b"), QStringLiteral("Project B"));
    next.project->root_path = QStringLiteral("/tmp/project-b");
    next.model.reset();
    inspector.setState(next);
    controller.setSelection(selection(
        SelectionKind::Project, QStringLiteral("project-b"), std::nullopt,
        QStringLiteral("project-b")));
    expect(inspector.currentPage() == InspectorPage::Project &&
               label(inspector, "projectInspectorName")->text() ==
                   QStringLiteral("Project B"),
           "project switch did not update inspector state");

    controller.setSelection(selection(
        SelectionKind::Source, QStringLiteral("shot-001:source"), std::nullopt,
        QStringLiteral("project-b")));
    expect(inspector.currentPage() == InspectorPage::Empty,
           "missing experiment state did not fall back safely");
}

void test_experiment_page_mapping_and_values() {
    SelectionController controller;
    ContextInspector inspector(&controller);
    auto configured = state();
    configured.experiment = experiment_state(true);
    inspector.setState(configured);

    controller.setSelection(selection(
        SelectionKind::Source, QStringLiteral("shot-001:source")));
    expect(inspector.currentPage() == InspectorPage::Source &&
               label(inspector, "sourceInspectorStatus")->text() ==
                   QStringLiteral("Configured") &&
               label(inspector, "sourceInspectorFrequency")->text() ==
                   QStringLiteral("3 Hz"),
           "configured Source did not populate SourceInspector");

    controller.setSelection(selection(
        SelectionKind::ReceiverSet, QStringLiteral("shot-001:receivers")));
    expect(inspector.currentPage() == InspectorPage::ReceiverSet &&
               label(inspector, "receiverInspectorCount")->text() ==
                   QStringLiteral("10201"),
           "ReceiverSet did not populate ReceiverSetInspector");

    controller.setSelection(selection(
        SelectionKind::Simulation, QStringLiteral("project-a:elastic-forward")));
    expect(inspector.currentPage() == InspectorPage::Simulation &&
               label(inspector, "simulationInspectorSpatialOrder")->text() ==
                   QStringLiteral("12th order") &&
               label(inspector, "simulationInspectorDt")->text().contains(
                   QStringLiteral("1 ms")) &&
               label(inspector, "simulationInspectorSteps")->text() ==
                   QStringLiteral("3000"),
           "Simulation numerics are incorrect");

    controller.setSelection(selection(
        SelectionKind::Boundary,
        QStringLiteral("project-a:elastic-forward:boundary")));
    expect(inspector.currentPage() == InspectorPage::Boundary &&
               label(inspector, "boundaryInspectorX")->text() ==
                   QStringLiteral("20 / 20 cells") &&
               label(inspector, "boundaryInspectorZ")->text() ==
                   QStringLiteral("0 / 20 cells"),
           "Boundary CPML values are incorrect");

    controller.setSelection(selection(
        SelectionKind::Output, QStringLiteral("project-a:elastic-forward:output")));
    expect(inspector.currentPage() == InspectorPage::Output &&
               label(inspector, "outputInspectorComponents")->text() ==
                   QStringLiteral("Vx, Vy, Vz") &&
               label(inspector, "outputInspectorSamples")->text() ==
                   QStringLiteral("3000"),
           "Output values are incorrect");

    controller.setSelection(selection(
        SelectionKind::Run, QStringLiteral("run-001")));
    expect(inspector.currentPage() == InspectorPage::Unsupported,
           "Run selection did not retain the safe unsupported page");
    controller.setSelection(selection(
        SelectionKind::Result, QStringLiteral("run-001:result")));
    expect(inspector.currentPage() == InspectorPage::Unsupported,
           "Result selection did not retain the safe unsupported page");
}

void test_unconfigured_and_stale_experiment_state() {
    SelectionController controller;
    ContextInspector inspector(&controller);
    auto unconfigured = state();
    unconfigured.experiment = experiment_state(false);
    inspector.setState(unconfigured);
    controller.setSelection(selection(
        SelectionKind::Source, QStringLiteral("shot-001:source")));
    expect(inspector.currentPage() == InspectorPage::Source &&
               label(inspector, "sourceInspectorStatus")->text() ==
                   QStringLiteral("Not configured") &&
               label(inspector, "sourceInspectorFrequency")->text() ==
                   QStringLiteral("—"),
           "unconfigured Source exposed plausible default values");
    controller.setSelection(selection(
        SelectionKind::ReceiverSet, QStringLiteral("shot-001:receivers")));
    expect(inspector.currentPage() == InspectorPage::ReceiverSet &&
               label(inspector, "receiverInspectorStatus")->text() ==
                   QStringLiteral("Not configured") &&
               label(inspector, "receiverInspectorCount")->text() ==
                   QStringLiteral("—"),
           "unconfigured ReceiverSet exposed plausible default values");

    inspector.setState({});
    expect(inspector.currentPage() == InspectorPage::Empty &&
               label(inspector, "receiverInspectorStatus")->text() ==
                   QStringLiteral("—") &&
               label(inspector, "sourceInspectorId")->text() ==
                   QStringLiteral("—"),
           "project replacement retained stale experiment values");
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    static_assert(!std::is_base_of_v<QWidget, SelectionContext>);
    static_assert(std::is_same_v<decltype(SelectionContext::object_id), QString>);
    try {
        test_page_mapping_and_reuse();
        test_state_replacement_and_safe_fallback();
        test_experiment_page_mapping_and_values();
        test_unconfigured_and_stale_experiment_state();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
