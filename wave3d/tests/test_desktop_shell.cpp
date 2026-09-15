#include "wave3d/desktop/main_window.hpp"
#include "wave3d/desktop/project_workspace.hpp"
#include "wave3d/desktop/theme.hpp"

#ifdef WAVE3D_DESKTOP_HAS_HDF5
#include "wave3d/io/hdf5.hpp"
#endif

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
        !require_child<QAction>(window, "importHdf5ModelAction")->isEnabled(),
        "model import must remain gated without an open project");
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
#ifdef WAVE3D_DESKTOP_HAS_HDF5
    expect(
        require_child<QAction>(window, "importHdf5ModelAction")->isEnabled(),
        "HDF5-enabled project did not enable model import");
#else
    expect(
        !require_child<QAction>(window, "importHdf5ModelAction")->isEnabled(),
        "HDF5-off project exposed model import");
    error.clear();
    expect(
        !window.import_hdf5_model(QStringLiteral("unused.h5"), &error) &&
            !error.isEmpty(),
        "HDF5-off model import must fail explicitly");
#endif

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

#ifdef WAVE3D_DESKTOP_HAS_HDF5
wave3d::PhysicalModel model_fixture() {
    const wave3d::Grid3D grid{
        4, 3, 5,
        10.0F, 20.0F, 30.0F,
        6,
        {2, 2}, {2, 2}, {0, 2}};
    auto model = wave3d::make_homogeneous_model(
        grid, {3600.0F, 2000.0F, 2400.0F});
    model.vp_m_s.front() = 3000.0F;
    model.vp_m_s.back() = 4500.0F;
    model.vs_m_s.front() = 1700.0F;
    model.vs_m_s.back() = 2500.0F;
    model.density_kg_m3.front() = 2200.0F;
    model.density_kg_m3.back() = 2700.0F;
    return model;
}

QByteArray file_bytes(const QString& path) {
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("cannot read test file");
    }
    return input.readAll();
}

void test_hdf5_model_import() {
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary model workspace is unavailable");
    const auto project_root =
        QDir(temporary.path()).filePath(QStringLiteral("model-project"));
    const auto second_project_root =
        QDir(temporary.path()).filePath(QStringLiteral("empty-project"));
    const auto source =
        QDir(temporary.path()).filePath(QStringLiteral("fixture.h5"));
    wave3d::io::write_hdf5_model(source.toStdString(), model_fixture());
    const auto original_bytes = file_bytes(source);

    wave3d::desktop::MainWindow window;
    QString error;
    expect(
        !window.import_hdf5_model(source, &error) && !error.isEmpty(),
        "model import without an open project must fail explicitly");
    error.clear();
    expect(
        window.create_project(
            project_root, QStringLiteral("模型导入测试"), &error),
        "model import project creation failed");
    expect(
        require_child<QAction>(window, "importHdf5ModelAction")->isEnabled(),
        "HDF5 import action was not enabled for an open project");
    expect(
        window.import_hdf5_model(source, &error),
        "valid external HDF5 model import failed");

    const auto copied =
        QDir(project_root).filePath(QStringLiteral("models/fixture.h5"));
    expect(
        QFileInfo::exists(source) && QFileInfo::exists(copied),
        "model import must retain the source and create the project copy");
    expect(
        file_bytes(source) == original_bytes &&
            file_bytes(copied) == original_bytes,
        "model import changed the source or did not make an exact copy");
    expect(
        window.current_project()->model_reference ==
            QStringLiteral("models/fixture.h5"),
        "active project model reference is incorrect");
    expect(
        wave3d::desktop::ProjectWorkspace::load(project_root).model_reference ==
            QStringLiteral("models/fixture.h5"),
        "model reference was not persisted");
    expect(
        require_child<QComboBox>(window, "modelPropertySelector")->isEnabled(),
        "model property selector was not enabled");
    expect(
        require_child<QLabel>(window, "modelGridLabel")->text() ==
            QStringLiteral("4 × 3 × 5"),
        "model grid metadata was not populated");
    expect(
        require_child<QLabel>(window, "modelStateBadge")->text() ==
            QStringLiteral("模型已加载"),
        "model state badge was not updated");

    const std::array<std::pair<const char*, QSize>, 4> expected_images{{
        {"volumeViewport", QSize(4, 3)},
        {"xyViewport", QSize(4, 3)},
        {"xzViewport", QSize(4, 5)},
        {"yzViewport", QSize(3, 5)}}};
    for (const auto& [name, size] : expected_images) {
        const auto* viewport = require_child<QOpenGLWidget>(window, name);
        expect(
            viewport->property("hasScientificImage").toBool(),
            "imported model did not populate every scientific viewport");
        expect(
            viewport->property("scientificImageSize").toSize() == size,
            "scientific viewport received a section with incorrect dimensions");
    }

    wave3d::desktop::MainWindow reopened;
    expect(
        reopened.current_project() != nullptr &&
            reopened.current_project()->model_reference ==
                QStringLiteral("models/fixture.h5") &&
            require_child<QOpenGLWidget>(reopened, "xyViewport")
                ->property("hasScientificImage")
                .toBool(),
        "reopening a project did not reload its referenced model");

    auto* property =
        require_child<QComboBox>(window, "modelPropertySelector");
    property->setCurrentIndex(property->findData(QStringLiteral("density")));
    expect(
        require_child<QOpenGLWidget>(window, "yzViewport")
            ->property("hasScientificImage")
            .toBool(),
        "property switching cleared the scientific section");

    error.clear();
    expect(
        !window.import_hdf5_model(source, &error) && !error.isEmpty(),
        "existing project model must not be overwritten");
    expect(
        file_bytes(copied) == original_bytes &&
            wave3d::io::read_hdf5_model(copied.toStdString()).grid.nx == 4,
        "overwrite refusal damaged the existing project model");

    expect(
        window.create_project(
            second_project_root, QStringLiteral("空模型项目"), &error),
        "second project creation failed");
    expect(
        !require_child<QComboBox>(window, "modelPropertySelector")->isEnabled(),
        "switching to an empty project retained the prior model selector");
    expect(
        require_child<QLabel>(window, "modelGridLabel")->text() ==
            QStringLiteral("—"),
        "switching to an empty project retained prior model metadata");
    for (const auto& [name, unused] : expected_images) {
        static_cast<void>(unused);
        expect(
            !require_child<QOpenGLWidget>(window, name)
                 ->property("hasScientificImage")
                 .toBool(),
            "switching to an empty project retained a prior model image");
    }
}
#endif

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
#ifdef WAVE3D_DESKTOP_HAS_HDF5
        test_hdf5_model_import();
#endif
        std::cout << "desktop shell tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "desktop shell test failure: " << error.what() << '\n';
        return 1;
    }
}
