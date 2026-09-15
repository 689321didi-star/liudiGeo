#include "wave3d/desktop/main_window.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <exception>
#include <utility>

namespace wave3d::desktop {
namespace {

class ScientificViewport final : public QOpenGLWidget,
                                 protected QOpenGLFunctions {
public:
    ScientificViewport(QString title, QString object_name, QWidget* parent)
        : QOpenGLWidget(parent), title_(std::move(title)) {
        setObjectName(std::move(object_name));
        setMinimumSize(220, 160);
        setProperty("openGlReady", false);
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        setProperty(
            "openGlReady", context() != nullptr && context()->isValid());
    }

    void paintGL() override {
        glClearColor(0.035F, 0.055F, 0.075F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(38, 61, 76, 120), 1.0));
        constexpr int grid_spacing = 42;
        for (int x = grid_spacing; x < width(); x += grid_spacing) {
            painter.drawLine(x, 0, x, height());
        }
        for (int y = grid_spacing; y < height(); y += grid_spacing) {
            painter.drawLine(0, y, width(), y);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(18, 46, 62, 225));
        painter.drawRoundedRect(QRect(14, 14, 112, 30), 8, 8);
        painter.setPen(QColor(171, 220, 239));
        painter.drawText(QRect(14, 14, 112, 30), Qt::AlignCenter, title_);

        painter.setPen(QColor(91, 119, 137));
        painter.drawText(
            rect().adjusted(0, 32, 0, 0),
            Qt::AlignCenter,
            QStringLiteral("等待科学数据"));

        painter.setPen(QPen(QColor(45, 75, 92), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 8, 8);
    }

private:
    QString title_;
};

QWidget* make_run_controls(QWidget* parent) {
    auto* group = new QGroupBox(QStringLiteral("正演控制"), parent);
    group->setObjectName(QStringLiteral("runControlGroup"));
    auto* layout = new QVBoxLayout(group);

    auto* state = new QLabel(QStringLiteral("空闲"), group);
    state->setObjectName(QStringLiteral("runStateLabel"));
    layout->addWidget(state);

    auto* progress = new QProgressBar(group);
    progress->setObjectName(QStringLiteral("runProgress"));
    progress->setRange(0, 100);
    progress->setValue(0);
    layout->addWidget(progress);

    auto* start = new QPushButton(QStringLiteral("开始"), group);
    start->setObjectName(QStringLiteral("startRunButton"));
    start->setEnabled(false);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(7);
    buttons->addWidget(start);
    for (const auto& specification : std::array{
             std::pair{QStringLiteral("暂停"), QStringLiteral("pauseRunButton")},
             std::pair{QStringLiteral("继续"), QStringLiteral("resumeRunButton")},
             std::pair{QStringLiteral("停止"), QStringLiteral("stopRunButton")}}) {
        auto* button = new QPushButton(specification.first, group);
        button->setObjectName(specification.second);
        button->setEnabled(false);
        buttons->addWidget(button);
    }
    layout->addLayout(buttons);
    return group;
}

QDockWidget* make_experiment_dock(QMainWindow* window) {
    auto* dock = new QDockWidget(QStringLiteral("实验工作区"), window);
    dock->setObjectName(QStringLiteral("experimentDock"));
    auto* contents = new QWidget(dock);
    auto* layout = new QVBoxLayout(contents);

    auto* navigation = new QListWidget(contents);
    navigation->setObjectName(QStringLiteral("moduleNavigation"));
    navigation->addItems({
        QStringLiteral("项目"),
        QStringLiteral("模型与裁剪"),
        QStringLiteral("工作区"),
        QStringLiteral("震源与炮集"),
        QStringLiteral("观测系统"),
        QStringLiteral("任务队列"),
        QStringLiteral("结果"),
        QStringLiteral("显示设置")});
    navigation->setCurrentRow(0);
    navigation->setMinimumWidth(230);
    navigation->setMinimumHeight(272);
    layout->addWidget(navigation, 1);

    auto* project_group = new QGroupBox(QStringLiteral("当前项目"), contents);
    project_group->setObjectName(QStringLiteral("projectSummaryGroup"));
    auto* project_form = new QFormLayout(project_group);
    auto* project_name = new QLabel(QStringLiteral("未打开"), project_group);
    project_name->setObjectName(QStringLiteral("projectNameLabel"));
    auto* project_path = new QLabel(QStringLiteral("—"), project_group);
    project_path->setObjectName(QStringLiteral("projectPathLabel"));
    project_path->setWordWrap(true);
    auto* shot_count = new QLabel(QStringLiteral("0"), project_group);
    shot_count->setObjectName(QStringLiteral("projectShotCountLabel"));
    project_form->addRow(QStringLiteral("名称："), project_name);
    project_form->addRow(QStringLiteral("路径："), project_path);
    project_form->addRow(QStringLiteral("炮数："), shot_count);
    layout->addWidget(project_group);

    auto* display_group = new QGroupBox(QStringLiteral("共享显示量"), contents);
    auto* display_form = new QFormLayout(display_group);
    auto* field = new QComboBox(display_group);
    field->setObjectName(QStringLiteral("displayFieldSelector"));
    field->addItem(QStringLiteral("速度模"), QStringLiteral("speed"));
    field->addItem(QStringLiteral("Vx"), QStringLiteral("vx"));
    field->addItem(QStringLiteral("Vy"), QStringLiteral("vy"));
    field->addItem(QStringLiteral("Vz"), QStringLiteral("vz"));
    field->addItem(QStringLiteral("散度"), QStringLiteral("divergence"));
    field->addItem(QStringLiteral("旋度模"), QStringLiteral("curl_magnitude"));
    display_form->addRow(QStringLiteral("波场："), field);
    layout->addWidget(display_group);
    layout->addWidget(make_run_controls(contents));

    dock->setWidget(contents);
    return dock;
}

QDockWidget* make_log_dock(QMainWindow* window) {
    auto* dock = new QDockWidget(QStringLiteral("运行日志"), window);
    dock->setObjectName(QStringLiteral("logDock"));
    auto* log = new QTextEdit(dock);
    log->setObjectName(QStringLiteral("runLog"));
    log->setReadOnly(true);
    log->setPlainText(QStringLiteral("Wave3D Studio 已就绪。"));
    dock->setWidget(log);
    return dock;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("wave3dMainWindow"));
    setWindowTitle(QStringLiteral("Wave3D 科研实验工作台"));
    resize(1440, 900);
    setDockOptions(
        QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks |
        QMainWindow::GroupedDragging);

