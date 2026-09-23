#include "wave3d/desktop/main_window.hpp"
#include "wave3d/desktop/project_navigator.hpp"
#include "wave3d/desktop/theme.hpp"
#include "wave3d/desktop/volume_viewport.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDockWidget>
#include <QListWidget>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPixmap>
#include <QSurfaceFormat>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QTreeView>

#include <array>
#include <exception>
#include <functional>
#include <iostream>
#include <vector>

namespace {

void configure_surface_format() {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);
}

int inspect_shell(wave3d::desktop::MainWindow& window) {
    const auto viewports = window.findChildren<QOpenGLWidget*>();
    const auto* field =
        window.findChild<QComboBox*>(QStringLiteral("displayFieldSelector"));
    std::cout << "viewport_count=" << viewports.size() << '\n'
              << "display_field_count=" << (field == nullptr ? 0 : field->count())
              << '\n'
              << "default_display_field="
              << (field == nullptr ? "" : field->currentData().toString().toStdString())
              << '\n'
              << "navigator_page_count=" << window.navigator_host()->count()
              << '\n'
              << "inspector_page_count="
              << window.context_inspector_host()->count() << '\n'
              << "bottom_tool_page_count=" << window.bottom_tool_area()->count()
              << '\n';
    return viewports.size() == 4 && field != nullptr && field->count() == 6 &&
                   field->findData(QStringLiteral("speed")) >= 0 &&
                   window.navigator_host()->count() == 3 &&
                   window.context_inspector_host()->count() == 2 &&
                   window.bottom_tool_area()->count() == 4
               ? 0
               : 1;
}

bool capture_shell(wave3d::desktop::MainWindow& window, const QString& path) {
    auto image = window.grab();
    QPainter painter(&image);
    for (auto* viewport : window.findChildren<QOpenGLWidget*>()) {
        if (!viewport->isVisible()) {
            continue;
        }
        const auto top_left = viewport->mapTo(&window, QPoint{});
        painter.drawImage(
            QRect(top_left, viewport->size()), viewport->grabFramebuffer());
    }
    painter.end();
    return image.save(path);
}

bool set_spin_values(
    wave3d::desktop::MainWindow& window,
    const QString& text,
    const std::vector<const char*>& names,
    QString* error) {
    const auto values = text.split(QLatin1Char(','));
    if (values.size() != static_cast<qsizetype>(names.size())) {
        *error = QStringLiteral("参数数量不正确");
        return false;
    }
    std::vector<int> parsed;
    parsed.reserve(names.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        bool valid = false;
        const auto value = values[index].toInt(&valid);
        auto* spin = window.findChild<QSpinBox*>(
            QString::fromUtf8(names[static_cast<std::size_t>(index)]));
        if (!valid || spin == nullptr || !spin->isEnabled() ||
            value < spin->minimum() || value > spin->maximum()) {
            *error = QStringLiteral("参数超出当前模型范围");
            return false;
        }
        parsed.push_back(value);
    }
    for (std::size_t index = 0; index < names.size(); ++index) {
        window.findChild<QSpinBox*>(QString::fromUtf8(names[index]))
            ->setValue(parsed[index]);
    }
    return true;
}

bool set_volume_camera(
    wave3d::desktop::MainWindow& window,
    const QString& text,
    QString* error) {
    const auto values = text.split(QLatin1Char(','));
    if (values.size() != 3) {
        *error = QStringLiteral("三维视角需要 yaw,pitch,distance 三个参数");
        return false;
    }
    std::array<float, 3> parsed{};
    for (qsizetype index = 0; index < values.size(); ++index) {
        bool valid = false;
        parsed[static_cast<std::size_t>(index)] = values[index].toFloat(&valid);
        if (!valid) {
            *error = QStringLiteral("三维视角参数不是有效数字");
            return false;
        }
    }
    auto* base =
        window.findChild<QOpenGLWidget*>(QStringLiteral("volumeViewport"));
    auto* volume = dynamic_cast<wave3d::desktop::VolumeViewport*>(base);
    if (volume == nullptr) {
        *error = QStringLiteral("找不到三维体渲染器");
        return false;
    }
    try {
        volume->set_camera(parsed[0], parsed[1], parsed[2]);
    } catch (const std::exception& exception) {
        *error = QString::fromUtf8(exception.what());
        return false;
    }
    return true;
}

