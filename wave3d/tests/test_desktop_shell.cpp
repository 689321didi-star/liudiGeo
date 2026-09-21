#include "wave3d/desktop/main_window.hpp"
#include "wave3d/desktop/experiment_editor.hpp"
#include "wave3d/desktop/project_workspace.hpp"
#include "wave3d/desktop/theme.hpp"
#include "wave3d/desktop/volume_viewport.hpp"

#ifdef WAVE3D_DESKTOP_HAS_HDF5
#include "wave3d/io/hdf5.hpp"
#endif
#ifdef WAVE3D_DESKTOP_HAS_YAML
#include "wave3d/io/yaml_config.hpp"
#endif
#ifdef WAVE3D_DESKTOP_HAS_SEGY
#include "wave3d/io/segy.hpp"
#endif

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QToolBar>

#include <algorithm>
#include <array>
#include <cmath>
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
    wave3d::desktop::MainWindow window(nullptr, false);
    window.ensurePolished();
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
    for (const char* name : {
             "sliceXSpin", "sliceYSpin", "sliceZSpin",
             "cropXBeginSpin", "cropXEndSpin", "cropYBeginSpin",
             "cropYEndSpin", "cropZBeginSpin", "cropZEndSpin"}) {
        expect(
            !require_child<QSpinBox>(window, name)->isEnabled(),
            "model index controls must start disabled");
    }
    expect(
        !require_child<QSlider>(window, "volumeOpacitySlider")->isEnabled() &&
            !require_child<QPushButton>(window, "resetVolumeCameraButton")
                 ->isEnabled(),
        "volume controls must start disabled");
    expect(
        !require_child<QWidget>(window, "experimentEditor")->isEnabled() &&
            !require_child<QPushButton>(window, "saveExperimentDraftButton")
                 ->isEnabled(),
        "experiment editor must start disabled without a model");
    const auto* receiver_geometry =
        require_child<QComboBox>(window, "receiverGeometryCombo");
    expect(
        receiver_geometry->count() == 3 &&
            receiver_geometry->model()
                 ->flags(receiver_geometry->model()->index(1, 0))
                 .testFlag(Qt::ItemIsEnabled) &&
            receiver_geometry->model()
                 ->flags(receiver_geometry->model()->index(2, 0))
                 .testFlag(Qt::ItemIsEnabled),
        "line and CSV receiver modes are not enabled");

    const auto* four_view = require_child<QSplitter>(window, "fourViewSplitter");
    const auto* slices = require_child<QSplitter>(window, "sliceViewSplitter");
    auto* navigation =
        require_child<QListWidget>(window, "moduleNavigation");
    expect(
        four_view->orientation() == Qt::Horizontal && four_view->count() == 2,
        "desktop shell must place volume and slices horizontally");
    expect(
        slices->orientation() == Qt::Vertical && slices->count() == 3,
        "desktop shell must stack three orthogonal slices");
    expect(
        four_view->handleWidth() >= 7 && slices->handleWidth() >= 7 &&
            window.statusBar()->isSizeGripEnabled(),
        "window and viewport resize handles must remain directly draggable");
    expect(
        !window.dockOptions().testFlag(QMainWindow::GroupedDragging),
        "tabified editors must detach independently instead of as a dock group");
    expect(
        window.minimumSizeHint().height() <= 920,
        "desktop shell must stay near the accepted 1440 by 900 viewport");
    expect(
        require_child<QDockWidget>(window, "resultsDock")->isHidden(),
        "results workspace must stay hidden until selected");
    navigation->setCurrentRow(6);
    QCoreApplication::processEvents();
    expect(
        !require_child<QDockWidget>(window, "resultsDock")->isHidden() &&
            require_child<QWidget>(window, "workspaceCentral")->isHidden() &&
            window.minimumSizeHint().height() <= 920,
        "selected results workspace must stay near the review viewport");
    navigation->setCurrentRow(2);
    QCoreApplication::processEvents();
    expect(
        require_child<QDockWidget>(window, "resultsDock")->isHidden() &&
            !require_child<QWidget>(window, "workspaceCentral")->isHidden() &&
            !require_child<QDockWidget>(window, "logDock")->isHidden(),
        "leaving results must restore the normal workspace dock");

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
    expect(
        require_child<QSpinBox>(window, "displayIntervalSpin")->value() == 15,
        "measured Overthrust display interval recommendation changed");

    expect(navigation->count() == 8, "required desktop modules are missing");
    expect(
        navigation->item(0)->text() == QStringLiteral("项目概览") &&
            navigation->item(3)->text() == QStringLiteral("震源"),
        "single-shot navigation labels are incorrect");
    expect(
        navigation->item(5)->text() == QStringLiteral("任务队列（后续）") &&
            !navigation->item(5)->flags().testFlag(Qt::ItemIsEnabled) &&
            !navigation->item(5)->toolTip().isEmpty(),
        "deferred run queue must be visible but non-interactive");
    for (const char* name : {
             "toggleExperimentWorkspaceAction", "toggleModelInformationAction",
             "toggleExperimentEditorAction", "toggleRunLogAction",
             "toggleResultsWorkspaceAction", "resetLayoutAction"}) {
        static_cast<void>(require_child<QAction>(window, name));
    }
    auto* editor_dock =
        require_child<QDockWidget>(window, "experimentEditorDock");
    editor_dock->setFloating(true);
    require_child<QAction>(window, "resetLayoutAction")->trigger();
    QCoreApplication::processEvents();
    expect(
        !editor_dock->isFloating() &&
            require_child<QDockWidget>(window, "resultsDock")->isHidden() &&
            !require_child<QDockWidget>(window, "experimentDock")->isHidden(),
        "reset layout did not recover the required desktop docks");

    const auto* snapshot = require_child<QAction>(window, "snapshotAction");
    expect(
        !snapshot->isEnabled() &&
            snapshot->toolTip().contains(QStringLiteral("当前版本不写入")),
        "wavefield snapshot must remain explicitly reserved and disabled");
    expect(
        !require_child<QAction>(window, "importHdf5ModelAction")->isEnabled(),
        "model import must remain gated without an open project");
    expect(
        !require_child<QAction>(window, "convertSegyModelAction")->isEnabled(),
        "SEG-Y model conversion must remain gated without an open project");
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
#if defined(WAVE3D_DESKTOP_HAS_HDF5) && defined(WAVE3D_DESKTOP_HAS_SEGY)
    expect(
        require_child<QAction>(window, "convertSegyModelAction")->isEnabled(),
        "complete I/O build did not enable SEG-Y model conversion");
