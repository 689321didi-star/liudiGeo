#include "wave3d/desktop/main_window.hpp"
#include "wave3d/desktop/project_workspace.hpp"
#include "wave3d/desktop/theme.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QOpenGLWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolBar>

#include <array>
#include <iostream>
#include <stdexcept>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Widget>
Widget* require_child(QWidget& parent, const char* object_name) {
    auto* child = parent.findChild<Widget*>(QString::fromUtf8(object_name));
    if (child == nullptr) {
        throw std::runtime_error(
            std::string("missing desktop child: ") + object_name);
    }
    return child;
}

void test_shell_contract() {
    wave3d::desktop::MainWindow window;
    expect(
        !qApp->styleSheet().isEmpty(),
        "desktop application must use the centralized scientific theme");
    static_cast<void>(require_child<QFrame>(window, "workspaceHeader"));
    static_cast<void>(require_child<QToolBar>(window, "mainToolbar"));
    expect(
        window.findChildren<QOpenGLWidget*>().size() == 4,
        "desktop shell must contain exactly four OpenGL viewports");
    for (const char* name : {
             "volumeViewport", "xyViewport", "xzViewport", "yzViewport"}) {
        static_cast<void>(require_child<QOpenGLWidget>(window, name));
    }

    const auto* four_view = require_child<QSplitter>(window, "fourViewSplitter");
    const auto* slices = require_child<QSplitter>(window, "sliceViewSplitter");
    expect(
        four_view->orientation() == Qt::Horizontal && four_view->count() == 2,
        "desktop shell must place volume and slices horizontally");
    expect(
        slices->orientation() == Qt::Vertical && slices->count() == 3,
        "desktop shell must stack three orthogonal slices");

    const auto* field =
        require_child<QComboBox>(window, "displayFieldSelector");
    const std::array<const char*, 6> expected_fields{
        "speed", "vx", "vy", "vz", "divergence", "curl_magnitude"};
    expect(
        field->count() == static_cast<int>(expected_fields.size()),
        "display field count changed");
    for (int index = 0; index < field->count(); ++index) {
        expect(
            field->itemData(index).toString() ==
                QString::fromUtf8(expected_fields[static_cast<std::size_t>(index)]),
            "display field order or identity changed");
    }
    expect(
        field->currentData().toString() == QStringLiteral("speed"),
        "velocity magnitude must be the initial display field");

    const auto* navigation =
        require_child<QListWidget>(window, "moduleNavigation");
    expect(navigation->count() == 8, "required desktop modules are missing");
    expect(
        navigation->item(3)->text() == QStringLiteral("震源与炮集"),
        "multi-shot navigation entry is missing");
    expect(
        navigation->item(5)->text() == QStringLiteral("任务队列"),
        "optional run queue navigation entry is missing");

    const auto* snapshot = require_child<QAction>(window, "snapshotAction");
    expect(!snapshot->isEnabled(), "wavefield snapshot must remain disabled");
    expect(
        require_child<QLabel>(window, "runStateLabel")->text() ==
            QStringLiteral("空闲"),
        "desktop shell must start idle");
    expect(
        require_child<QProgressBar>(window, "runProgress")->value() == 0,
        "desktop shell progress must start at zero");
    expect(
        !require_child<QPushButton>(window, "startRunButton")->isEnabled(),
        "start control must remain gated without a valid experiment");
    for (const char* name : {
             "pauseRunButton", "resumeRunButton", "stopRunButton"}) {
        expect(
            !require_child<QPushButton>(window, name)->isEnabled(),
            "inactive run controls must start disabled");
    }
}

void test_project_window_state() {
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary project parent is unavailable");
    const auto root = QDir(temporary.path()).filePath(QStringLiteral("project"));

    wave3d::desktop::MainWindow window;
    expect(window.current_project() == nullptr, "window must start without a project");
    expect(
        require_child<QLabel>(window, "projectNameLabel")->text() ==
            QStringLiteral("未打开"),
        "empty project summary is incorrect");

    QString error;
    expect(
        window.create_project(root, QStringLiteral("界面状态测试"), &error),
        "window could not create a valid project");
    expect(error.isEmpty(), "successful project creation reported an error");
    expect(window.current_project() != nullptr, "created project was not activated");
    expect(
        window.current_project_root() == QDir(root).absolutePath(),
        "active project root is not canonical");
    expect(
        require_child<QLabel>(window, "projectNameLabel")->text() ==
            QStringLiteral("界面状态测试"),
        "project name summary was not updated");
    expect(
        require_child<QLabel>(window, "projectShotCountLabel")->text() ==
            QStringLiteral("1"),
        "project shot summary was not updated");
    expect(
        !require_child<QAction>(window, "validateExperimentAction")->isEnabled() &&
            !require_child<QAction>(window, "startRunAction")->isEnabled() &&
            !require_child<QPushButton>(window, "startRunButton")->isEnabled(),
        "scientific actions must remain gated after project creation");

    auto* selector =
        require_child<QComboBox>(window, "displayFieldSelector");
    const auto vz_index = selector->findData(QStringLiteral("vz"));
    expect(vz_index >= 0, "Vz display choice is missing");
    selector->setCurrentIndex(vz_index);
    const auto persisted =
        wave3d::desktop::ProjectWorkspace::load(QDir(root).absolutePath());
    expect(
        persisted.display_field == QStringLiteral("vz"),
        "shared display preference was not persisted");

    QSettings settings;
    expect(
        settings.value(QStringLiteral("desktop/last_project")).toString() ==
            QDir(root).absolutePath(),
        "last project setting was not persisted");
    window.resize(1180, 760);
    window.show();
    QApplication::processEvents();
    expect(window.close(), "desktop window refused normal close");
    settings.sync();
    expect(
        !settings.value(QStringLiteral("desktop/geometry")).toByteArray().isEmpty() &&
            !settings.value(QStringLiteral("desktop/window_state"))
                 .toByteArray()
                 .isEmpty(),
        "window geometry or dock state was not persisted on close");

    wave3d::desktop::MainWindow reopened;
    expect(
        reopened.current_project() != nullptr &&
            reopened.current_project_root() == QDir(root).absolutePath(),
        "last project was not reopened from desktop settings");
    expect(
        require_child<QComboBox>(reopened, "displayFieldSelector")
                ->currentData()
                .toString() == QStringLiteral("vz"),
        "reopened project did not restore its display preference");
    expect(
        !reopened.open_project(
            QDir(temporary.path()).filePath(QStringLiteral("missing")), &error) &&
            !error.isEmpty(),
        "invalid project open must report failure without throwing");
}

} // namespace

int main(int argc, char** argv) {
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Wave3DTests"));
    QApplication::setApplicationName(QStringLiteral("DesktopShellTests"));
    QSettings().clear();
    wave3d::desktop::apply_scientific_theme(application);
    try {
        test_shell_contract();
        test_project_window_state();
        std::cout << "desktop shell tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "desktop shell test failure: " << error.what() << '\n';
        return 1;
    }
}
