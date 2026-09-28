#pragma once

#include <QString>

namespace wave3d::desktop {

class ExperimentController;

enum class ExperimentNavigationDecision {
    Apply,
    Discard,
    Cancel,
};

enum class ExperimentNavigationOutcome {
    Proceed,
    Cancelled,
};

[[nodiscard]] bool experiment_navigation_requires_decision(
    const ExperimentController& controller) noexcept;

[[nodiscard]] ExperimentNavigationOutcome resolve_experiment_navigation(
    ExperimentController& controller,
    ExperimentNavigationDecision decision,
    QString* error_message = nullptr);

} // namespace wave3d::desktop
