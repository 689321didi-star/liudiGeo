#include "wave3d/desktop/experiment_navigation_guard.hpp"

#include "wave3d/desktop/experiment_controller.hpp"

namespace wave3d::desktop {

bool experiment_navigation_requires_decision(
    const ExperimentController& controller) noexcept {
    return controller.state().dirty;
}

ExperimentNavigationOutcome resolve_experiment_navigation(
    ExperimentController& controller,
    ExperimentNavigationDecision decision,
    QString* error_message) {
    if (error_message != nullptr) error_message->clear();
    if (!experiment_navigation_requires_decision(controller)) {
        return ExperimentNavigationOutcome::Proceed;
    }

    switch (decision) {
    case ExperimentNavigationDecision::Apply:
        return controller.apply(error_message)
                   ? ExperimentNavigationOutcome::Proceed
                   : ExperimentNavigationOutcome::Cancelled;
    case ExperimentNavigationDecision::Discard:
        if (controller.revert()) {
            return ExperimentNavigationOutcome::Proceed;
        }
        if (error_message != nullptr) {
            *error_message = QStringLiteral(
                "The unapplied experiment changes could not be discarded");
        }
        return ExperimentNavigationOutcome::Cancelled;
    case ExperimentNavigationDecision::Cancel:
        return ExperimentNavigationOutcome::Cancelled;
    }

    return ExperimentNavigationOutcome::Cancelled;
}

} // namespace wave3d::desktop