#else
    expect(
        !require_child<QAction>(window, "convertSegyModelAction")->isEnabled(),
        "incomplete I/O build exposed SEG-Y model conversion");
    error.clear();
    expect(
        !window.convert_segy_model({}, &error) && !error.isEmpty(),
        "incomplete I/O build did not reject SEG-Y model conversion");
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
    require_child<QListWidget>(window, "moduleNavigation")->setCurrentRow(6);
    QApplication::processEvents();
    expect(window.close(), "desktop window refused normal close");
    settings.sync();
    expect(
        !settings.value(QStringLiteral("desktop/geometry")).toByteArray().isEmpty() &&
            !settings.value(QStringLiteral("desktop/window_state"))
                 .toByteArray()
                 .isEmpty() &&
            settings.value(QStringLiteral("desktop/current_module")).toInt() == 6,
        "window geometry, dock state, or active module was not persisted on close");

    wave3d::desktop::MainWindow reopened;
    expect(
        reopened.current_project() != nullptr &&
            reopened.current_project_root() == QDir(root).absolutePath(),
        "last project was not reopened from desktop settings");
    expect(
        require_child<QListWidget>(reopened, "moduleNavigation")->currentRow() == 6 &&
            !require_child<QDockWidget>(reopened, "resultsDock")->isHidden() &&
            require_child<QWidget>(reopened, "workspaceCentral")->isHidden(),
        "reopened desktop did not restore a consistent active module");
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
#ifdef WAVE3D_DESKTOP_HAS_SEGY
    wave3d::MomentTensorSource segy_source{};
    segy_source.physical_location = {0.0, 0.0, 0.0};
    segy_source.storage_location = {0.0, 0.0, 0.0};
    segy_source.moment = {1.0, 1.0, 1.0, 0.0, 0.0, 0.0};
    segy_source.wavelet = {10.0, 0.1, 1.0};
    wave3d::io::ThreeComponentTraces property_volumes{
        4, 2, 0.001,
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}},
        segy_source,
        {3000, 3001, 3010, 3011, 3020, 3021, 3030, 3031},
        {1500, 1501, 1510, 1511, 1520, 1521, 1530, 1531},
        {2200, 2201, 2210, 2211, 2220, 2221, 2230, 2231}};
    const auto vp_segy = QDir(temporary.path()).filePath(QStringLiteral("vp.sgy"));
    const auto vs_segy = QDir(temporary.path()).filePath(QStringLiteral("vs.sgy"));
    const auto rho_segy = QDir(temporary.path()).filePath(QStringLiteral("rho.sgy"));
    wave3d::io::write_component_segy(
        vp_segy.toStdString(), property_volumes, wave3d::io::SegyComponent::Vx);
    wave3d::io::write_component_segy(
        vs_segy.toStdString(), property_volumes, wave3d::io::SegyComponent::Vy);
    wave3d::io::write_component_segy(
        rho_segy.toStdString(), property_volumes, wave3d::io::SegyComponent::Vz);
