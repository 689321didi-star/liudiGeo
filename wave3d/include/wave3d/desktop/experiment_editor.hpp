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
    bool import_receiver_csv_file(const QString& path, QString* error = nullptr);
    bool save_acquisition_template_file(
        const QString& path,
        QString* error = nullptr) const;
    bool load_acquisition_template_file(
        const QString& path,
        QString* error = nullptr);
    void show_validation(
        const ResolvedExperimentDraft& resolved,
        bool model_reference_changed);
    void show_validation_error(const QString& message);
    void mark_saved();

private:
    void publish_change();
    void update_source_mode_page();
    void update_receiver_mode_page();
    [[nodiscard]] AcquisitionGeometry current_acquisition() const;
    void populate_acquisition(const AcquisitionGeometry& acquisition);

    std::function<void()> change_callback_;
    std::function<void()> save_callback_;
    QString shot_id_;
    QString model_reference_;
    std::vector<PhysicalPoint3D> explicit_receivers_;
    bool populating_{false};
};

} // namespace wave3d::desktop
