#include "wave3d/desktop/experiment_controller.hpp"
#include "wave3d/desktop/experiment_editor.hpp"
#include "wave3d/desktop/experiment_inspector_state.hpp"
#include "wave3d/desktop/main_window.hpp"
#include "wave3d/desktop/project_workspace.hpp"
#include "wave3d/desktop/selection_controller.hpp"

#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
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

    auto* source_x =
        require_child<QDoubleSpinBox>(window, "sourceXSpin");
    source_x->setValue(2525.0);
    expect(controller->state().dirty && controller->validation().valid() &&
               controller->draft()->source_location_m.x_m == 2525.0 &&
               controller->appliedConfiguration()->source.physical_location.x_m ==
                   2500.0,
           "ExperimentEditor did not publish a separated dirty draft");
    expect(controller->revert() && !controller->state().dirty &&
               controller->draft()->source_location_m.x_m == 2500.0,
           "controller Revert did not restore the real Overthrust baseline");
    source_x->setValue(2500.0);

    window.selection_controller()->setSelection(SelectionContext{
        SelectionKind::Source,
        project.shots.front().id + QStringLiteral(":source"),
        project.project_id});
    auto* frequency =
        require_child<QDoubleSpinBox>(window, "sourceFrequencySpin");
    frequency->setValue(2.9);
    expect(controller->state().dirty && controller->validate().valid(),
           "real Overthrust safe edit did not validate");
    expect(controller->apply(&error) &&
               controller->appliedConfiguration()
                       ->source.wavelet.dominant_frequency_hz == 2.9 &&
               require_child<QLabel>(window, "sourceInspectorFrequency")
                   ->text() == QStringLiteral("2.9 Hz"),
           "Apply did not update the applied state and read-only Inspector");

    frequency->setValue(3.0);
    expect(controller->apply(&error) &&
               controller->appliedConfiguration()
                       ->source.wavelet.dominant_frequency_hz == 3.0,
           "Overthrust baseline was not restored after integration edit");
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    try {
        test_real_overthrust_controller_contract();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}