    auto* central = new QWidget(this);
    auto* central_layout = new QVBoxLayout(central);
    central_layout->setContentsMargins(10, 10, 10, 10);
    central_layout->setSpacing(9);

    auto* header = new QFrame(central);
    header->setObjectName(QStringLiteral("workspaceHeader"));
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(16, 11, 16, 11);
    auto* header_text = new QVBoxLayout;
    header_text->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("波场实验工作区"), header);
    title->setObjectName(QStringLiteral("workspaceTitle"));
    auto* subtitle = new QLabel(
        QStringLiteral("四视图同步 · 单GPU · 当前实验未运行"), header);
    subtitle->setObjectName(QStringLiteral("workspaceSubtitle"));
    header_text->addWidget(title);
    header_text->addWidget(subtitle);
    header_layout->addLayout(header_text);
    header_layout->addStretch();
    auto* badge = new QLabel(QStringLiteral("未加载模型"), header);
    badge->setObjectName(QStringLiteral("modelStateBadge"));
    header_layout->addWidget(badge);
    central_layout->addWidget(header);

    auto* horizontal = new QSplitter(Qt::Horizontal, central);
    horizontal->setObjectName(QStringLiteral("fourViewSplitter"));
    horizontal->addWidget(new ScientificViewport(
        QStringLiteral("三维体视图"), QStringLiteral("volumeViewport"), horizontal));

