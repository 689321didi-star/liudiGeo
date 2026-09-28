#include "wave3d/desktop/experiment_controller.hpp"

#include <utility>

namespace wave3d::desktop {
namespace {

bool same_point(const PhysicalPoint3D& first, const PhysicalPoint3D& second) {
    return first.x_m == second.x_m && first.y_m == second.y_m &&
           first.z_m == second.z_m;
}

bool same_tensor(
    const SymmetricMomentTensor& first,
    const SymmetricMomentTensor& second) {
    return first.m_xx_nm == second.m_xx_nm &&
           first.m_yy_nm == second.m_yy_nm &&
           first.m_zz_nm == second.m_zz_nm &&
           first.m_xy_nm == second.m_xy_nm &&
           first.m_xz_nm == second.m_xz_nm &&
           first.m_yz_nm == second.m_yz_nm;
}

bool same_acquisition(
    const std::optional<AcquisitionGeometry>& first,
    const std::optional<AcquisitionGeometry>& second) {
    if (first.has_value() != second.has_value()) return false;
    if (!first) return true;
    if (first->mode != second->mode ||
        first->translate_x_m != second->translate_x_m ||
        first->translate_y_m != second->translate_y_m) {
        return false;
    }
    const auto& first_grid = first->rectangular;
    const auto& second_grid = second->rectangular;
    if (first_grid.count_x != second_grid.count_x ||
        first_grid.count_y != second_grid.count_y ||
        first_grid.minimum_x_m != second_grid.minimum_x_m ||
        first_grid.maximum_x_m != second_grid.maximum_x_m ||
        first_grid.minimum_y_m != second_grid.minimum_y_m ||
        first_grid.maximum_y_m != second_grid.maximum_y_m ||
        first_grid.depth_m != second_grid.depth_m) {
        return false;
    }
    const auto& first_line = first->line;
    const auto& second_line = second->line;
    if (first_line.count != second_line.count ||
        first_line.first_x_m != second_line.first_x_m ||
        first_line.first_y_m != second_line.first_y_m ||
        first_line.last_x_m != second_line.last_x_m ||
        first_line.last_y_m != second_line.last_y_m ||
        first_line.depth_m != second_line.depth_m ||
        first->explicit_coordinates.size() !=
            second->explicit_coordinates.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < first->explicit_coordinates.size(); ++index) {
        if (!same_point(
                first->explicit_coordinates[index],
                second->explicit_coordinates[index])) {
            return false;
        }
    }
    return true;
}

bool same_draft(const ExperimentDraft& first, const ExperimentDraft& second) {
    return first.shot_id == second.shot_id &&
           first.model_reference == second.model_reference &&
           first.dt_s == second.dt_s &&
           first.total_time_s == second.total_time_s &&
           first.cfl_safety_factor == second.cfl_safety_factor &&
           first.design_frequency_hz == second.design_frequency_hz &&
           same_point(first.source_location_m, second.source_location_m) &&
           first.source_origin_time_s == second.source_origin_time_s &&
           first.source_mode == second.source_mode &&
           first.explosion_moment_nm == second.explosion_moment_nm &&
           same_tensor(first.moment_tensor_nm, second.moment_tensor_nm) &&
           first.double_couple.scalar_moment_nm ==
               second.double_couple.scalar_moment_nm &&
           first.double_couple.strike_deg == second.double_couple.strike_deg &&
           first.double_couple.dip_deg == second.double_couple.dip_deg &&
           first.double_couple.rake_deg == second.double_couple.rake_deg &&
           first.wavelet.dominant_frequency_hz ==
               second.wavelet.dominant_frequency_hz &&
           first.wavelet.peak_delay_s == second.wavelet.peak_delay_s &&
           first.wavelet.peak_rate_s_inv == second.wavelet.peak_rate_s_inv &&
           same_acquisition(first.acquisition, second.acquisition);
}

bool same_issue(
    const ExperimentValidationIssue& first,
    const ExperimentValidationIssue& second) {
    return first.severity == second.severity && first.code == second.code &&
           first.message == second.message && first.target == second.target;
}

bool same_validation(
    const ExperimentValidationResult& first,
    const ExperimentValidationResult& second) {
    if (first.performed != second.performed ||
        first.issues.size() != second.issues.size()) {
        return false;
    }
    for (qsizetype index = 0; index < first.issues.size(); ++index) {
        if (!same_issue(first.issues[index], second.issues[index])) return false;
    }
    return true;
}

} // namespace

ExperimentController::ExperimentController(QObject* parent) : QObject(parent) {}

void ExperimentController::load(
    Grid3D grid,
    PhysicalModelExtrema extrema,
    ExperimentDraft draft,
    ResolvedValidator resolved_validator) {
    const auto old_dirty = dirty();
    const auto old_state = editState();
    grid_ = std::move(grid);
    extrema_ = std::move(extrema);
    draft_ = std::move(draft);
    revert_draft_ = draft_;
    applied_draft_.reset();
    applied_configuration_.reset();
    input_issue_.reset();
    resolved_validator_ = std::move(resolved_validator);
    validateCurrent();
    if (validation_.valid()) {
        applied_draft_ = draft_;
        applied_configuration_ = validated_configuration_;
    }
    emit contextChanged(true);
    emit draftChanged(*draft_);
    emit validationChanged(validation_);
    if (applied_configuration_) {
        emit appliedConfigurationChanged(*applied_configuration_);
    }
    emitStateChanges(old_dirty, old_state);
}

void ExperimentController::clear() {
    if (!hasContext()) return;
    const auto old_dirty = dirty();
    const auto old_state = editState();
    grid_.reset();
    extrema_.reset();
    draft_.reset();
    revert_draft_.reset();
    applied_draft_.reset();
    validated_configuration_.reset();
    applied_configuration_.reset();
    validation_ = {};
    input_issue_.reset();
    resolved_validator_ = {};
    emit contextChanged(false);
    emit validationChanged(validation_);
    emitStateChanges(old_dirty, old_state);
}

bool ExperimentController::updateDraft(ExperimentDraft draft) {
    if (!hasContext() || !draft_) return false;
    if (!input_issue_ && same_draft(*draft_, draft)) return false;
    const auto old_dirty = dirty();
    const auto old_state = editState();
    const auto previous_validation = validation_;
    draft_ = std::move(draft);
    input_issue_.reset();
    validateCurrent();
    emit draftChanged(*draft_);
    if (!same_validation(previous_validation, validation_)) {
        emit validationChanged(validation_);
    }
    emitStateChanges(old_dirty, old_state);
    return true;
}

void ExperimentController::reportDraftInputError(
    QString code,
    QString message,
    ExperimentValidationTarget target) {
    if (!hasContext()) return;
    const auto old_dirty = dirty();
    const auto old_state = editState();
    const auto previous_validation = validation_;
    input_issue_ = ExperimentValidationIssue{
        ExperimentValidationSeverity::Error,
        std::move(code),
        std::move(message),
        target};
    validateCurrent();
    if (!same_validation(previous_validation, validation_)) {
        emit validationChanged(validation_);
    }
    emitStateChanges(old_dirty, old_state);
}

ExperimentValidationResult ExperimentController::validate() {
    if (!hasContext()) return validation_;
    const auto old_state = editState();
    const auto previous = validation_;
    validateCurrent();
    if (!same_validation(previous, validation_)) {
        emit validationChanged(validation_);
    }
    const auto next_state = editState();
    if (next_state != old_state) emit editStateChanged(next_state);
    return validation_;
}

bool ExperimentController::apply(QString* error_message) {
    if (!hasContext()) {
        if (error_message) *error_message = QStringLiteral("No experiment is loaded");
        return false;
    }
    const auto old_dirty = dirty();
    const auto old_state = editState();
    const auto previous_validation = validation_;
    validateCurrent();
    if (!same_validation(previous_validation, validation_)) {
        emit validationChanged(validation_);
    }
    if (!validation_.valid() || !validated_configuration_) {
        if (error_message) {
            *error_message = validation_.issues.isEmpty()
                                 ? QStringLiteral("Experiment validation failed")
                                 : validation_.issues.front().message;
        }
        emitStateChanges(old_dirty, old_state);
        return false;
    }
    applied_draft_ = draft_;
    revert_draft_ = draft_;
    applied_configuration_ = validated_configuration_;
    emit appliedConfigurationChanged(*applied_configuration_);
    emitStateChanges(old_dirty, old_state);
    return true;
}

bool ExperimentController::revert() {
    if (!draft_ || !revert_draft_ ||
        (!input_issue_ && same_draft(*draft_, *revert_draft_))) {
        return false;
    }
    const auto old_dirty = dirty();
    const auto old_state = editState();
    const auto previous_validation = validation_;
    draft_ = revert_draft_;
    input_issue_.reset();
    validateCurrent();
    emit draftChanged(*draft_);
    if (!same_validation(previous_validation, validation_)) {
        emit validationChanged(validation_);
    }
    emitStateChanges(old_dirty, old_state);
    return true;
}

bool ExperimentController::hasContext() const noexcept {
    return grid_.has_value() && extrema_.has_value() && draft_.has_value();
}

const std::optional<ExperimentDraft>& ExperimentController::draft() const noexcept {
    return draft_;
}

const std::optional<ExperimentDraft>&
ExperimentController::appliedDraft() const noexcept {
    return applied_draft_;
}

const std::optional<ResolvedExperimentDraft>&
ExperimentController::validatedConfiguration() const noexcept {
    return validated_configuration_;
}

const std::optional<ResolvedExperimentDraft>&
ExperimentController::appliedConfiguration() const noexcept {
    return applied_configuration_;
}

const ExperimentValidationResult&
ExperimentController::validation() const noexcept {
    return validation_;
}

ExperimentControllerState ExperimentController::state() const noexcept {
    const auto current_state = editState();
    const auto is_dirty = dirty();
    return {
        current_state,
        is_dirty,
        validation_.performed,
        validation_.valid(),
        applied_configuration_.has_value(),
        validation_.valid() &&
            (is_dirty || !applied_configuration_.has_value()),
        is_dirty && revert_draft_.has_value(),
        applied_configuration_.has_value() && !is_dirty};
}

void ExperimentController::validateCurrent() {
    validation_ = {};
    validated_configuration_.reset();
    if (!hasContext()) return;
    validation_.performed = true;
    if (input_issue_) {
        validation_.issues.push_back(*input_issue_);
        return;
    }
    try {
        auto resolved = ExperimentDraftStore::resolve(
            *draft_, *grid_, *extrema_);
        if (resolved_validator_) resolved_validator_(resolved);
        validated_configuration_ = std::move(resolved);
    } catch (const std::exception& error) {
        validation_.issues.push_back(ExperimentValidationIssue{
            ExperimentValidationSeverity::Error,
            QStringLiteral("experiment.resolve"),
            QString::fromUtf8(error.what()),
            ExperimentValidationTarget::General});
    }
}

void ExperimentController::emitStateChanges(
    bool old_dirty,
    ExperimentEditState old_state) {
    const auto next_dirty = dirty();
    const auto next_state = editState();
    if (next_dirty != old_dirty) emit dirtyChanged(next_dirty);
    if (next_state != old_state) emit editStateChanged(next_state);
}

bool ExperimentController::dirty() const noexcept {
    return input_issue_.has_value() ||
           (draft_ && revert_draft_ && !same_draft(*draft_, *revert_draft_));
}

ExperimentEditState ExperimentController::editState() const noexcept {
    if (!hasContext()) return ExperimentEditState::Empty;
    if (!validation_.performed) return ExperimentEditState::Clean;
    if (!validation_.valid()) return ExperimentEditState::Invalid;
    if (applied_draft_ && draft_ && same_draft(*draft_, *applied_draft_)) {
        return ExperimentEditState::Applied;
    }
    return ExperimentEditState::ValidUnapplied;
}

} // namespace wave3d::desktop
