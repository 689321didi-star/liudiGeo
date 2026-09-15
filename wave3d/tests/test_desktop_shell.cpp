#include "wave3d/desktop/main_window.hpp"
#include "wave3d/desktop/theme.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QOpenGLWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
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
        require_child<QPushButton>(window, "startRunButton")->isEnabled(),
        "start control must be initially available");
    for (const char* name : {
             "pauseRunButton", "resumeRunButton", "stopRunButton"}) {
        expect(
            !require_child<QPushButton>(window, name)->isEnabled(),
            "inactive run controls must start disabled");
    }
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    wave3d::desktop::apply_scientific_theme(application);
    try {
        test_shell_contract();
        std::cout << "desktop shell tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "desktop shell test failure: " << error.what() << '\n';
        return 1;
    }
}