#endif
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
    expect(
        require_child<QWidget>(window, "experimentEditor")->isEnabled() &&
            require_child<QLabel>(window, "workspaceGridSummaryLabel")
                ->text()
                .contains(QStringLiteral("4 × 3 × 5")) &&
            require_child<QLabel>(window, "workspaceGridSummaryLabel")
                ->text()
                .contains(QStringLiteral("只读")) &&
            require_child<QLabel>(window, "experimentValidationLabel")
                ->text()
                .startsWith(QStringLiteral("参数有效")),
        "model did not configure a valid read-only-grid experiment draft");
    expect(
        require_child<QSpinBox>(window, "receiverCountXSpin")->value() == 101 &&
            require_child<QSpinBox>(window, "receiverCountYSpin")->value() == 101 &&
            require_child<QLabel>(window, "receiverComponentsLabel")->text() ==
                QStringLiteral("Vx / Vy / Vz") &&
            require_child<QLabel>(window, "acquisitionEstimateLabel")
                ->text()
                .contains(QStringLiteral("10201 个接收器")),
        "model did not configure the 101 by 101 three-component acquisition");
#if defined(WAVE3D_DESKTOP_HAS_YAML) && defined(WAVE3D_DESKTOP_HAS_SEGY)
    expect(
        require_child<QAction>(window, "validateExperimentAction")->isEnabled(),
        "valid HDF5/YAML desktop experiment did not enable preflight");
#else
    expect(
        !require_child<QAction>(window, "validateExperimentAction")->isEnabled(),
        "desktop without the complete HDF5/YAML/SEG-Y adapters exposed preflight");
