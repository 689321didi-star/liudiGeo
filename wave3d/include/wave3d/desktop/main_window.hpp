#pragma once

#include "wave3d/desktop/experiment_draft.hpp"
#include "wave3d/desktop/project_workspace.hpp"
#include "wave3d/desktop/segy_model_conversion.hpp"

#include <QMainWindow>
#include <QString>

#include <memory>
#include <optional>

class QCloseEvent;
class QTimer;

namespace wave3d::desktop {

class StaticModelScene;
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
class ForwardRunWorker;
struct LiveWavefieldFrame;
#endif

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(
        QWidget* parent = nullptr,
        bool restore_last_project = true);
    ~MainWindow() override;

    [[nodiscard]] bool create_project(
        const QString& root_directory,
        const QString& project_name,
        QString* error_message = nullptr);

    [[nodiscard]] bool open_project(
        const QString& root_directory,
        QString* error_message = nullptr);

    [[nodiscard]] bool import_hdf5_model(
        const QString& source_path,
        QString* error_message = nullptr);
    [[nodiscard]] bool convert_segy_model(
        const SegyModelConversionRequest& request,
        QString* error_message = nullptr);
    [[nodiscard]] bool create_cropped_model(
        const QString& output_stem,
        QString* error_message = nullptr);
    [[nodiscard]] bool save_experiment_draft(
        QString* error_message = nullptr);
    [[nodiscard]] bool preflight_experiment(
        const QString& run_id,
        QString* error_message = nullptr);
    [[nodiscard]] bool start_prepared_run(
        QString* error_message = nullptr);

    [[nodiscard]] const ProjectDocument* current_project() const noexcept;
    [[nodiscard]] const QString& current_project_root() const noexcept;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void activate_project(QString root_directory, ProjectDocument project);
    void clear_model_view();
    void configure_experiment_editor();
    void update_experiment_validation();
    void update_crop_summary();
    void update_model_view();
    void populate_model_information();
    void invalidate_prepared_run();
    void poll_forward_run();
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
    void present_live_frame(std::shared_ptr<const LiveWavefieldFrame> frame);
#endif
    void set_run_editing_locked(bool locked);
    void save_window_settings();

    QString project_root_;
    std::optional<ProjectDocument> project_;
    std::unique_ptr<StaticModelScene> model_scene_;
    std::optional<ResolvedExperimentDraft> resolved_experiment_;
    std::optional<PreparedRun> prepared_run_;
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
    std::unique_ptr<ForwardRunWorker> forward_worker_;
    std::shared_ptr<const LiveWavefieldFrame> presented_frame_;
#endif
    QTimer* run_poll_timer_{nullptr};
    bool experiment_model_reference_changed_{false};
    int volume_property_index_{-1};
};

} // namespace wave3d::desktop
