#include "wave3d/desktop/main_window_shell.hpp"

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QSettings>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QWidget>

namespace wave3d::desktop {
namespace {

constexpr int kV2LayoutStateVersion = 1;

void configure_dock(QDockWidget* dock, Qt::DockWidgetAreas allowed_areas) {
    dock->setAllowedAreas(allowed_areas);
    dock->setFeatures(
        QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
        QDockWidget::DockWidgetFloatable);
}

void add_telemetry_item(
    QToolBar* toolbar,
    const QString& caption,
    QLabel** value,
    const QString& initial_value,
    const char* object_name) {
    auto* label = new QLabel(caption, toolbar);
    label->setProperty("telemetryCaption", true);
    toolbar->addWidget(label);
    *value = new QLabel(initial_value, toolbar);
    (*value)->setObjectName(QString::fromUtf8(object_name));
    (*value)->setProperty("telemetryValue", true);
    toolbar->addWidget(*value);
}

} // namespace

MainWindowShell::MainWindowShell(QWidget* parent)
    : QMainWindow(parent) {
    setObjectName(QStringLiteral("wave3dMainWindow"));
    setWindowTitle(QStringLiteral("Wave3D Studio V2"));
    resize(1440, 900);
    setDockOptions(
        QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks |
        QMainWindow::AllowNestedDocks);

    static_cast<void>(menuBar());

    command_bar_ = addToolBar(QStringLiteral("Command Bar"));
    command_bar_->setObjectName(QStringLiteral("commandBar"));
    command_bar_->setMovable(false);
    command_bar_->setFloatable(false);

    navigator_host_ = new QTabWidget(this);
    navigator_host_->setObjectName(QStringLiteral("navigatorHost"));
    navigator_host_->setDocumentMode(true);
    navigator_dock_ = new QDockWidget(QStringLiteral("Navigator"), this);
    navigator_dock_->setObjectName(QStringLiteral("navigatorDock"));
    navigator_dock_->setWidget(navigator_host_);
    navigator_dock_->setMinimumWidth(230);
    configure_dock(
        navigator_dock_,
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    context_inspector_host_ = new QTabWidget(this);
    context_inspector_host_->setObjectName(
        QStringLiteral("contextInspectorHost"));
    context_inspector_host_->setDocumentMode(true);
    context_inspector_dock_ =
        new QDockWidget(QStringLiteral("Context Inspector"), this);
    context_inspector_dock_->setObjectName(
        QStringLiteral("contextInspectorDock"));
    context_inspector_dock_->setWidget(context_inspector_host_);
    context_inspector_dock_->setMinimumWidth(280);
    configure_dock(
        context_inspector_dock_,
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    bottom_tool_area_ = new QTabWidget(this);
    bottom_tool_area_->setObjectName(QStringLiteral("bottomToolArea"));
    bottom_tool_area_->setDocumentMode(true);
    bottom_tool_area_->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Ignored);
    bottom_tool_dock_ =
        new QDockWidget(QStringLiteral("Bottom Tools"), this);
    bottom_tool_dock_->setObjectName(QStringLiteral("bottomToolDock"));
    bottom_tool_dock_->setWidget(bottom_tool_area_);
    bottom_tool_dock_->setMinimumSize(0, 0);
    bottom_tool_dock_->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Ignored);
    configure_dock(
        bottom_tool_dock_,
        Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);

    addDockWidget(Qt::LeftDockWidgetArea, navigator_dock_);
    addDockWidget(Qt::RightDockWidgetArea, context_inspector_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, bottom_tool_dock_);

    reset_layout_action_ = new QAction(QStringLiteral("重置默认布局"), this);
    reset_layout_action_->setObjectName(QStringLiteral("resetLayoutAction"));
    connect(
        reset_layout_action_, &QAction::triggered,
        this, &MainWindowShell::reset_default_layout);

    statusBar()->setSizeGripEnabled(true);
    statusBar()->showMessage(QStringLiteral("空闲 · 速度模 · 未加载模型"));
}

void MainWindowShell::set_workspace_host(QWidget* workspace) {
    workspace->setObjectName(QStringLiteral("workspaceHost"));
    setCentralWidget(workspace);
}

void MainWindowShell::set_navigator_pages(
    QWidget* project_page,
    QWidget* files_page,
    QWidget* workflow_page) {
    navigator_host_->addTab(project_page, QStringLiteral("Project"));
    navigator_host_->addTab(files_page, QStringLiteral("Files"));
    navigator_host_->addTab(workflow_page, QStringLiteral("Workflow"));
}

void MainWindowShell::set_inspector_pages(
    QWidget* model_page,
    QWidget* experiment_page) {
    context_inspector_host_->addTab(model_page, QStringLiteral("Model"));
    context_inspector_host_->addTab(
        experiment_page, QStringLiteral("Experiment"));
}

void MainWindowShell::set_bottom_tool_pages(
    QWidget* jobs_page,
    QWidget* log_page,
    QWidget* seismogram_page,
    QWidget* performance_page) {
    bottom_tool_area_->addTab(jobs_page, QStringLiteral("Jobs"));
    bottom_tool_area_->addTab(log_page, QStringLiteral("Log"));
    bottom_tool_area_->addTab(seismogram_page, QStringLiteral("Seismogram"));
    bottom_tool_area_->addTab(performance_page, QStringLiteral("Performance"));
    bottom_tool_area_->setCurrentIndex(1);
}

QToolBar* MainWindowShell::command_bar() const noexcept { return command_bar_; }
QDockWidget* MainWindowShell::navigator_dock() const noexcept {
    return navigator_dock_;
}
QDockWidget* MainWindowShell::context_inspector_dock() const noexcept {
    return context_inspector_dock_;
}
QDockWidget* MainWindowShell::bottom_tool_dock() const noexcept {
    return bottom_tool_dock_;
}
QTabWidget* MainWindowShell::navigator_host() const noexcept {
    return navigator_host_;
}
QTabWidget* MainWindowShell::context_inspector_host() const noexcept {
    return context_inspector_host_;
}
QTabWidget* MainWindowShell::bottom_tool_area() const noexcept {
    return bottom_tool_area_;
}
QAction* MainWindowShell::reset_layout_action() const noexcept {
    return reset_layout_action_;
}

void MainWindowShell::reset_default_layout() {
    for (auto* dock : {
             navigator_dock_, context_inspector_dock_, bottom_tool_dock_}) {
        dock->setFloating(false);
        removeDockWidget(dock);
    }
    addDockWidget(Qt::LeftDockWidgetArea, navigator_dock_);
    addDockWidget(Qt::RightDockWidgetArea, context_inspector_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, bottom_tool_dock_);
    navigator_dock_->show();
    context_inspector_dock_->show();
    bottom_tool_dock_->show();
    navigator_host_->setCurrentIndex(0);
    context_inspector_host_->setCurrentIndex(0);
    if (bottom_tool_area_->count() > 1) {
        bottom_tool_area_->setCurrentIndex(1);
    }
    resizeDocks({navigator_dock_}, {250}, Qt::Horizontal);
    resizeDocks({context_inspector_dock_}, {340}, Qt::Horizontal);
    resizeDocks({bottom_tool_dock_}, {180}, Qt::Vertical);
}

bool MainWindowShell::restore_v2_layout() {
    QSettings settings;
    restoreGeometry(
        settings.value(QStringLiteral("desktop/v2/geometry")).toByteArray());
    const auto state =
        settings.value(QStringLiteral("desktop/v2/window_state")).toByteArray();
    if (state.isEmpty()) {
        reset_default_layout();
        return true;
    }
    if (!restoreState(state, kV2LayoutStateVersion)) {
        reset_default_layout();
        return false;
    }
    return true;
}

void MainWindowShell::save_v2_layout() const {
    QSettings settings;
    settings.setValue(QStringLiteral("desktop/v2/geometry"), saveGeometry());
    settings.setValue(
        QStringLiteral("desktop/v2/window_state"),
        saveState(kV2LayoutStateVersion));
}

void MainWindowShell::set_run_telemetry(
    const QString& status,
    const QString& step,
    const QString& physical_time,
    const QString& gpu_device) {
    if (telemetry_status_ == nullptr) {
        command_bar_->addSeparator();
        auto* spacer = new QLabel(command_bar_);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        command_bar_->addWidget(spacer);
        add_telemetry_item(
            command_bar_, QStringLiteral("状态 "), &telemetry_status_,
            status, "runTelemetryStatus");
        command_bar_->addSeparator();
        add_telemetry_item(
            command_bar_, QStringLiteral("步数 "), &telemetry_step_,
            step, "runTelemetryStep");
        command_bar_->addSeparator();
        add_telemetry_item(
            command_bar_, QStringLiteral("物理时间 "), &telemetry_time_,
            physical_time, "runTelemetryPhysicalTime");
        command_bar_->addSeparator();
        add_telemetry_item(
            command_bar_, QStringLiteral("GPU "), &telemetry_gpu_,
            gpu_device, "runTelemetryGpu");
        return;
    }
    telemetry_status_->setText(status);
    telemetry_step_->setText(step);
    telemetry_time_->setText(physical_time);
    telemetry_gpu_->setText(gpu_device);
}

} // namespace wave3d::desktop
