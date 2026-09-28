#include "wave3d/desktop/experiment_controller.hpp"
#include "wave3d/desktop/experiment_editor.hpp"
#include "wave3d/desktop/experiment_navigation_guard.hpp"
#include "wave3d/desktop/main_window.hpp"
#include "wave3d/desktop/project_navigator.hpp"
#include "wave3d/desktop/project_workspace.hpp"
#include "wave3d/desktop/selection_controller.hpp"
#include "wave3d/desktop/theme.hpp"

#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QTemporaryDir>

#include <deque>
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

ProjectDocument create_overthrust_project(
    const QString& root,
    const QString& name) {
    auto project = ProjectWorkspace::create(root, name);
    const auto relative = QStringLiteral("models/elastic_200x200x187.h5");
    std::filesystem::create_symlink(
        std::filesystem::path(WAVE3D_TEST_OVERTHRUST_MODEL),
        std::filesystem::path(QDir(root).filePath(relative).toStdString()));
    project.model_reference = relative;
    ProjectWorkspace::save(root, project);
    return project;
}

void select_source(MainWindow& window, const ProjectDocument& project) {
    expect(window.project_navigator()->selectContext(SelectionContext{
               SelectionKind::Source,
               project.shots.front().id + QStringLiteral(":source"),
               project.project_id}),
           "Source selection failed");
}

