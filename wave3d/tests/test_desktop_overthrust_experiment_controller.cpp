#include "wave3d/desktop/experiment_controller.hpp"
#include "wave3d/desktop/experiment_editor.hpp"
#include "wave3d/desktop/experiment_inspector_state.hpp"
#include "wave3d/desktop/main_window.hpp"
#include "wave3d/desktop/project_navigator.hpp"
#include "wave3d/desktop/project_workspace.hpp"
#include "wave3d/desktop/selection_controller.hpp"
#include "wave3d/desktop/theme.hpp"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>

#include <filesystem>
#include <stdexcept>

namespace {

using namespace wave3d::desktop;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Widget>
Widget* require_child(QWidget& parent, const char* object_name) {
    auto* child = parent.findChild<Widget*>(QString::fromUtf8(object_name));
    if (!child) throw std::runtime_error("required widget is missing");
    return child;
}

void capture_if_requested(MainWindow& window, const QString& file_name) {
    const auto directory =
        qEnvironmentVariable("WAVE3D_SOURCE_SCREENSHOT_DIR");
    if (directory.isEmpty()) return;
    expect(QDir().mkpath(directory), "screenshot directory could not be created");
    window.bottom_tool_dock()->hide();
    window.resize(1440, 1000);
    window.show();
    QApplication::processEvents();
    expect(
        window.grab().save(QDir(directory).filePath(file_name)),
        "SourceInspector screenshot could not be saved");
}

void test_real_overthrust_controller_contract() {
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary Overthrust project is unavailable");
    const auto project_root =
        QDir(temporary.path()).filePath(QStringLiteral("project"));
    auto project = ProjectWorkspace::create(
        project_root, QStringLiteral("Overthrust Controller Integration"));
    const auto model_relative =
        QStringLiteral("models/elastic_200x200x187.h5");
    const auto model_destination =
        QDir(project_root).filePath(model_relative);
    std::filesystem::create_symlink(
        std::filesystem::path(WAVE3D_TEST_OVERTHRUST_MODEL),
        std::filesystem::path(model_destination.toStdString()));
    project.model_reference = model_relative;
    ProjectWorkspace::save(project_root, project);

    MainWindow window(nullptr, false);
    window.resize(1440, 900);
    QString error;
    expect(window.open_project(project_root, &error),
           "real Overthrust project did not open");
    auto* controller = window.experiment_controller();
    expect(controller && controller->state().edit_state ==
                             ExperimentEditState::Applied,
           "real Overthrust draft was not initially applied");
    const auto& resolved = *controller->appliedConfiguration();
    expect(resolved.source.physical_location.x_m == 2500.0 &&
               resolved.source.physical_location.y_m == 2500.0 &&
               resolved.source.physical_location.z_m == 1150.0 &&
               resolved.source.wavelet.dominant_frequency_hz == 3.0 &&
               resolved.receivers.size() == 10201 &&
               resolved.simulation.time.dt_s == 0.001 &&
               resolved.simulation.time.step_count() == 3000 &&
               resolved.simulation.grid.x_boundary.lower_absorbing == 20 &&
               resolved.simulation.grid.z_boundary.lower_absorbing == 0,
           "real Overthrust production values changed through the controller");
    const auto presentation = make_experiment_inspector_state(
        project.project_id,
        project.shots.front().id,
        controller->appliedDraft(),
        controller->appliedConfiguration(),
        false,
        {},
        QStringLiteral("Speed magnitude"),
        true,
        true);
    expect(presentation.simulation.spatial_order == 12 &&
               presentation.simulation.stencil_radius == 6 &&
               presentation.output.receiver_components ==
                   QStringLiteral("Vx, Vy, Vz"),
           "Overthrust Inspector projection changed numerical/output contracts");

    expect(window.project_navigator()->selectContext(SelectionContext{
        SelectionKind::Source,
        project.shots.front().id + QStringLiteral(":source"),
        project.project_id}),
        "ProjectNavigator could not select the real Source");
    auto* inspector_x =
        require_child<QDoubleSpinBox>(window, "sourceInspectorXSpin");
    auto* inspector_y =
        require_child<QDoubleSpinBox>(window, "sourceInspectorYSpin");
    auto* inspector_z =
        require_child<QDoubleSpinBox>(window, "sourceInspectorZSpin");
    auto* inspector_frequency = require_child<QDoubleSpinBox>(
        window, "sourceInspectorFrequencySpin");
    auto* inspector_peak_rate = require_child<QDoubleSpinBox>(
        window, "sourceInspectorPeakRateSpin");
    auto* legacy_x = require_child<QDoubleSpinBox>(window, "sourceXSpin");
    auto* legacy_y = require_child<QDoubleSpinBox>(window, "sourceYSpin");
    auto* legacy_z = require_child<QDoubleSpinBox>(window, "sourceZSpin");
    auto* legacy_frequency =
        require_child<QDoubleSpinBox>(window, "sourceFrequencySpin");
    auto* apply = require_child<QPushButton>(window, "inspectorApplyButton");
    auto* revert = require_child<QPushButton>(window, "inspectorRevertButton");
    auto* edit_status = require_child<QLabel>(window, "inspectorEditStatus");
    auto* validation =
        require_child<QLabel>(window, "inspectorEditValidation");
    expect(
        inspector_x->value() == 2500.0 && inspector_y->value() == 2500.0 &&
            inspector_z->value() == 1150.0 &&
            inspector_frequency->value() == 3.0 &&
            edit_status->text() == QStringLiteral("Applied"),
        "real Overthrust Source draft did not reach SourceInspector");
    capture_if_requested(window, QStringLiteral("01-source-applied.png"));

    int draft_changes = 0;
    QObject::connect(
        controller, &ExperimentController::draftChanged,
        [&draft_changes](const ExperimentDraft&) { ++draft_changes; });
    inspector_x->setValue(2525.0);
    expect(
        draft_changes == 1 && controller->state().dirty &&
            controller->validation().valid() && legacy_x->value() == 2525.0 &&
            controller->appliedConfiguration()
                    ->source.physical_location.x_m == 2500.0 &&
            edit_status->text() == QStringLiteral("Modified"),
        "Inspector edit did not synchronize the legacy editor or preserve Applied");
    capture_if_requested(window, QStringLiteral("02-source-modified.png"));

    inspector_x->setValue(-25.0);
    expect(
        draft_changes == 2 &&
            controller->state().edit_state == ExperimentEditState::Invalid &&
            legacy_x->value() == -25.0 && !apply->isEnabled() &&
            revert->isEnabled() &&
            controller->appliedConfiguration()
                    ->source.physical_location.x_m == 2500.0 &&
            edit_status->text() == QStringLiteral("Invalid") &&
            validation->text().contains(QStringLiteral("outside")),
        "invalid Inspector Source edit replaced Applied or lost validation");
    capture_if_requested(window, QStringLiteral("03-source-invalid.png"));

    inspector_x->setValue(2525.0);
    inspector_peak_rate->setValue(0.0);
    expect(
        controller->state().edit_state == ExperimentEditState::Invalid &&
            validation->text().contains(QStringLiteral("peak moment rate")),
        "Ricker validation message did not reach SourceInspector");
    capture_if_requested(
        window, QStringLiteral("05-source-validation-message.png"));

    revert->click();
    expect(
        !controller->state().dirty && inspector_x->value() == 2500.0 &&
            legacy_x->value() == 2500.0 &&
            edit_status->text() == QStringLiteral("Applied"),
        "Inspector Revert did not synchronize both editors");
    capture_if_requested(window, QStringLiteral("04-source-after-revert.png"));

    const auto before_inspector_batch = draft_changes;
    inspector_x->setValue(2525.0);
    inspector_y->setValue(2475.0);
    inspector_z->setValue(1175.0);
    inspector_frequency->setValue(2.9);
    expect(
        draft_changes == before_inspector_batch + 4 &&
            legacy_x->value() == 2525.0 && legacy_y->value() == 2475.0 &&
            legacy_z->value() == 1175.0 &&
            legacy_frequency->value() == 2.9,
        "SourceInspector fields produced a signal loop or failed legacy sync");

    const auto before_legacy_edit = draft_changes;
    legacy_frequency->setValue(2.8);
    expect(
        draft_changes == before_legacy_edit + 1 &&
            inspector_frequency->value() == 2.8 &&
            controller->draft()->wavelet.dominant_frequency_hz == 2.8,
        "legacy ExperimentEditor edit did not synchronize SourceInspector");
    revert->click();
    expect(
        inspector_x->value() == 2500.0 && inspector_y->value() == 2500.0 &&
            inspector_z->value() == 1150.0 &&
            inspector_frequency->value() == 3.0 &&
            legacy_frequency->value() == 3.0,
        "Revert did not restore all Source fields in both editors");

    inspector_frequency->setValue(2.9);
    apply->click();
    expect(
        !controller->state().dirty &&
            controller->appliedConfiguration()
                    ->source.wavelet.dominant_frequency_hz == 2.9 &&
            edit_status->text() == QStringLiteral("Applied"),
        "SourceInspector Apply did not update Applied source state");

    inspector_frequency->setValue(3.0);
    apply->click();
    expect(
        !controller->state().dirty &&
            controller->appliedConfiguration()
                    ->source.physical_location.x_m == 2500.0 &&
            controller->appliedConfiguration()
                    ->source.physical_location.y_m == 2500.0 &&
            controller->appliedConfiguration()
                    ->source.physical_location.z_m == 1150.0 &&
            controller->appliedConfiguration()
                    ->source.wavelet.dominant_frequency_hz == 3.0,
        "canonical Overthrust Source was not restored after integration edit");
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    apply_scientific_theme(application);
    try {
        test_real_overthrust_controller_contract();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