bool select_project_node(
    wave3d::desktop::MainWindow& window,
    const QString& key,
    QString* error) {
    auto* navigator = window.project_navigator();
    auto* model = navigator == nullptr ? nullptr : navigator->model();
    if (model == nullptr) {
        *error = QStringLiteral("Project Navigator is unavailable");
        return false;
    }
    std::function<QModelIndex(const QModelIndex&)> find =
        [&](const QModelIndex& parent) -> QModelIndex {
        for (int row = 0; row < model->rowCount(parent); ++row) {
            const auto index = model->index(row, 0, parent);
            const auto selection = model->selectionForIndex(index);
            const bool matches_property =
                selection && selection->kind ==
                                 wave3d::desktop::SelectionKind::ModelProperty &&
                ((key == QStringLiteral("vp") &&
                  selection->model_property ==
                      wave3d::desktop::SelectionModelProperty::Vp) ||
                 (key == QStringLiteral("vs") &&
                  selection->model_property ==
                      wave3d::desktop::SelectionModelProperty::Vs) ||
                 (key == QStringLiteral("density") &&
                  selection->model_property ==
                      wave3d::desktop::SelectionModelProperty::Density));
            const bool matches_source =
                key == QStringLiteral("source") && selection &&
                selection->kind == wave3d::desktop::SelectionKind::Source;
            if (matches_property || matches_source) return index;
            const auto nested = find(index);
            if (nested.isValid()) return nested;
        }
        return {};
    };
    const auto index = find({});
    if (!index.isValid()) {
        *error = QStringLiteral("Project Navigator node is unavailable: %1").arg(key);
        return false;
    }
    navigator->treeView()->setCurrentIndex(index);
    navigator->treeView()->scrollTo(index);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    configure_surface_format();
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Wave3D"));
    QApplication::setOrganizationDomain(QStringLiteral("wave3d.local"));
    wave3d::desktop::apply_scientific_theme(application);
    QApplication::setApplicationName(QStringLiteral("Wave3D Studio"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Wave3D 三维弹性波科研实验工作台"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption inspect_option(
        QStringLiteral("inspect-shell"), QStringLiteral("检查界面结构后退出"));
    const QCommandLineOption smoke_option(
        QStringLiteral("smoke-test"),
        QStringLiteral("创建窗口和OpenGL上下文后自动退出"));
    const QCommandLineOption capture_option(
        QStringLiteral("capture-shell"),
        QStringLiteral("渲染界面并将审查图保存到指定路径"),
        QStringLiteral("path"));
    const QCommandLineOption project_option(
        QStringLiteral("project"),
        QStringLiteral("打开指定 Wave3D 项目目录"),
        QStringLiteral("directory"));
    const QCommandLineOption import_model_option(
        QStringLiteral("import-model"),
        QStringLiteral("向当前项目导入指定 HDF5 模型"),
        QStringLiteral("path"));
    const QCommandLineOption slice_option(
        QStringLiteral("slice-indices"),
        QStringLiteral("设置审查切面 x,y,z"),
        QStringLiteral("x,y,z"));
    const QCommandLineOption crop_option(
        QStringLiteral("crop-bounds"),
        QStringLiteral("设置含端点的审查裁剪范围"),
        QStringLiteral("x0,x1,y0,y1,z0,z1"));
    const QCommandLineOption volume_camera_option(
        QStringLiteral("volume-camera"),
        QStringLiteral("设置三维审查视角 yaw,pitch,distance（弧度、弧度、相对距离）"),
        QStringLiteral("yaw,pitch,distance"));
    const QCommandLineOption module_option(
        QStringLiteral("module"),
        QStringLiteral(
            "打开审查模块：project、model、workspace、source、acquisition、"
            "results 或 display"),
        QStringLiteral("name"));
    const QCommandLineOption hide_navigator_option(
        QStringLiteral("hide-navigator"),
        QStringLiteral("审查截图中隐藏 Navigator"));
    const QCommandLineOption hide_inspector_option(
        QStringLiteral("hide-inspector"),
        QStringLiteral("审查截图中隐藏 Context Inspector"));
    const QCommandLineOption collapse_bottom_option(
        QStringLiteral("collapse-bottom"),
        QStringLiteral("审查截图中折叠 Bottom Tool Area"));
    const QCommandLineOption auto_run_option(
        QStringLiteral("auto-run"),
        QStringLiteral("保存、预检并启动指定编号的审查运行"),
        QStringLiteral("run-id"));
    const QCommandLineOption project_selection_option(
        QStringLiteral("project-selection"),
        QStringLiteral("选择审查节点：vp、vs、density 或 source"),
        QStringLiteral("node"));
    const QCommandLineOption navigator_width_option(
        QStringLiteral("navigator-width"),
        QStringLiteral("设置审查截图中的 Navigator 宽度"),
        QStringLiteral("pixels"));
    parser.addOption(inspect_option);
    parser.addOption(smoke_option);
    parser.addOption(capture_option);
    parser.addOption(project_option);
    parser.addOption(import_model_option);
    parser.addOption(slice_option);
    parser.addOption(crop_option);
    parser.addOption(volume_camera_option);
    parser.addOption(module_option);
    parser.addOption(hide_navigator_option);
    parser.addOption(hide_inspector_option);
    parser.addOption(collapse_bottom_option);
    parser.addOption(auto_run_option);
    parser.addOption(project_selection_option);
    parser.addOption(navigator_width_option);
    parser.process(application);

    wave3d::desktop::MainWindow window(
        nullptr, !parser.isSet(project_option));
    QString error;
    if (parser.isSet(project_option) &&
        !window.open_project(parser.value(project_option), &error)) {
        std::cerr << "Cannot open project: " << error.toStdString() << '\n';
        return 2;
    }
    if (parser.isSet(import_model_option) &&
        !window.import_hdf5_model(parser.value(import_model_option), &error)) {
        std::cerr << "Cannot import model: " << error.toStdString() << '\n';
        return 2;
    }
    if (parser.isSet(slice_option) &&
        !set_spin_values(
            window,
            parser.value(slice_option),
            {"sliceXSpin", "sliceYSpin", "sliceZSpin"},
            &error)) {
        std::cerr << "Cannot set slice indices: " << error.toStdString() << '\n';
        return 2;
    }
    if (parser.isSet(crop_option) &&
        !set_spin_values(
            window,
            parser.value(crop_option),
            {"cropXBeginSpin", "cropXEndSpin", "cropYBeginSpin",
             "cropYEndSpin", "cropZBeginSpin", "cropZEndSpin"},
            &error)) {
        std::cerr << "Cannot set crop bounds: " << error.toStdString() << '\n';
        return 2;
    }
    if (parser.isSet(volume_camera_option) &&
        !set_volume_camera(
            window, parser.value(volume_camera_option), &error)) {
        std::cerr << "Cannot set volume camera: " << error.toStdString() << '\n';
        return 2;
    }
    if (parser.isSet(module_option)) {
        const auto module = parser.value(module_option);
        int row = -1;
        if (module == QStringLiteral("project")) {
            row = 0;
        } else if (module == QStringLiteral("model")) {
            row = 1;
        } else if (module == QStringLiteral("workspace")) {
            row = 2;
        } else if (module == QStringLiteral("source")) {
            row = 3;
        } else if (module == QStringLiteral("acquisition")) {
            row = 4;
        } else if (module == QStringLiteral("results")) {
            row = 6;
        } else if (module == QStringLiteral("display")) {
            row = 7;
        }
        auto* navigation = window.findChild<QListWidget*>(
            QStringLiteral("moduleNavigation"));
        if (row < 0 || navigation == nullptr) {
            std::cerr << "Cannot select review module\n";
            return 2;
        }
        navigation->setCurrentRow(row);
    }
    if (parser.isSet(project_selection_option) &&
        !select_project_node(
            window, parser.value(project_selection_option), &error)) {
        std::cerr << "Cannot select project node: " << error.toStdString() << '\n';
        return 2;
    }
    if (parser.isSet(navigator_width_option)) {
        bool valid = false;
        const auto width = parser.value(navigator_width_option).toInt(&valid);
        if (!valid || width < 180 || width > 700) {
            std::cerr << "Navigator width must be between 180 and 700\n";
            return 2;
        }
        window.resizeDocks(
            {window.navigator_dock()}, {width}, Qt::Horizontal);
    }
    if (parser.isSet(hide_navigator_option)) {
        window.navigator_dock()->hide();
    }
    if (parser.isSet(hide_inspector_option)) {
        window.context_inspector_dock()->hide();
    }
    if (parser.isSet(collapse_bottom_option)) {
        window.bottom_tool_dock()->hide();
    }
    if (parser.isSet(auto_run_option)) {
        if (!window.save_experiment_draft(&error) ||
            !window.preflight_experiment(parser.value(auto_run_option), &error) ||
            !window.start_prepared_run(&error)) {
            std::cerr << "Cannot start review run: " << error.toStdString()
                      << '\n';
            return 2;
        }
    }
    if (parser.isSet(inspect_option)) {
        return inspect_shell(window);
    }

    window.show();
    if (parser.isSet(capture_option)) {
        const auto output_path = parser.value(capture_option);
        const auto capture_delay_ms =
            parser.isSet(auto_run_option) ? 8000 : 1500;
        QTimer::singleShot(capture_delay_ms, &application, [&application, &window, output_path] {
            application.exit(capture_shell(window, output_path) ? 0 : 2);
        });
    } else if (parser.isSet(smoke_option)) {
        QTimer::singleShot(1500, &application, &QApplication::quit);
    }
    const auto result = application.exec();
    if (result != 0 ||
        (!parser.isSet(smoke_option) && !parser.isSet(capture_option))) {
        return result;
    }
    for (const auto* viewport : window.findChildren<QOpenGLWidget*>()) {
        if (viewport->isVisible() &&
            !viewport->property("openGlReady").toBool()) {
            std::cerr << "OpenGL context was not created for "
                      << viewport->objectName().toStdString() << '\n';
            return 1;
        }
    }
    const auto* volume =
        window.findChild<QOpenGLWidget*>(QStringLiteral("volumeViewport"));
    if (volume != nullptr && volume->isVisible() &&
        window.current_project() != nullptr &&
        !window.current_project()->model_reference.isEmpty() &&
        (!volume->property("volumeShaderReady").toBool() ||
         !volume->property("volumeTextureReady").toBool() ||
         !volume->property("volumeFrameReady").toBool())) {
        std::cerr << "Static volume renderer did not produce a valid frame\n";
        return 1;
    }
    if (volume != nullptr && volume->isVisible() &&
        volume->property("sourceMarkerPosition").toList().size() == 3 &&
        !volume->property("sourceMarkerVisible").toBool()) {
        std::cerr << "Static source marker was not rendered\n";
        return 1;
    }
    if (volume != nullptr && volume->isVisible() &&
        volume->property("receiverCount").toULongLong() > 0 &&
        !volume->property("receiverMarkerVisible").toBool()) {
        std::cerr << "Static receiver markers were not rendered\n";
        return 1;
    }
    return 0;
}
