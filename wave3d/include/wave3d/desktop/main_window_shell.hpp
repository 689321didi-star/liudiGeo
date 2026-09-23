#pragma once

#include "wave3d/desktop/selection_context.hpp"

#include <QMainWindow>
#include <QString>

#include <vector>

class QAction;
class QDockWidget;
class QLabel;
class QTabWidget;
class QToolBar;
class QWidget;

namespace wave3d::desktop {

class SelectionController;

struct NavigatorPage final {
    QWidget* widget{nullptr};
    QString title;
    SelectionContext selection;
};

class MainWindowShell : public QMainWindow {
public:
    explicit MainWindowShell(QWidget* parent = nullptr);

    void set_workspace_host(QWidget* workspace);
    void set_navigator_pages(
        NavigatorPage project_page,
        NavigatorPage files_page,
        NavigatorPage workflow_page);
    void set_navigator_selection_context(
        int page_index,
        SelectionContext selection);
    void set_context_inspector(QWidget* inspector);
    void set_bottom_tool_pages(
        QWidget* jobs_page,
        QWidget* log_page,
        QWidget* seismogram_page,
        QWidget* performance_page);

    [[nodiscard]] QToolBar* command_bar() const noexcept;
    [[nodiscard]] QDockWidget* navigator_dock() const noexcept;
    [[nodiscard]] QDockWidget* context_inspector_dock() const noexcept;
    [[nodiscard]] QDockWidget* bottom_tool_dock() const noexcept;
    [[nodiscard]] QTabWidget* navigator_host() const noexcept;
    [[nodiscard]] QTabWidget* context_inspector_host() const noexcept;
    [[nodiscard]] QTabWidget* bottom_tool_area() const noexcept;
    [[nodiscard]] QAction* reset_layout_action() const noexcept;
    [[nodiscard]] SelectionController* selection_controller() const noexcept;

    void reset_default_layout();
    [[nodiscard]] bool restore_v2_layout();
    void save_v2_layout() const;
    void set_run_telemetry(
        const QString& status,
        const QString& step,
        const QString& physical_time,
        const QString& gpu_device);

private:
    QToolBar* command_bar_{nullptr};
    QDockWidget* navigator_dock_{nullptr};
    QDockWidget* context_inspector_dock_{nullptr};
    QDockWidget* bottom_tool_dock_{nullptr};
    QTabWidget* navigator_host_{nullptr};
    QTabWidget* context_inspector_host_{nullptr};
    QTabWidget* bottom_tool_area_{nullptr};
    QAction* reset_layout_action_{nullptr};
    QLabel* telemetry_status_{nullptr};
    QLabel* telemetry_step_{nullptr};
    QLabel* telemetry_time_{nullptr};
    QLabel* telemetry_gpu_{nullptr};
    SelectionController* selection_controller_{nullptr};
    std::vector<SelectionContext> navigator_selections_;
};

} // namespace wave3d::desktop
