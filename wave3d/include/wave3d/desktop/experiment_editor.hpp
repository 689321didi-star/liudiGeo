#pragma once

#include "wave3d/desktop/experiment_draft.hpp"

#include <QWidget>

#include <functional>

namespace wave3d::desktop {

class ExperimentEditor final : public QWidget {
public:
    explicit ExperimentEditor(QWidget* parent = nullptr);

    void set_callbacks(
        std::function<void()> change_callback,
        std::function<void()> save_callback);
    void set_model_context(
        const Grid3D& grid,
        const ExperimentDraft& draft,
        bool loaded_from_disk,
        bool model_reference_changed);
    void clear_model_context();

    [[nodiscard]] ExperimentDraft current_draft() const;
    void show_validation(
        const ResolvedExperimentDraft& resolved,
        bool model_reference_changed);
    void show_validation_error(const QString& message);
    void mark_saved();

private:
    void publish_change();
    void update_source_mode_page();

    std::function<void()> change_callback_;
    std::function<void()> save_callback_;
    QString shot_id_;
    QString model_reference_;
    bool populating_{false};
};

} // namespace wave3d::desktop
