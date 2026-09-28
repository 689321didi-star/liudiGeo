#include "wave3d/desktop/experiment_controller.hpp"
#include "wave3d/desktop/experiment_navigation_guard.hpp"

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

ExperimentDraft baseline() {
    return ExperimentDraftStore::defaults(
        QStringLiteral("shot-001"),
        QStringLiteral("models/overthrust.h5"), grid(), extrema());
}

void test_clean_draft_needs_no_decision() {
    ExperimentController controller;
    controller.load(grid(), extrema(), baseline());
    expect(!experiment_navigation_requires_decision(controller),
           "clean Draft unexpectedly requires a navigation decision");
    expect(resolve_experiment_navigation(
               controller, ExperimentNavigationDecision::Cancel) ==
               ExperimentNavigationOutcome::Proceed,
           "clean Draft did not allow navigation without a decision");
}

void test_cancel_is_side_effect_free() {
    ExperimentController controller;
    controller.load(grid(), extrema(), baseline());
    auto changed = *controller.draft();
    changed.wavelet.dominant_frequency_hz = 2.9;
    expect(controller.updateDraft(changed), "valid edit was ignored");
    const auto applied_frequency =
        controller.appliedConfiguration()->source.wavelet.dominant_frequency_hz;
    expect(resolve_experiment_navigation(
               controller, ExperimentNavigationDecision::Cancel) ==
               ExperimentNavigationOutcome::Cancelled &&
               controller.state().dirty &&
               controller.draft()->wavelet.dominant_frequency_hz == 2.9 &&
               controller.appliedConfiguration()
                       ->source.wavelet.dominant_frequency_hz ==
                   applied_frequency,
           "Cancel mutated Draft or Applied state");
}

void test_discard_uses_controller_revert() {
    ExperimentController controller;
    controller.load(grid(), extrema(), baseline());
    auto changed = *controller.draft();
    changed.wavelet.dominant_frequency_hz = 2.9;
    expect(controller.updateDraft(changed), "valid edit was ignored");
    expect(resolve_experiment_navigation(
               controller, ExperimentNavigationDecision::Discard) ==
               ExperimentNavigationOutcome::Proceed &&
               !controller.state().dirty &&
               controller.draft()->wavelet.dominant_frequency_hz == 3.0,
           "Discard did not revert the domain Draft");
}

void test_apply_is_transactional() {
    ExperimentController controller;
    controller.load(grid(), extrema(), baseline());
    auto changed = *controller.draft();
    changed.wavelet.dominant_frequency_hz = 2.9;
    expect(controller.updateDraft(changed), "valid edit was ignored");
    expect(resolve_experiment_navigation(
               controller, ExperimentNavigationDecision::Apply) ==
               ExperimentNavigationOutcome::Proceed &&
               !controller.state().dirty &&
               controller.appliedConfiguration()
                       ->source.wavelet.dominant_frequency_hz == 2.9,
           "valid Apply did not publish before navigation");

    auto invalid = *controller.draft();
    invalid.source_location_m.x_m = -25.0;
    expect(controller.updateDraft(invalid), "invalid edit was ignored");
    QString error;
    expect(resolve_experiment_navigation(
               controller, ExperimentNavigationDecision::Apply, &error) ==
               ExperimentNavigationOutcome::Cancelled &&
               !error.isEmpty() && controller.state().dirty &&
               controller.draft()->source_location_m.x_m == -25.0 &&
               controller.appliedConfiguration()
                       ->source.physical_location.x_m == 2500.0,
           "failed Apply did not block navigation transactionally");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        test_clean_draft_needs_no_decision();
        test_cancel_is_side_effect_free();
        test_discard_uses_controller_revert();
        test_apply_is_transactional();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
