#pragma once

#include "wave3d/desktop/experiment_draft.hpp"
#include "wave3d/desktop/experiment_navigation_guard.hpp"
#include "wave3d/desktop/main_window_shell.hpp"
#include "wave3d/desktop/project_workspace.hpp"
#include "wave3d/desktop/segy_model_conversion.hpp"

#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QCloseEvent;
class QTimer;
class QAction;
class QTabWidget;

namespace wave3d::desktop {

class StaticModelScene;
class ProjectNavigator;
class ContextInspector;
class ExperimentController;
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
class ForwardRunWorker;
struct LiveWavefieldFrame;
#endif

class MainWindow final : public MainWindowShell {
public:
    using ExperimentNavigationDecisionProvider =
        std::function<ExperimentNavigationDecision()>;

    explicit MainWindow(
        QWidget* parent = nullptr,
        bool restore_last_project = true,
        ExperimentNavigationDecisionProvider navigation_decision_provider = {});
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
    [[nodiscard]] ProjectNavigator* project_navigator() const noexcept;
    [[nodiscard]] ExperimentController* experiment_controller() const noexcept;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    [[nodiscard]] ExperimentNavigationOutcome
    request_experiment_navigation_permission(
        QString* error_message = nullptr);
    void activate_project(QString root_directory, ProjectDocument project);
    void clear_model_view();
    void configure_experiment_editor();
    void accept_experiment_draft(ExperimentDraft draft);
    void reject_experiment_draft_input(const QString& message);
    void update_experiment_validation();
    void update_experiment_editor_feedback();
    void update_crop_summary();
    void update_model_view();
    void populate_model_information();
    void refresh_project_navigator();
    void refresh_context_inspector();
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
    ProjectNavigator* project_navigator_{nullptr};
    ContextInspector* context_inspector_{nullptr};
    QWidget* legacy_inspector_dialog_{nullptr};
    QTabWidget* legacy_inspector_host_{nullptr};
    ExperimentController* experiment_controller_{nullptr};
    std::optional<PreparedRun> prepared_run_;
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
    std::unique_ptr<ForwardRunWorker> forward_worker_;
    std::shared_ptr<const LiveWavefieldFrame> presented_frame_;
#endif
    QTimer* run_poll_timer_{nullptr};
    QAction* save_project_action_{nullptr};
    QAction* validate_experiment_action_{nullptr};
    QAction* pause_run_action_{nullptr};
    QAction* resume_run_action_{nullptr};
    QAction* stop_run_action_{nullptr};
    ExperimentNavigationDecisionProvider navigation_decision_provider_;
    bool experiment_model_reference_changed_{false};
    int volume_property_index_{-1};
};

} // namespace wave3d::desktop
