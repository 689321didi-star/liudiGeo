#include "wave3d/desktop/main_window.hpp"

#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
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
    layout->addWidget(navigation, 1);

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

    addDockWidget(Qt::LeftDockWidgetArea, make_experiment_dock(this));
    addDockWidget(Qt::BottomDockWidgetArea, make_log_dock(this));

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

    statusBar()->showMessage(QStringLiteral("空闲 · 速度模 · 未加载模型"));
}

} // namespace wave3d::desktop
