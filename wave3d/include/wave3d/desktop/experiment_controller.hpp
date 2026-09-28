#pragma once

#include "wave3d/desktop/experiment_draft.hpp"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace wave3d::desktop {

enum class ExperimentEditState {
    Empty,
    Clean,
    Invalid,
    ValidUnapplied,
    Applied,
};

enum class ExperimentValidationSeverity {
    Error,
};

enum class ExperimentValidationTarget {
    General,
    Time,
    Numerics,
    Source,
    Acquisition,
    Output,
};

struct ExperimentValidationIssue final {
    ExperimentValidationSeverity severity{ExperimentValidationSeverity::Error};
    QString code;
    QString message;
    ExperimentValidationTarget target{ExperimentValidationTarget::General};
};

struct ExperimentValidationResult final {
    bool performed{false};
    QVector<ExperimentValidationIssue> issues;

    [[nodiscard]] bool valid() const noexcept {
        return performed && issues.isEmpty();
    }
};

struct ExperimentControllerState final {
    ExperimentEditState edit_state{ExperimentEditState::Empty};
    bool dirty{false};
    bool validation_performed{false};
    bool validation_valid{false};
    bool applied{false};
    bool can_apply{false};
    bool can_revert{false};
    bool can_preflight{false};
};

class ExperimentController final : public QObject {
    Q_OBJECT

public:
    using ResolvedValidator =
        std::function<void(const ResolvedExperimentDraft&)>;

    explicit ExperimentController(QObject* parent = nullptr);

    void load(
        Grid3D grid,
        PhysicalModelExtrema extrema,
        ExperimentDraft draft,
        ResolvedValidator resolved_validator = {});
    void clear();

    [[nodiscard]] bool updateDraft(ExperimentDraft draft);
    void reportDraftInputError(
        QString code,
        QString message,
        ExperimentValidationTarget target =
            ExperimentValidationTarget::General);
    [[nodiscard]] ExperimentValidationResult validate();
    [[nodiscard]] bool apply(QString* error_message = nullptr);
    [[nodiscard]] bool revert();

    [[nodiscard]] bool hasContext() const noexcept;
    [[nodiscard]] const std::optional<ExperimentDraft>& draft() const noexcept;
    [[nodiscard]] const std::optional<ExperimentDraft>& appliedDraft() const noexcept;
    [[nodiscard]] const std::optional<ResolvedExperimentDraft>&
    validatedConfiguration() const noexcept;
    [[nodiscard]] const std::optional<ResolvedExperimentDraft>&
    appliedConfiguration() const noexcept;
    [[nodiscard]] const ExperimentValidationResult& validation() const noexcept;
    [[nodiscard]] ExperimentControllerState state() const noexcept;

signals:
    void contextChanged(bool available);
    void draftChanged(const wave3d::desktop::ExperimentDraft& draft);
    void dirtyChanged(bool dirty);
    void validationChanged(
        const wave3d::desktop::ExperimentValidationResult& result);
    void appliedConfigurationChanged(
        const wave3d::desktop::ResolvedExperimentDraft& resolved);
    void editStateChanged(wave3d::desktop::ExperimentEditState state);

private:
    void validateCurrent();
    void emitStateChanges(bool old_dirty, ExperimentEditState old_state);
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] ExperimentEditState editState() const noexcept;

    std::optional<Grid3D> grid_;
    std::optional<PhysicalModelExtrema> extrema_;
    std::optional<ExperimentDraft> draft_;
    std::optional<ExperimentDraft> revert_draft_;
    std::optional<ExperimentDraft> applied_draft_;
    std::optional<ResolvedExperimentDraft> validated_configuration_;
    std::optional<ResolvedExperimentDraft> applied_configuration_;
    ExperimentValidationResult validation_;
    std::optional<ExperimentValidationIssue> input_issue_;
    ResolvedValidator resolved_validator_;
};

} // namespace wave3d::desktop