    auto* slices = new QSplitter(Qt::Vertical, horizontal);
    slices->setObjectName(QStringLiteral("sliceViewSplitter"));
    slices->addWidget(new ScientificViewport(
        QStringLiteral("XY 切面"), QStringLiteral("xyViewport"), slices));
    slices->addWidget(new ScientificViewport(
        QStringLiteral("XZ 切面"), QStringLiteral("xzViewport"), slices));
    slices->addWidget(new ScientificViewport(
        QStringLiteral("YZ 切面"), QStringLiteral("yzViewport"), slices));
    horizontal->addWidget(slices);
    horizontal->setStretchFactor(0, 2);
    horizontal->setStretchFactor(1, 1);
    horizontal->setChildrenCollapsible(false);
    slices->setChildrenCollapsible(false);
    central_layout->addWidget(horizontal, 1);
    setCentralWidget(central);

    auto* experiment_dock = make_experiment_dock(this);
    auto* log_dock = make_log_dock(this);
    addDockWidget(Qt::LeftDockWidgetArea, experiment_dock);
    addDockWidget(Qt::BottomDockWidgetArea, log_dock);
    resizeDocks({log_dock}, {145}, Qt::Vertical);

    auto* file_menu = menuBar()->addMenu(QStringLiteral("文件"));
    auto* new_project = file_menu->addAction(QStringLiteral("新建项目"));
    new_project->setObjectName(QStringLiteral("newProjectAction"));
    auto* open_project = file_menu->addAction(QStringLiteral("打开项目"));
    open_project->setObjectName(QStringLiteral("openProjectAction"));

    auto* run_menu = menuBar()->addMenu(QStringLiteral("运行"));
    auto* snapshot = run_menu->addAction(QStringLiteral("保存波场快照（预留）"));
    snapshot->setObjectName(QStringLiteral("snapshotAction"));
    snapshot->setEnabled(false);
    snapshot->setToolTip(QStringLiteral("波场快照写入将在后续增量实现"));

    auto* toolbar = addToolBar(QStringLiteral("主工具栏"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);
    toolbar->addAction(new_project);
    toolbar->addAction(open_project);
    toolbar->addSeparator();
    auto* validate = toolbar->addAction(QStringLiteral("实验预检"));
    validate->setObjectName(QStringLiteral("validateExperimentAction"));
    validate->setEnabled(false);
    auto* run = toolbar->addAction(QStringLiteral("开始正演"));
    run->setObjectName(QStringLiteral("startRunAction"));
    run->setEnabled(false);

    connect(new_project, &QAction::triggered, this, [this] {
        const auto root = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择新的空项目目录"));
        if (root.isEmpty()) {
            return;
        }
        bool accepted = false;
        const auto default_name = QFileInfo(root).fileName();
        const auto name = QInputDialog::getText(
            this,
            QStringLiteral("新建项目"),
            QStringLiteral("项目名称："),
            QLineEdit::Normal,
            default_name,
            &accepted);
        if (!accepted) {
            return;
        }
        QString error;
        if (!create_project(root, name, &error)) {
            QMessageBox::critical(this, QStringLiteral("无法创建项目"), error);
        }
    });
    connect(open_project, &QAction::triggered, this, [this] {
        const auto document = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("打开 Wave3D 项目"),
            QString(),
            QStringLiteral("Wave3D 项目 (project.wave3d.json)"));
        if (document.isEmpty()) {
            return;
        }
        QString error;
        if (!this->open_project(QFileInfo(document).absolutePath(), &error)) {
            QMessageBox::critical(this, QStringLiteral("无法打开项目"), error);
        }
    });