void test_real_overthrust_navigation_guard() {
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary project root is unavailable");
    const auto root_a = QDir(temporary.path()).filePath(QStringLiteral("a"));
    const auto root_b = QDir(temporary.path()).filePath(QStringLiteral("b"));
    const auto root_c = QDir(temporary.path()).filePath(QStringLiteral("c"));
    const auto project_a = create_overthrust_project(
        root_a, QStringLiteral("Dirty Guard A"));
    const auto project_b = create_overthrust_project(
        root_b, QStringLiteral("Dirty Guard B"));

    std::deque<ExperimentNavigationDecision> decisions;
    int decision_requests = 0;
    MainWindow window(
        nullptr, false,
        [&decisions, &decision_requests] {
            ++decision_requests;
            if (decisions.empty()) {
                throw std::runtime_error("unexpected navigation decision request");
            }
            const auto decision = decisions.front();
            decisions.pop_front();
            return decision;
        });
    QString error;
    expect(window.open_project(root_a, &error) && decision_requests == 0,
           "clean project open entered the prompt path");
    expect(window.open_project(root_b, &error) && decision_requests == 0 &&
               window.current_project_root() == QDir(root_b).absolutePath(),
           "clean Draft project switch entered the prompt path");
    expect(window.open_project(root_a, &error) && decision_requests == 0,
           "clean project could not be restored without a prompt");
    select_source(window, project_a);

    auto* inspector_frequency = require_child<QDoubleSpinBox>(
        window, "sourceInspectorFrequencySpin");
    auto* legacy_frequency =
        require_child<QDoubleSpinBox>(window, "sourceFrequencySpin");
    auto* inspector_x =
        require_child<QDoubleSpinBox>(window, "sourceInspectorXSpin");
    auto* legacy_x = require_child<QDoubleSpinBox>(window, "sourceXSpin");
    auto* controller = window.experiment_controller();
    expect(
        controller->appliedConfiguration()
                    ->source.physical_location.x_m == 2500.0 &&
            controller->appliedConfiguration()
                    ->source.physical_location.y_m == 2500.0 &&
            controller->appliedConfiguration()
                    ->source.physical_location.z_m == 1150.0 &&
            controller->appliedConfiguration()
                    ->source.wavelet.dominant_frequency_hz == 3.0,
        "canonical Overthrust source baseline changed");

    inspector_frequency->setValue(2.9);
    const auto selection_before_cancel =
        window.selection_controller()->currentSelection();
    decisions.push_back(ExperimentNavigationDecision::Cancel);
    error = QStringLiteral("stale");
    expect(!window.open_project(root_b, &error) && error.isEmpty() &&
               window.current_project_root() == QDir(root_a).absolutePath() &&
               controller->draft()->wavelet.dominant_frequency_hz == 2.9 &&
               controller->appliedConfiguration()
                       ->source.wavelet.dominant_frequency_hz == 3.0 &&
               window.selection_controller()->currentSelection() ==
                   selection_before_cancel,
           "Cancel changed project, Draft, Applied state, or selection");

    expect(resolve_experiment_navigation(
               *controller, ExperimentNavigationDecision::Discard) ==
               ExperimentNavigationOutcome::Proceed &&
               inspector_frequency->value() == 3.0 &&
               legacy_frequency->value() == 3.0,
           "Discard did not synchronize SourceInspector and ExperimentEditor");

    inspector_frequency->setValue(2.9);
    decisions.push_back(ExperimentNavigationDecision::Discard);
    expect(window.open_project(root_b, &error) &&
               window.current_project_root() == QDir(root_b).absolutePath() &&
               !controller->state().dirty &&
               inspector_frequency->value() == 3.0 &&
               legacy_frequency->value() == 3.0,
           "Discard did not revert and complete project switch");

    select_source(window, project_b);
    inspector_frequency->setValue(2.9);
    double applied_before_switch = 0.0;
    QObject::connect(
        controller, &ExperimentController::appliedConfigurationChanged,
        [&applied_before_switch](const ResolvedExperimentDraft& resolved) {
            if (applied_before_switch == 0.0) {
                applied_before_switch =
                    resolved.source.wavelet.dominant_frequency_hz;
            }
        });
    decisions.push_back(ExperimentNavigationDecision::Apply);
    expect(window.open_project(root_a, &error) &&
               applied_before_switch == 2.9 &&
               window.current_project_root() == QDir(root_a).absolutePath(),
           "valid Apply did not publish before project switch");

    select_source(window, project_a);
    inspector_x->setValue(-25.0);
    const auto invalid_selection =
        window.selection_controller()->currentSelection();
    decisions.push_back(ExperimentNavigationDecision::Apply);
    expect(!window.open_project(root_b, &error) && !error.isEmpty() &&
               window.current_project_root() == QDir(root_a).absolutePath() &&
               controller->state().dirty &&
               controller->draft()->source_location_m.x_m == -25.0 &&
               controller->appliedConfiguration()
                       ->source.physical_location.x_m == 2500.0 &&
               inspector_x->value() == -25.0 && legacy_x->value() == -25.0 &&
               window.selection_controller()->currentSelection() ==
                   invalid_selection,
           "failed Apply did not preserve the active project and invalid Draft");
    expect(controller->revert(), "failed-Apply Draft could not be reverted");

    inspector_frequency->setValue(2.9);
    decisions.push_back(ExperimentNavigationDecision::Cancel);
    error = QStringLiteral("stale");
    expect(!window.create_project(
               root_c, QStringLiteral("Dirty Guard C"), &error) &&
               error.isEmpty() && !QFileInfo::exists(
                   QDir(root_c).filePath(QStringLiteral("project.wave3d.json"))) &&
               window.current_project_root() == QDir(root_a).absolutePath(),
           "Cancel did not block new-project replacement before creation");
    decisions.push_back(ExperimentNavigationDecision::Discard);
    expect(window.create_project(
               root_c, QStringLiteral("Dirty Guard C"), &error) &&
               window.current_project_root() == QDir(root_c).absolutePath(),
           "Discard did not allow new-project replacement");

    expect(window.open_project(root_a, &error),
           "canonical project could not be restored for close testing");
    select_source(window, project_a);
    inspector_frequency->setValue(2.9);
    window.show();
    QApplication::processEvents();
    const auto requests_before_close = decision_requests;
    const auto close_selection = window.selection_controller()->currentSelection();
    decisions.push_back(ExperimentNavigationDecision::Cancel);
    expect(!window.close() &&
               decision_requests == requests_before_close + 1 &&
               window.current_project_root() == QDir(root_a).absolutePath() &&
               controller->draft()->wavelet.dominant_frequency_hz == 2.9 &&
               window.selection_controller()->currentSelection() ==
                   close_selection,
           "Cancel did not block project/application close without side effects");

    decisions.push_back(ExperimentNavigationDecision::Discard);
    const auto requests_before_final_close = decision_requests;
    expect(window.close() &&
               decision_requests == requests_before_final_close + 1 &&
               !controller->state().dirty &&
               controller->draft()->wavelet.dominant_frequency_hz == 3.0,
           "application close prompted more than once or failed to restore baseline");
    expect(decisions.empty(), "a planned navigation decision was not consumed");
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    apply_scientific_theme(application);
    try {
        test_real_overthrust_navigation_guard();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
