#include "wave3d/desktop/main_window.hpp"
#include "wave3d/desktop/theme.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPixmap>
#include <QSurfaceFormat>
#include <QTimer>

#include <iostream>

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
              << '\n';
    return viewports.size() == 4 && field != nullptr && field->count() == 6 &&
                   field->findData(QStringLiteral("speed")) >= 0
               ? 0
               : 1;
}

bool capture_shell(wave3d::desktop::MainWindow& window, const QString& path) {
    auto image = window.grab();
    QPainter painter(&image);
    for (auto* viewport : window.findChildren<QOpenGLWidget*>()) {
        const auto top_left = viewport->mapTo(&window, QPoint{});
        painter.drawImage(
            QRect(top_left, viewport->size()), viewport->grabFramebuffer());
    }
    painter.end();
    return image.save(path);
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
    parser.addOption(inspect_option);
    parser.addOption(smoke_option);
    parser.addOption(capture_option);
    parser.addOption(project_option);
    parser.addOption(import_model_option);
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
    if (parser.isSet(inspect_option)) {
        return inspect_shell(window);
    }

    window.show();
    if (parser.isSet(capture_option)) {
        const auto output_path = parser.value(capture_option);
        QTimer::singleShot(1500, &application, [&application, &window, output_path] {
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
        if (!viewport->property("openGlReady").toBool()) {
            std::cerr << "OpenGL context was not created for "
                      << viewport->objectName().toStdString() << '\n';
            return 1;
        }
    }
    return 0;
}