    connect(
        findChild<QComboBox*>(QStringLiteral("displayFieldSelector")),
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            if (!project_) {
                return;
            }
            auto* selector =
                findChild<QComboBox*>(QStringLiteral("displayFieldSelector"));
            project_->display_field = selector->currentData().toString();
            try {
                ProjectWorkspace::save(project_root_, *project_);
            } catch (const std::exception& error) {
                findChild<QTextEdit*>(QStringLiteral("runLog"))
                    ->append(QStringLiteral("保存显示设置失败：%1")
                                 .arg(QString::fromUtf8(error.what())));
            }
        });

    QSettings settings;
    restoreGeometry(
        settings.value(QStringLiteral("desktop/geometry")).toByteArray());
    restoreState(
        settings.value(QStringLiteral("desktop/window_state")).toByteArray());

    statusBar()->showMessage(QStringLiteral("空闲 · 速度模 · 未加载模型"));
    const auto last_project =
        settings.value(QStringLiteral("desktop/last_project")).toString();
    if (!last_project.isEmpty() &&
        QFileInfo(QDir(last_project).filePath(
                      QString::fromUtf8(kDesktopProjectFile)))
            .isFile()) {
        QString ignored_error;
        static_cast<void>(this->open_project(last_project, &ignored_error));
    }
}

bool MainWindow::create_project(
    const QString& root_directory,
    const QString& project_name,
    QString* error_message) {
    try {
        auto project = ProjectWorkspace::create(root_directory, project_name);
        activate_project(QDir(root_directory).absolutePath(), std::move(project));
        return true;
    } catch (const std::exception& error) {
        if (error_message != nullptr) {
            *error_message = QString::fromUtf8(error.what());
        }
        return false;
    }
}

bool MainWindow::open_project(
    const QString& root_directory,
    QString* error_message) {
    try {
        auto project = ProjectWorkspace::load(root_directory);
        activate_project(QDir(root_directory).absolutePath(), std::move(project));
        return true;
    } catch (const std::exception& error) {
        if (error_message != nullptr) {
            *error_message = QString::fromUtf8(error.what());
        }
        return false;
    }
}

const ProjectDocument* MainWindow::current_project() const noexcept {
    return project_ ? &*project_ : nullptr;
}

const QString& MainWindow::current_project_root() const noexcept {
    return project_root_;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    save_window_settings();
    QMainWindow::closeEvent(event);
}

void MainWindow::activate_project(
    QString root_directory,
    ProjectDocument project) {
    project_root_ = std::move(root_directory);
    project_ = std::move(project);

    findChild<QLabel*>(QStringLiteral("projectNameLabel"))->setText(project_->name);
    findChild<QLabel*>(QStringLiteral("projectPathLabel"))->setText(project_root_);
    findChild<QLabel*>(QStringLiteral("projectShotCountLabel"))
        ->setText(QString::number(project_->shots.size()));
    findChild<QLabel*>(QStringLiteral("workspaceTitle"))->setText(project_->name);
    findChild<QLabel*>(QStringLiteral("modelStateBadge"))
        ->setText(
            project_->model_reference.isEmpty() ? QStringLiteral("未加载模型")
                                                 : QStringLiteral("模型已引用"));

    auto* selector = findChild<QComboBox*>(QStringLiteral("displayFieldSelector"));
    const auto index = selector->findData(project_->display_field);
    if (index >= 0) {
        selector->setCurrentIndex(index);
    }

    QSettings settings;
    settings.setValue(QStringLiteral("desktop/last_project"), project_root_);
    statusBar()->showMessage(
        QStringLiteral("项目已打开 · %1 炮 · 等待实验配置")
            .arg(project_->shots.size()));
    findChild<QTextEdit*>(QStringLiteral("runLog"))
        ->append(QStringLiteral("已打开项目：%1").arg(project_root_));
}

void MainWindow::save_window_settings() {
    QSettings settings;
    settings.setValue(QStringLiteral("desktop/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("desktop/window_state"), saveState());
}

} // namespace wave3d::desktop