#endif
    auto* source_modes =
        require_child<QComboBox>(window, "sourceModeCombo");
    expect(
        source_modes->count() == 3 &&
            source_modes->model()
                 ->flags(source_modes->model()->index(2, 0))
                 .testFlag(Qt::ItemIsEnabled),
        "double-couple source option is not enabled");

    source_modes->setCurrentIndex(2);
    require_child<QLineEdit>(window, "doubleCoupleMomentEdit")
        ->setText(QStringLiteral("2e12"));
    require_child<QDoubleSpinBox>(window, "doubleCoupleStrikeSpin")
        ->setValue(0.0);
    require_child<QDoubleSpinBox>(window, "doubleCoupleDipSpin")
        ->setValue(90.0);
    require_child<QDoubleSpinBox>(window, "doubleCoupleRakeSpin")
        ->setValue(0.0);
    expect(
        require_child<QLabel>(window, "doubleCoupleTensorLabel")
                ->text()
                .contains(QStringLiteral("Mxy=2e+12")) &&
            require_child<QPushButton>(window, "saveExperimentDraftButton")
                ->isEnabled(),
        "valid double-couple parameters did not show the resolved tensor");
    error.clear();
    expect(
        window.save_experiment_draft(&error),
        "valid double-couple draft save failed");
    const auto saved_double_couple =
        wave3d::desktop::ExperimentDraftStore::load(
            project_root, QStringLiteral("shot-001"));
    expect(
        saved_double_couple.source_mode ==
                wave3d::desktop::DraftSourceMode::DoubleCouple &&
            saved_double_couple.double_couple.scalar_moment_nm == 2.0e12 &&
            saved_double_couple.double_couple.dip_deg == 90.0,
        "double-couple UI values did not persist");

    const std::array<std::pair<const char*, QSize>, 3> expected_images{{
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
    auto* volume_base = require_child<QOpenGLWidget>(window, "volumeViewport");
    auto* volume =
        dynamic_cast<wave3d::desktop::VolumeViewport*>(volume_base);
    expect(
        volume != nullptr &&
            volume->property("volumeTextureDimensions").toString() ==
                QStringLiteral("4x3x5") &&
            volume->property("uploadedModelProperty").toString() ==
                QStringLiteral("vp") &&
            require_child<QSlider>(window, "volumeOpacitySlider")->isEnabled(),
        "model did not synchronize the 3-D volume texture state");
    volume->set_camera(0.9F, 2.0F, 0.2F);
    expect(
        volume->property("volumeCameraYaw").toFloat() == 0.9F &&
            volume->property("volumeCameraPitch").toFloat() == 1.35F &&
            volume->property("volumeCameraDistance").toFloat() == 1.15F,
        "volume camera did not apply orbit/zoom limits");
    require_child<QPushButton>(window, "resetVolumeCameraButton")->click();
    expect(
        volume->property("volumeCameraYaw").toFloat() == 0.65F &&
            volume->property("volumeCameraPitch").toFloat() == 0.42F &&
            volume->property("volumeCameraDistance").toFloat() == 1.75F,
        "volume camera reset did not restore the review view");
    QMouseEvent press(
        QEvent::MouseButtonPress,
        QPointF(100.0, 100.0),
        QPointF(100.0, 100.0),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(volume, &press);
    QMouseEvent move(
        QEvent::MouseMove,
        QPointF(120.0, 110.0),
        QPointF(120.0, 110.0),
        Qt::NoButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(volume, &move);
    expect(
        std::abs(volume->property("volumeCameraYaw").toFloat() - 0.49F) <
                1.0e-5F &&
            std::abs(
                volume->property("volumeCameraPitch").toFloat() - 0.50F) <
                1.0e-5F,
        "volume drag must move the grabbed volume in the mouse direction");
    auto* editor_dock =
        require_child<QDockWidget>(window, "experimentEditorDock");
    auto* frequency =
        require_child<QDoubleSpinBox>(window, "designFrequencySpin");
    window.show();
    auto* module_navigation =
        require_child<QListWidget>(window, "moduleNavigation");
    auto* editor_scroll =
        require_child<QScrollArea>(window, "experimentEditorScroll");
    const auto expect_editor_section_top = [&](int row, const char* object_name) {
        module_navigation->setCurrentRow(row);
        QApplication::processEvents();
        auto* target = require_child<QGroupBox>(window, object_name);
        const auto requested =
            target->mapTo(editor_scroll->widget(), QPoint{}).y();
        const auto expected = std::min(
            requested, editor_scroll->verticalScrollBar()->maximum());
        expect(
            std::abs(editor_scroll->verticalScrollBar()->value() - expected) <= 1,
            "experiment navigation did not align the selected section top");
    };
    expect_editor_section_top(2, "workspaceEditorGroup");
    expect_editor_section_top(3, "sourceEditorGroup");
    expect_editor_section_top(4, "acquisitionEditorGroup");
    module_navigation->setCurrentRow(7);
    QApplication::processEvents();
    auto* model_scroll =
        require_child<QScrollArea>(window, "modelInformationScroll");
    auto* rendering_group =
        require_child<QGroupBox>(window, "volumeRenderingGroup");
    expect(
        std::abs(
            model_scroll->verticalScrollBar()->value() -
            std::min(
                rendering_group->mapTo(model_scroll->widget(), QPoint{}).y(),
                model_scroll->verticalScrollBar()->maximum())) <= 1,
        "display navigation did not reveal the transfer controls");
    module_navigation->setCurrentRow(6);
    QApplication::processEvents();
    expect(
        !require_child<QDockWidget>(window, "resultsDock")->isHidden(),
        "results navigation did not expose the gather workspace");
    module_navigation->setCurrentRow(3);
    QApplication::processEvents();
    editor_dock->setFloating(true);
    editor_dock->show();
    editor_dock->raise();
    editor_dock->activateWindow();
    frequency->setFocus(Qt::ActiveWindowFocusReason);
    QApplication::processEvents();
    const auto frequency_before = frequency->value();
    QKeyEvent increase_frequency(
        QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(frequency, &increase_frequency);
    expect(
        editor_dock->isFloating() && editor_dock->isEnabled() &&
            frequency->isEnabled() && frequency->value() > frequency_before,
        "detached experiment editor did not accept interactive input");
    editor_dock->setFloating(false);
    QApplication::processEvents();
    require_child<QSlider>(window, "volumeOpacitySlider")->setValue(70);
    require_child<QSlider>(window, "volumeThresholdSlider")->setValue(20);
    expect(
        volume->property("volumeOpacity").toFloat() == 0.7F &&
            volume->property("volumeLowerThreshold").toFloat() == 0.2F,
        "volume transfer controls did not update renderer state");
    for (const char* name : {"xyViewport", "xzViewport", "yzViewport"}) {
        expect(
            require_child<QOpenGLWidget>(window, name)
                ->property("sourceMarkerVisible")
                .toBool(),
            "valid default source was not synchronized to every viewport");
    }
    expect(
        volume->property("sourceMarkerPosition").toList().size() == 3,
        "valid default source was not synchronized to the 3-D viewport");
    expect(
        volume->property("receiverCount").toULongLong() == 10201,
        "default receivers were not synchronized to the 3-D viewport");

    auto* receiver_geometry =
        require_child<QComboBox>(window, "receiverGeometryCombo");
    receiver_geometry->setCurrentIndex(1);
    require_child<QSpinBox>(window, "receiverLineCountSpin")->setValue(3);
    require_child<QDoubleSpinBox>(window, "receiverLineFirstXSpin")
        ->setValue(0.0);
    require_child<QDoubleSpinBox>(window, "receiverLineFirstYSpin")
        ->setValue(10.0);
    require_child<QDoubleSpinBox>(window, "receiverLineLastXSpin")
        ->setValue(30.0);
    require_child<QDoubleSpinBox>(window, "receiverLineLastYSpin")
        ->setValue(10.0);
    expect(
        volume->property("receiverCount").toULongLong() == 3 &&
            require_child<QPushButton>(window, "saveExperimentDraftButton")
                ->isEnabled(),
        "valid receiver line did not update the resolved overlay");

    const auto csv_path = QDir(project_root).filePath(QStringLiteral("receivers.csv"));
    QFile csv_file(csv_path);
    expect(csv_file.open(QIODevice::WriteOnly), "cannot create receiver CSV fixture");
    expect(
        csv_file.write("x_m,y_m,z_m\n0,0,0\n10,10,0\n20,20,0\n") > 0,
        "cannot write receiver CSV fixture");
    csv_file.close();
    auto* editor = dynamic_cast<wave3d::desktop::ExperimentEditor*>(
        require_child<QWidget>(window, "experimentEditor"));
    expect(editor != nullptr, "experiment editor type is unavailable");
    error.clear();
    expect(
        editor->import_receiver_csv_file(csv_path, &error) &&
            receiver_geometry->currentIndex() == 2 &&
            require_child<QLabel>(window, "explicitReceiverCountLabel")
                ->text()
                .contains(QStringLiteral("3 个")) &&
            volume->property("receiverCount").toULongLong() == 3,
        "CSV receiver import did not preserve and resolve three rows");
    require_child<QDoubleSpinBox>(window, "receiverTranslateXSpin")
        ->setValue(5.0);
    expect(
        require_child<QPushButton>(window, "saveExperimentDraftButton")
            ->isEnabled(),
        "in-domain receiver translation was rejected");
    const auto template_path =
        QDir(project_root).filePath(QStringLiteral("fixture.wave3d-acquisition.json"));
    error.clear();
    expect(
        editor->save_acquisition_template_file(template_path, &error),
        "acquisition template save failed");
    receiver_geometry->setCurrentIndex(1);
    error.clear();
    expect(
        editor->load_acquisition_template_file(template_path, &error) &&
            receiver_geometry->currentIndex() == 2 &&
            require_child<QDoubleSpinBox>(window, "receiverTranslateXSpin")
                    ->value() == 5.0 &&
            volume->property("receiverCount").toULongLong() == 3,
        "acquisition template load did not restore CSV geometry");
    receiver_geometry->setCurrentIndex(0);
    require_child<QDoubleSpinBox>(window, "receiverTranslateXSpin")
        ->setValue(0.0);

    require_child<QDoubleSpinBox>(window, "sourceXSpin")->setValue(15.0);
    require_child<QDoubleSpinBox>(window, "totalTimeSpin")->setValue(4.25);
    require_child<QComboBox>(window, "sourceModeCombo")->setCurrentIndex(1);
    require_child<QSpinBox>(window, "receiverCountXSpin")->setValue(5);
    require_child<QSpinBox>(window, "receiverCountYSpin")->setValue(4);
    for (const char* name : {
             "momentMxxEdit", "momentMyyEdit", "momentMzzEdit",
             "momentMxzEdit", "momentMyzEdit"}) {
        require_child<QLineEdit>(window, name)->setText(QStringLiteral("0"));
    }
    require_child<QLineEdit>(window, "momentMxyEdit")
        ->setText(QStringLiteral("1e12"));
    expect(
        require_child<QPushButton>(window, "saveExperimentDraftButton")
                ->isEnabled() &&
            require_child<QLabel>(window, "experimentSaveStateLabel")->text() ==
                QStringLiteral("有未保存修改"),
        "valid source edits did not produce a saveable dirty draft");
    error.clear();
    expect(
        window.save_experiment_draft(&error),
        "valid workspace/source draft save failed");
    const auto draft_path = QDir(project_root).filePath(
        QStringLiteral("source/shot-001.experiment.json"));
    const auto saved_draft = wave3d::desktop::ExperimentDraftStore::load(
        project_root, QStringLiteral("shot-001"));
    expect(
        QFileInfo::exists(draft_path) && saved_draft.total_time_s == 4.25 &&
            saved_draft.source_location_m.x_m == 15.0 &&
            saved_draft.source_mode ==
                wave3d::desktop::DraftSourceMode::MomentTensor &&
            saved_draft.moment_tensor_nm.m_xy_nm == 1.0e12 &&
            saved_draft.acquisition.has_value() &&
            saved_draft.acquisition->rectangular.count_x == 5 &&
            saved_draft.acquisition->rectangular.count_y == 4 &&
            volume->property("receiverCount").toULongLong() == 20,
        "saved UI draft did not preserve workspace/source/acquisition values");
    const auto source_marker = volume->property("sourceMarkerPosition").toList();
    expect(
        source_marker.size() == 3 && source_marker[0].toFloat() == 0.5F &&
            source_marker[1].toFloat() == 0.5F &&
            source_marker[2].toFloat() == 0.25F,
        "edited source did not map to normalized 3-D model coordinates");

    require_child<QDoubleSpinBox>(window, "timeStepMsSpin")->setValue(10.0);
    expect(
        !require_child<QPushButton>(window, "saveExperimentDraftButton")
             ->isEnabled() &&
            require_child<QLabel>(window, "experimentValidationLabel")
                ->text()
                .startsWith(QStringLiteral("参数无效")) &&
            volume->property("sourceMarkerPosition").toList().isEmpty(),
        "invalid CFL state remained saveable or retained a source marker");
    require_child<QDoubleSpinBox>(window, "timeStepMsSpin")->setValue(1.0);

#ifdef WAVE3D_DESKTOP_HAS_SEGY
    require_child<QDoubleSpinBox>(window, "timeStepMsSpin")->setValue(0.5005);
    expect(
        !require_child<QPushButton>(window, "saveExperimentDraftButton")
             ->isEnabled() &&
            require_child<QLabel>(window, "experimentValidationLabel")
                ->text()
                .contains(QStringLiteral("SEG-Y")),
        "fractional-microsecond SEG-Y interval remained saveable");
    require_child<QDoubleSpinBox>(window, "timeStepMsSpin")->setValue(1.0);
#endif

    require_child<QDoubleSpinBox>(window, "receiverMaxXSpin")->setValue(50.0);
    expect(
        !require_child<QPushButton>(window, "saveExperimentDraftButton")
             ->isEnabled() &&
            !require_child<QAction>(window, "validateExperimentAction")
                 ->isEnabled() &&
            volume->property("receiverCount").toULongLong() == 0,
        "out-of-domain receiver aperture remained saveable or visible");
    require_child<QDoubleSpinBox>(window, "receiverMaxXSpin")->setValue(30.0);

#if defined(WAVE3D_DESKTOP_HAS_YAML) && defined(WAVE3D_DESKTOP_HAS_SEGY)
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
    require_child<QDoubleSpinBox>(window, "totalTimeSpin")->setValue(0.02);
    error.clear();
    expect(
        window.save_experiment_draft(&error),
        "short CUDA desktop fixture could not be saved");
#endif
    error.clear();
    expect(
        window.preflight_experiment(QStringLiteral("run-001"), &error),
        "valid experiment preflight failed");
    const auto prepared_config =
        QDir(project_root).filePath(QStringLiteral("runs/run-001/config.yaml"));
    const auto prepared_manifest =
        QDir(project_root).filePath(QStringLiteral("runs/run-001/manifest.json"));
    expect(
        QFileInfo::exists(prepared_config) && QFileInfo::exists(prepared_manifest),
        "preflight did not publish its immutable config and manifest");
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
    expect(
        require_child<QAction>(window, "startRunAction")->isEnabled() &&
            require_child<QPushButton>(window, "startRunButton")->isEnabled(),
        "complete production build did not enable the preflighted run");
    error.clear();
    expect(
        window.start_prepared_run(&error),
        "preflighted desktop run did not start");
    expect(
        !require_child<QWidget>(window, "experimentEditor")->isEnabled() &&
            require_child<QPushButton>(window, "stopRunButton")->isEnabled(),
        "active desktop run did not lock editing or enable stop");
    const auto result_path =
        QDir(project_root).filePath(QStringLiteral("runs/run-001/result.json"));
    QElapsedTimer run_timer;
    run_timer.start();
    qulonglong observed_sequence = 0;
    bool observed_synchronized_sections = false;
    while (!QFileInfo::exists(result_path) && run_timer.elapsed() < 15000) {
        QCoreApplication::processEvents();
        observed_sequence = std::max(
            observed_sequence,
            window.property("lastPresentedLiveFrameSequence").toULongLong());
        const auto xy_sequence =
            require_child<QOpenGLWidget>(window, "xyViewport")
                ->property("lastPresentedLiveFrameSequence")
                .toULongLong();
        const auto xz_sequence =
            require_child<QOpenGLWidget>(window, "xzViewport")
                ->property("lastPresentedLiveFrameSequence")
                .toULongLong();
        const auto yz_sequence =
            require_child<QOpenGLWidget>(window, "yzViewport")
                ->property("lastPresentedLiveFrameSequence")
                .toULongLong();
        observed_synchronized_sections =
            observed_synchronized_sections ||
            (xy_sequence > 0 && xy_sequence == xz_sequence &&
             xy_sequence == yz_sequence);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
    expect(
        QFileInfo::exists(result_path) &&
            QFileInfo::exists(QDir(project_root).filePath(
                QStringLiteral("runs/run-001/output/record_vx.sgy"))) &&
            QFileInfo::exists(QDir(project_root).filePath(
                QStringLiteral("runs/run-001/output/record_vy.sgy"))) &&
            QFileInfo::exists(QDir(project_root).filePath(
                QStringLiteral("runs/run-001/output/record_vz.sgy"))) &&
            require_child<QLabel>(window, "runStateLabel")->text() ==
                QStringLiteral("正演完成") &&
            require_child<QProgressBar>(window, "runProgress")->value() == 100 &&
            require_child<QWidget>(window, "experimentEditor")->isEnabled() &&
            require_child<QWidget>(window, "resultWorkspace")
                    ->property("completedResultRunCount")
                    .toInt() == 1 &&
            require_child<QWidget>(window, "resultWorkspace")
                    ->property("selectedResultRun")
                    .toString() == QStringLiteral("run-001") &&
            observed_sequence > 0 && observed_synchronized_sections &&
            !volume->property("liveWavefieldReady").toBool(),
        "background desktop run did not publish and restore the UI");
    require_child<QListWidget>(window, "moduleNavigation")->setCurrentRow(6);
    QCoreApplication::processEvents();
    expect(
        !require_child<QDockWidget>(window, "resultsDock")->isHidden() &&
            require_child<QListWidget>(window, "moduleNavigation")
                    ->currentRow() == 6,
        "results navigation did not expose the results workspace");
#else
    expect(
        !require_child<QAction>(window, "startRunAction")->isEnabled() &&
            !require_child<QPushButton>(window, "startRunButton")->isEnabled(),
        "desktop without CUDA execution exposed the start controls");
#endif
    const auto prepared = wave3d::io::load_yaml_run_configuration(
        prepared_config.toStdString());
    expect(
        prepared.receiver_coordinates_m.size() == 20 &&
            prepared.receiver_coordinates_m.front().x_m == 0.0 &&
            prepared.receiver_coordinates_m.back().x_m == 30.0 &&
            prepared.receiver_coordinates_m.back().y_m == 40.0 &&
            prepared.model_hdf5_path == "../../models/fixture.h5" &&
            prepared.output_directory == "output",
        "preflight YAML did not round trip model, output, or receiver geometry");
    error.clear();
    expect(
        !window.preflight_experiment(QStringLiteral("run-001"), &error) &&
            !error.isEmpty(),
        "preflight overwrote an existing immutable run");
#else
    error.clear();
    expect(
        !window.preflight_experiment(QStringLiteral("run-001"), &error) &&
            !error.isEmpty(),
        "incomplete-adapter preflight did not fail explicitly");
#endif

    auto* slice_z = require_child<QSpinBox>(window, "sliceZSpin");
    expect(
        slice_z->minimum() == 0 && slice_z->maximum() == 4 &&
            slice_z->value() == 2,
        "slice controls were not configured from model dimensions");
    const auto prior_key =
        require_child<QOpenGLWidget>(window, "xyViewport")
            ->property("scientificImageCacheKey")
            .toULongLong();
    slice_z->setValue(1);
    expect(
        require_child<QOpenGLWidget>(window, "xyViewport")
                ->property("scientificImageCacheKey")
                .toULongLong() != prior_key,
        "changing a slice index did not publish a new synchronized image");

    wave3d::desktop::MainWindow reopened;
    expect(
        reopened.current_project() != nullptr &&
            reopened.current_project()->model_reference ==
                QStringLiteral("models/fixture.h5"),
        "reopening a project did not reload its referenced model identity");
    require_child<QListWidget>(reopened, "moduleNavigation")->setCurrentRow(0);
    QApplication::processEvents();
    expect(
            require_child<QOpenGLWidget>(reopened, "xyViewport")
                ->property("hasScientificImage")
                .toBool(),
        "leaving restored results did not reload the referenced model view");
    expect(
        require_child<QDoubleSpinBox>(reopened, "totalTimeSpin")->value() ==
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
                0.02 &&
#else
                4.25 &&
#endif
            require_child<QDoubleSpinBox>(reopened, "sourceXSpin")->value() ==
                15.0 &&
            require_child<QComboBox>(reopened, "sourceModeCombo")
                    ->currentIndex() == 1,
        "reopening a project did not restore the active-shot experiment draft");

    auto* property =
        require_child<QComboBox>(window, "modelPropertySelector");
    property->setCurrentIndex(property->findData(QStringLiteral("density")));
    expect(
        require_child<QOpenGLWidget>(window, "yzViewport")
            ->property("hasScientificImage")
            .toBool(),
        "property switching cleared the scientific section");
    expect(
        volume->property("uploadedModelProperty").toString() ==
            QStringLiteral("density"),
        "property switching did not update the 3-D volume state");

    error.clear();
    expect(
        !window.import_hdf5_model(source, &error) && !error.isEmpty(),
        "existing project model must not be overwritten");
    expect(
        file_bytes(copied) == original_bytes &&
            wave3d::io::read_hdf5_model(copied.toStdString()).grid.nx == 4,
        "overwrite refusal damaged the existing project model");

    require_child<QSpinBox>(window, "cropXBeginSpin")->setValue(1);
    require_child<QSpinBox>(window, "cropXEndSpin")->setValue(3);
    require_child<QSpinBox>(window, "cropYBeginSpin")->setValue(0);
    require_child<QSpinBox>(window, "cropYEndSpin")->setValue(1);
    require_child<QSpinBox>(window, "cropZBeginSpin")->setValue(1);
    require_child<QSpinBox>(window, "cropZEndSpin")->setValue(4);
    const auto volume_crop =
        volume->property("volumeCropBounds").toList();
    expect(
        require_child<QPushButton>(window, "createCropButton")->isEnabled() &&
            volume_crop.size() == 6 &&
            volume_crop[0].toFloat() == (1.0F / 3.0F) &&
            volume_crop[1].toFloat() == 1.0F &&
            volume_crop[2].toFloat() == 0.0F &&
            volume_crop[3].toFloat() == 0.5F &&
            volume_crop[4].toFloat() == 0.25F &&
            volume_crop[5].toFloat() == 1.0F,
        "valid crop controls did not enable derivation");
    error.clear();
    expect(
        window.create_cropped_model(QStringLiteral("ui_crop"), &error),
        "valid UI crop derivation failed");
    const auto crop_path =
        QDir(project_root).filePath(QStringLiteral("models/ui_crop.h5"));
    const auto provenance_path = QDir(project_root).filePath(
        QStringLiteral("manifests/models/ui_crop.json"));
    expect(
        QFileInfo::exists(crop_path) && QFileInfo::exists(provenance_path) &&
            QFileInfo::exists(copied),
        "UI crop did not retain source/model/provenance artifacts");
    expect(
        window.current_project()->model_reference ==
                QStringLiteral("models/ui_crop.h5") &&
            wave3d::desktop::ProjectWorkspace::load(project_root)
                    .model_reference == QStringLiteral("models/ui_crop.h5") &&
            wave3d::io::read_hdf5_model(crop_path.toStdString()).grid.nx == 3 &&
            require_child<QSpinBox>(window, "sliceZSpin")->maximum() == 3,
        "UI crop did not activate the derived model or reset its controls");

    require_child<QSpinBox>(window, "cropXBeginSpin")->setValue(2);
    require_child<QSpinBox>(window, "cropXEndSpin")->setValue(1);
    expect(
        !require_child<QPushButton>(window, "createCropButton")->isEnabled(),
        "invalid crop range did not disable derivation");
    error.clear();
    expect(
        !window.create_cropped_model(QStringLiteral("invalid_crop"), &error) &&
            !error.isEmpty(),
        "invalid UI crop bounds must fail explicitly");

#ifdef WAVE3D_DESKTOP_HAS_SEGY
    error.clear();
    expect(
        window.convert_segy_model(
            {vp_segy, vs_segy, rho_segy, QStringLiteral("converted_ui"),
             {2, 2, 2, 10.0F, 20.0F, 30.0F, 6,
              {4, 4}, {5, 5}, {0, 6}}},
            &error),
        "desktop SEG-Y model conversion failed");
    expect(
        window.current_project()->model_reference ==
                QStringLiteral("models/converted_ui.h5") &&
            QFileInfo::exists(QDir(project_root).filePath(
                QStringLiteral("manifests/models/converted_ui.json"))) &&
            wave3d::io::read_hdf5_model(
                QDir(project_root).filePath(QStringLiteral("models/converted_ui.h5"))
                    .toStdString()).grid.x_boundary.lower_absorbing == 4,
        "desktop did not activate the converted SEG-Y model and provenance");
#endif

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
    expect(
        !require_child<QSpinBox>(window, "sliceXSpin")->isEnabled() &&
            !require_child<QPushButton>(window, "createCropButton")->isEnabled() &&
            require_child<QOpenGLWidget>(window, "volumeViewport")
                ->property("volumeTextureDimensions")
                .toString()
                .isEmpty() &&
            !require_child<QWidget>(window, "experimentEditor")->isEnabled(),
        "switching to an empty project retained crop controls");
    for (const char* name : {"xyViewport", "xzViewport", "yzViewport"}) {
        expect(
            !require_child<QOpenGLWidget>(window, name)
                 ->property("sourceMarkerVisible")
                 .toBool(),
            "switching to an empty project retained a source marker");
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
