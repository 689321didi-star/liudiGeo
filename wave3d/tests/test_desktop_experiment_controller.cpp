#include "wave3d/desktop/experiment_controller.hpp"
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

ExperimentDraft draft(const QString& shot_id = QStringLiteral("shot-001")) {
    return ExperimentDraftStore::defaults(
        shot_id, QStringLiteral("models/overthrust.h5"), grid(), extrema());
}

void test_load_dirty_validation_apply_and_revert() {
    ExperimentController controller;
    int draft_changes = 0;
    int dirty_changes = 0;
    int validation_changes = 0;
    int applied_changes = 0;
    QObject::connect(
        &controller, &ExperimentController::draftChanged,
        [&draft_changes](const ExperimentDraft&) { ++draft_changes; });
    QObject::connect(
        &controller, &ExperimentController::dirtyChanged,
        [&dirty_changes](bool) { ++dirty_changes; });
    QObject::connect(
        &controller, &ExperimentController::validationChanged,
        [&validation_changes](const ExperimentValidationResult&) {
            ++validation_changes;
        });
    QObject::connect(
        &controller, &ExperimentController::appliedConfigurationChanged,
        [&applied_changes](const ResolvedExperimentDraft&) { ++applied_changes; });

    const auto initial = draft();
    controller.load(grid(), extrema(), initial);
    expect(controller.hasContext() && controller.draft().has_value(),
           "controller did not load the existing draft");
    expect(controller.state().edit_state == ExperimentEditState::Applied &&
               !controller.state().dirty && controller.state().can_preflight,
           "initial valid draft is not clean and applied");
    expect(controller.validation().valid() &&
               controller.appliedConfiguration()->receivers.size() == 10201,
           "initial draft did not use production resolution");
    expect(!controller.updateDraft(initial) && !controller.state().dirty,
           "repeated no-op edit created false dirty state");

    auto valid_edit = initial;
    valid_edit.dt_s = 0.0009;
    expect(controller.updateDraft(valid_edit), "valid edit was ignored");
    expect(controller.state().dirty &&
               controller.state().edit_state ==
                   ExperimentEditState::ValidUnapplied &&
               controller.validation().valid() &&
               controller.appliedConfiguration()->simulation.time.dt_s == 0.001,
           "valid draft edit leaked into applied configuration");

    auto invalid_edit = valid_edit;
    invalid_edit.dt_s = 0.01;
    expect(controller.updateDraft(invalid_edit), "invalid edit was ignored");
    expect(controller.state().edit_state == ExperimentEditState::Invalid &&
               !controller.validation().valid() &&
               !controller.validation().issues.isEmpty(),
           "invalid draft did not produce structured validation failure");
    QString error;
    expect(!controller.apply(&error) && !error.isEmpty() &&
               controller.appliedConfiguration()->simulation.time.dt_s == 0.001,
           "failed Apply replaced the last applied configuration");

    expect(controller.updateDraft(valid_edit),
           "valid draft could not replace invalid draft");
    expect(controller.apply(&error) && !controller.state().dirty &&
               controller.state().edit_state == ExperimentEditState::Applied &&
               controller.appliedConfiguration()->simulation.time.dt_s == 0.0009,
           "successful Apply did not publish and clean the draft");

    auto moved_source = valid_edit;
    moved_source.source_location_m.x_m += 25.0;
    expect(controller.updateDraft(moved_source) && controller.state().can_revert,
           "post-Apply edit did not enable Revert");
    expect(controller.revert() && !controller.state().dirty &&
               controller.draft()->source_location_m.x_m ==
                   valid_edit.source_location_m.x_m &&
               controller.appliedConfiguration()->source.physical_location.x_m ==
                   valid_edit.source_location_m.x_m,
           "Revert did not restore the applied domain baseline");
    expect(draft_changes >= 5 && dirty_changes >= 4 &&
               validation_changes >= 3 && applied_changes == 2,
           "controller did not emit meaningful typed transitions");
}

void test_input_error_switch_clear_and_snapshot() {
    ExperimentController controller;
    controller.load(grid(), extrema(), draft());
    const auto initial_applied = controller.appliedConfiguration();
    controller.reportDraftInputError(
        QStringLiteral("experiment.editor_input"),
        QStringLiteral("moment value is not a finite number"),
        ExperimentValidationTarget::Source);
    expect(controller.state().dirty &&
               controller.state().edit_state == ExperimentEditState::Invalid &&
               controller.validation().issues.front().target ==
                   ExperimentValidationTarget::Source &&
               controller.appliedConfiguration()->simulation.time.dt_s ==
                   initial_applied->simulation.time.dt_s,
           "unrepresentable editor input did not preserve applied state");
    expect(controller.revert() && !controller.state().dirty,
           "Revert did not discard an unrepresentable editor edit");

    auto changed = *controller.draft();
    changed.source_location_m.z_m += 25.0;
    expect(controller.updateDraft(changed) && controller.apply(),
           "source Apply failed");
    const auto inspector = make_experiment_inspector_state(
        QStringLiteral("project-a"), changed.shot_id,
        controller.appliedDraft(), controller.appliedConfiguration(),
        false, {}, QStringLiteral("Speed magnitude"), true, true);
    expect(inspector.source.physical_position_m.z_m ==
               changed.source_location_m.z_m,
           "read-only inspector snapshot did not follow applied controller state");

    const auto next = draft(QStringLiteral("shot-002"));
    controller.load(grid(), extrema(), next);
    expect(controller.draft()->shot_id == QStringLiteral("shot-002") &&
               controller.appliedDraft()->shot_id == QStringLiteral("shot-002") &&
               !controller.state().dirty,
           "project/shot switch retained stale controller state");
    controller.clear();
    expect(!controller.hasContext() && !controller.draft() &&
               !controller.appliedConfiguration() &&
               controller.state().edit_state == ExperimentEditState::Empty,
           "project close retained stale experiment state");
}

void test_additional_output_validation_is_transactional() {
    ExperimentController controller;
    controller.load(
        grid(), extrema(), draft(),
        [](const ResolvedExperimentDraft& resolved) {
            if (resolved.simulation.time.dt_s != 0.001) {
                throw std::invalid_argument("output sample axis is unsupported");
            }
        });
    auto changed = *controller.draft();
    changed.dt_s = 0.0009;
    expect(controller.updateDraft(changed) && !controller.validation().valid(),
           "additional output constraint did not participate in validation");
    expect(!controller.apply() &&
               controller.appliedConfiguration()->simulation.time.dt_s == 0.001,
           "failed output validation changed the applied configuration");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        test_load_dirty_validation_apply_and_revert();
        test_input_error_switch_clear_and_snapshot();
        test_additional_output_validation_is_transactional();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}

