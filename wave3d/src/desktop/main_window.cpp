#include "wave3d/desktop/main_window.hpp"
#include "wave3d/desktop/experiment_editor.hpp"
#include "wave3d/desktop/model_derivation.hpp"
#include "wave3d/desktop/result_workspace.hpp"
#include "wave3d/desktop/static_model_scene.hpp"
#include "wave3d/desktop/volume_viewport.hpp"

#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
#include "wave3d/desktop/forward_run_worker.hpp"
#include "wave3d/desktop/live_wavefield_view.hpp"
#endif

#ifdef WAVE3D_DESKTOP_HAS_YAML
#include "wave3d/io/yaml_config.hpp"
#endif
#ifdef WAVE3D_DESKTOP_HAS_SEGY
#include "wave3d/io/segy.hpp"
#endif

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QFrame>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QGridLayout>
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
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace wave3d::desktop {
namespace {

#if defined(WAVE3D_DESKTOP_HAS_HDF5) && defined(WAVE3D_DESKTOP_HAS_SEGY)
std::optional<SegyModelConversionRequest> prompt_segy_model_conversion(
    QWidget* parent,
    const QString& project_root) {
    const auto select = [&](const QString& title) {
        return QFileDialog::getOpenFileName(
            parent, title, project_root,
            QStringLiteral("SEG-Y (*.sgy *.segy);;所有文件 (*)"));
    };
    const auto vp = select(QStringLiteral("选择 Vp SEG-Y 体（m/s）"));
    if (vp.isEmpty()) {
        return std::nullopt;
    }
    const auto vs = select(QStringLiteral("选择 Vs SEG-Y 体（m/s）"));
    if (vs.isEmpty()) {
        return std::nullopt;
    }
    const auto density = select(QStringLiteral("选择密度 SEG-Y 体（kg/m³）"));
    if (density.isEmpty()) {
        return std::nullopt;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("SEG-Y 规则属性体转换"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* note = new QLabel(
        QStringLiteral("每个文件须含 NX×NY 道，每道 NZ 个 IEEE float 样点；"
                       "道序为 X 快、Y 慢，样点沿 Z 向下。"),
        &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* form = new QFormLayout;
    auto* stem = new QLineEdit(QStringLiteral("segy_model_001"), &dialog);
    stem->setObjectName(QStringLiteral("segyModelOutputStemEdit"));
    form->addRow(QStringLiteral("输出名称"), stem);
    const auto add_size = [&](const QString& label, int value, int minimum,
                              int maximum, const char* name) {
        auto* spin = new QSpinBox(&dialog);
        spin->setObjectName(QString::fromUtf8(name));
        spin->setRange(minimum, maximum);
        spin->setValue(value);
        form->addRow(label, spin);
        return spin;
    };
    const auto add_spacing = [&](const QString& label, const char* name) {
        auto* spin = new QDoubleSpinBox(&dialog);
        spin->setObjectName(QString::fromUtf8(name));
        spin->setRange(0.001, 1000000.0);
        spin->setDecimals(3);
        spin->setValue(10.0);
        spin->setSuffix(QStringLiteral(" m"));
        form->addRow(label, spin);
        return spin;
    };
    auto* nx = add_size(
        QStringLiteral("NX"), 100, 1, 65535, "segyModelNxSpin");
    auto* ny = add_size(
        QStringLiteral("NY"), 100, 1, 65535, "segyModelNySpin");
    auto* nz = add_size(
        QStringLiteral("NZ / 每道样点"),
        100, 1, 65535, "segyModelNzSpin");
    auto* dx = add_spacing(QStringLiteral("DX"), "segyModelDxSpin");
    auto* dy = add_spacing(QStringLiteral("DY"), "segyModelDySpin");
    auto* dz = add_spacing(QStringLiteral("DZ"), "segyModelDzSpin");
    auto* halo = add_size(
        QStringLiteral("交错网格 halo"),
        6, 1, 64, "segyModelHaloSpin");
    std::array<QSpinBox*, 6> boundary{};
    const std::array<QString, 6> labels{
        QStringLiteral("X− 吸收层"), QStringLiteral("X+ 吸收层"),
        QStringLiteral("Y− 吸收层"), QStringLiteral("Y+ 吸收层"),
        QStringLiteral("Z− 吸收层"), QStringLiteral("Z+ 吸收层")};
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const auto name =
            "segyModelBoundary" + std::to_string(index) + "Spin";
        boundary[index] = add_size(
            labels[index], index == 4 ? 0 : 20, 0, 10000, name.c_str());
    }
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(
        buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(
        buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return SegyModelConversionRequest{
        vp, vs, density, stem->text(),
        {static_cast<std::size_t>(nx->value()),
         static_cast<std::size_t>(ny->value()),
         static_cast<std::size_t>(nz->value()),
         static_cast<float>(dx->value()), static_cast<float>(dy->value()),
         static_cast<float>(dz->value()), static_cast<std::size_t>(halo->value()),
         {static_cast<std::size_t>(boundary[0]->value()),
          static_cast<std::size_t>(boundary[1]->value())},
         {static_cast<std::size_t>(boundary[2]->value()),
          static_cast<std::size_t>(boundary[3]->value())},
         {static_cast<std::size_t>(boundary[4]->value()),
          static_cast<std::size_t>(boundary[5]->value())}}};
}
#endif

class ScientificViewport final : public QOpenGLWidget,
                                 protected QOpenGLFunctions {
public:
    ScientificViewport(QString title, QString object_name, QWidget* parent)
        : QOpenGLWidget(parent), title_(std::move(title)) {
        setObjectName(std::move(object_name));
        setMinimumSize(180, 110);
        setProperty("openGlReady", false);
        setProperty("hasScientificImage", false);
    }

    void set_scientific_image(QImage image, QString message = {}) {
        image_ = std::move(image);
        message_ = std::move(message);
        setProperty("hasScientificImage", !image_.isNull());
        setProperty("scientificImageSize", image_.size());
        setProperty(
            "scientificImageCacheKey",
            QVariant::fromValue<qulonglong>(image_.cacheKey()));
        update();
    }

    void clear_scientific_image(QString message) {
        image_ = {};
        message_ = std::move(message);
        setProperty("hasScientificImage", false);
        setProperty("scientificImageSize", QSize());
        setProperty("scientificImageCacheKey", QVariant::fromValue<qulonglong>(0));
        update();
    }

    [[nodiscard]] QImage scientific_image() const { return image_; }

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
        if (image_.isNull()) {
            painter.setPen(QPen(QColor(38, 61, 76, 120), 1.0));
            constexpr int grid_spacing = 42;
            for (int x = grid_spacing; x < width(); x += grid_spacing) {
                painter.drawLine(x, 0, x, height());
            }
            for (int y = grid_spacing; y < height(); y += grid_spacing) {
                painter.drawLine(0, y, width(), y);
            }
        } else {
            const auto available = rect().adjusted(8, 8, -8, -8);
            const auto scaled = image_.size().scaled(
                available.size(), Qt::KeepAspectRatio);
            const QRect target(
                available.center() - QPoint(scaled.width() / 2, scaled.height() / 2),
                scaled);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(target, image_);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(18, 46, 62, 225));
        painter.drawRoundedRect(QRect(14, 14, 112, 30), 8, 8);
        painter.setPen(QColor(171, 220, 239));
        painter.drawText(QRect(14, 14, 112, 30), Qt::AlignCenter, title_);

        const auto message = message_.isEmpty()
                                 ? QStringLiteral("等待科学数据")
                                 : message_;
        const auto message_rect =
            image_.isNull()
                ? rect().adjusted(0, 32, 0, 0)
                : QRect(14, height() - 46, width() - 28, 30);
        if (!image_.isNull()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(8, 23, 34, 205));
            painter.drawRoundedRect(message_rect, 8, 8);
        }
        painter.setPen(QColor(171, 220, 239));
        painter.drawText(message_rect, Qt::AlignCenter, message);

        painter.setPen(QPen(QColor(45, 75, 92), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 8, 8);
    }

private:
    QString title_;
    QImage image_;
    QString message_;
};

ScientificViewport* scientific_viewport(
    QMainWindow* window,
    const QString& object_name) {
    auto* viewport = dynamic_cast<ScientificViewport*>(
        window->findChild<QOpenGLWidget*>(object_name));
    if (viewport == nullptr) {
        throw std::logic_error("scientific viewport is missing");
    }
    return viewport;
}

VolumeViewport* volume_viewport(QMainWindow* window) {
    auto* viewport = dynamic_cast<VolumeViewport*>(
        window->findChild<QOpenGLWidget*>(QStringLiteral("volumeViewport")));
    if (viewport == nullptr) {
        throw std::logic_error("volume viewport is missing");
    }
    return viewport;
}

ExperimentEditor* experiment_editor(QMainWindow* window) {
    auto* editor = dynamic_cast<ExperimentEditor*>(
        window->findChild<QWidget*>(QStringLiteral("experimentEditor")));
    if (editor == nullptr) {
        throw std::logic_error("experiment editor is missing");
    }
    return editor;
}

ResultWorkspace* result_workspace(QMainWindow* window) {
    auto* workspace = dynamic_cast<ResultWorkspace*>(
        window->findChild<QWidget*>(QStringLiteral("resultWorkspace")));
    if (workspace == nullptr) {
        throw std::logic_error("result workspace is missing");
    }
    return workspace;
}

#ifdef WAVE3D_DESKTOP_HAS_HDF5
void copy_file_atomically(
    const QString& source_path,
    const QString& destination) {
    QFile input(source_path);
    if (!input.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("cannot open source model for copying");
    }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        throw std::runtime_error("cannot open project model copy");
    }
    constexpr qint64 block_size = 1024 * 1024;
    while (!input.atEnd()) {
        const auto block = input.read(block_size);
        if (block.isEmpty() || output.write(block) != block.size()) {
            throw std::runtime_error("cannot write complete project model copy");
        }
    }
    if (!output.commit()) {
        throw std::runtime_error("cannot atomically publish project model copy");
    }
}

ModelCropBounds selected_crop_bounds(QMainWindow* window) {
    const auto value = [window](const char* name) {
        return static_cast<std::size_t>(
            window->findChild<QSpinBox*>(QString::fromUtf8(name))->value());
    };
    return {
        value("cropXBeginSpin"), value("cropXEndSpin") + 1,
        value("cropYBeginSpin"), value("cropYEndSpin") + 1,
        value("cropZBeginSpin"), value("cropZEndSpin") + 1};
}

QImage annotate_section(
    QImage image,
    int orientation,
    std::size_t x,
    std::size_t y,
    std::size_t z,
    const ModelCropBounds& crop,
    const std::optional<std::array<double, 3>>& source_index,
    const std::vector<std::array<double, 3>>& receiver_indices) {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    const auto ix = [](std::size_t value) { return static_cast<int>(value); };
    painter.setPen(QPen(QColor(255, 142, 64), 1));
    if (orientation == 0) {
        painter.drawRect(
            ix(crop.x_begin),
            image.height() - ix(crop.y_end),
            ix(crop.x_end - crop.x_begin) - 1,
            ix(crop.y_end - crop.y_begin) - 1);
    } else if (orientation == 1) {
        painter.drawRect(
            ix(crop.x_begin),
            ix(crop.z_begin),
            ix(crop.x_end - crop.x_begin) - 1,
            ix(crop.z_end - crop.z_begin) - 1);
    } else {
        painter.drawRect(
            ix(crop.y_begin),
            ix(crop.z_begin),
            ix(crop.y_end - crop.y_begin) - 1,
            ix(crop.z_end - crop.z_begin) - 1);
    }
    painter.setPen(QPen(QColor(198, 244, 255), 1));
    if (orientation == 0) {
        painter.drawLine(ix(x), 0, ix(x), image.height() - 1);
        const auto row = image.height() - 1 - ix(y);
        painter.drawLine(0, row, image.width() - 1, row);
    } else if (orientation == 1) {
        painter.drawLine(ix(x), 0, ix(x), image.height() - 1);
        painter.drawLine(0, ix(z), image.width() - 1, ix(z));
    } else {
        painter.drawLine(ix(y), 0, ix(y), image.height() - 1);
        painter.drawLine(0, ix(z), image.width() - 1, ix(z));
    }
    if (source_index) {
        const auto plane_distance = orientation == 0
                                        ? std::abs((*source_index)[2] - z)
                                        : orientation == 1
                                              ? std::abs((*source_index)[1] - y)
                                              : std::abs((*source_index)[0] - x);
        const bool on_plane = plane_distance <= 0.5;
        QPointF point;
        if (orientation == 0) {
            point = {
                (*source_index)[0],
                image.height() - 1.0 - (*source_index)[1]};
        } else if (orientation == 1) {
            point = {(*source_index)[0], (*source_index)[2]};
        } else {
            point = {(*source_index)[1], (*source_index)[2]};
        }
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(
            on_plane ? QColor(255, 255, 255) : QColor(255, 124, 141),
            1.5,
            on_plane ? Qt::SolidLine : Qt::DashLine));
        painter.setBrush(
            on_plane ? QBrush(QColor(255, 82, 104)) : Qt::NoBrush);
        painter.drawEllipse(point, 3.5, 3.5);
        painter.drawLine(point + QPointF(-5.5, 0.0), point + QPointF(5.5, 0.0));
        painter.drawLine(point + QPointF(0.0, -5.5), point + QPointF(0.0, 5.5));
    }
    constexpr std::size_t maximum_section_markers = 1600;
    const auto stride = receiver_indices.size() <= maximum_section_markers
                            ? std::size_t{1}
                            : (receiver_indices.size() +
                               maximum_section_markers - 1) /
                                  maximum_section_markers;
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(192, 250, 255, 220), 1.0));
    painter.setBrush(QColor(39, 205, 221, 205));
    for (std::size_t index = 0; index < receiver_indices.size(); index += stride) {
        const auto& receiver = receiver_indices[index];
        const auto plane_distance = orientation == 0
                                        ? std::abs(receiver[2] - z)
                                        : orientation == 1
                                              ? std::abs(receiver[1] - y)
                                              : std::abs(receiver[0] - x);
        if (orientation != 0 && plane_distance > 0.5) {
            continue;
        }
        QPointF point;
        if (orientation == 0) {
            point = {receiver[0], image.height() - 1.0 - receiver[1]};
        } else if (orientation == 1) {
            point = {receiver[0], receiver[2]};
        } else {
            point = {receiver[1], receiver[2]};
        }
        painter.drawEllipse(point, 1.15, 1.15);
    }
    return image;
}
#endif

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

    auto* frame = new QLabel(QStringLiteral("波场帧：等待运行"), group);
    frame->setObjectName(QStringLiteral("liveFrameLabel"));
    frame->setWordWrap(true);
    layout->addWidget(frame);

    auto* display_form = new QFormLayout;
    auto* interval = new QSpinBox(group);
    interval->setObjectName(QStringLiteral("displayIntervalSpin"));
    interval->setRange(1, 100);
    interval->setValue(15);
    interval->setSuffix(QStringLiteral(" 步"));
    interval->setToolTip(QStringLiteral(
        "每隔多少个完整时间步更新一次四视图；当前 Overthrust 基准建议 15 步"));
    display_form->addRow(QStringLiteral("显示间隔："), interval);
    auto* live_opacity = new QSlider(Qt::Horizontal, group);
    live_opacity->setObjectName(QStringLiteral("liveOpacitySlider"));
    live_opacity->setRange(1, 100);
    live_opacity->setValue(88);
    display_form->addRow(QStringLiteral("波场不透明度："), live_opacity);
    auto* live_threshold = new QSlider(Qt::Horizontal, group);
    live_threshold->setObjectName(QStringLiteral("liveThresholdSlider"));
    live_threshold->setRange(0, 95);
    live_threshold->setValue(4);
    display_form->addRow(QStringLiteral("波场阈值："), live_threshold);
    layout->addLayout(display_form);

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

    auto* scroll = new QScrollArea(dock);
    scroll->setObjectName(QStringLiteral("experimentWorkspaceScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(contents);
    dock->setWidget(scroll);
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

QDockWidget* make_model_information_dock(QMainWindow* window) {
    auto* dock = new QDockWidget(QStringLiteral("模型信息"), window);
    dock->setObjectName(QStringLiteral("modelInformationDock"));
    dock->setMinimumWidth(250);
    auto* contents = new QWidget(dock);
    auto* layout = new QVBoxLayout(contents);
    auto* form = new QFormLayout;

    auto* property = new QComboBox(contents);
    property->setObjectName(QStringLiteral("modelPropertySelector"));
    property->addItem(QStringLiteral("Vp"), QStringLiteral("vp"));
    property->addItem(QStringLiteral("Vs"), QStringLiteral("vs"));
    property->addItem(QStringLiteral("密度"), QStringLiteral("density"));
    property->setEnabled(false);
    form->addRow(QStringLiteral("显示属性："), property);

    for (const auto& specification : std::array{
             std::pair{QStringLiteral("网格："), QStringLiteral("modelGridLabel")},
             std::pair{QStringLiteral("间距："), QStringLiteral("modelSpacingLabel")},
             std::pair{QStringLiteral("范围："), QStringLiteral("modelExtentLabel")},
             std::pair{QStringLiteral("Vp："), QStringLiteral("modelVpRangeLabel")},
             std::pair{QStringLiteral("Vs："), QStringLiteral("modelVsRangeLabel")},
             std::pair{QStringLiteral("密度："), QStringLiteral("modelDensityRangeLabel")},
             std::pair{QStringLiteral("中心索引："), QStringLiteral("modelCenterLabel")}}) {
        auto* value = new QLabel(QStringLiteral("—"), contents);
        value->setObjectName(specification.second);
        form->addRow(specification.first, value);
    }
    layout->addLayout(form);

    auto* slices = new QGroupBox(QStringLiteral("联动切面索引"), contents);
    auto* slice_layout = new QGridLayout(slices);
    int column = 0;
    for (const auto& specification : std::array{
             std::pair{QStringLiteral("X"), QStringLiteral("sliceXSpin")},
             std::pair{QStringLiteral("Y"), QStringLiteral("sliceYSpin")},
             std::pair{QStringLiteral("Z"), QStringLiteral("sliceZSpin")}}) {
        slice_layout->addWidget(new QLabel(specification.first, slices), 0, column);
        auto* spin = new QSpinBox(slices);
        spin->setObjectName(specification.second);
        spin->setEnabled(false);
        slice_layout->addWidget(spin, 1, column++);
    }
    layout->addWidget(slices);

    auto* crop = new QGroupBox(QStringLiteral("裁剪范围（含端点）"), contents);
    auto* crop_layout = new QGridLayout(crop);
    crop_layout->addWidget(new QLabel(QStringLiteral("轴"), crop), 0, 0);
    crop_layout->addWidget(new QLabel(QStringLiteral("起点"), crop), 0, 1);
    crop_layout->addWidget(new QLabel(QStringLiteral("终点"), crop), 0, 2);
    const std::array<const char*, 6> crop_names{
        "cropXBeginSpin", "cropXEndSpin", "cropYBeginSpin",
        "cropYEndSpin", "cropZBeginSpin", "cropZEndSpin"};
    for (int axis = 0; axis < 3; ++axis) {
        crop_layout->addWidget(
            new QLabel(QString(QChar('X' + axis)), crop), axis + 1, 0);
        for (int endpoint = 0; endpoint < 2; ++endpoint) {
            auto* spin = new QSpinBox(crop);
            spin->setObjectName(QString::fromUtf8(
                crop_names[static_cast<std::size_t>(axis * 2 + endpoint)]));
            spin->setEnabled(false);
            crop_layout->addWidget(spin, axis + 1, endpoint + 1);
        }
    }
    auto* summary = new QLabel(QStringLiteral("—"), crop);
    summary->setObjectName(QStringLiteral("cropSummaryLabel"));
    summary->setWordWrap(true);
    crop_layout->addWidget(summary, 4, 0, 1, 3);
    auto* create = new QPushButton(QStringLiteral("创建裁剪模型"), crop);
    create->setObjectName(QStringLiteral("createCropButton"));
    create->setEnabled(false);
    crop_layout->addWidget(create, 5, 0, 1, 3);
    layout->addWidget(crop);

    auto* rendering = new QGroupBox(QStringLiteral("三维传递函数"), contents);
    auto* rendering_form = new QFormLayout(rendering);
    auto* opacity = new QSlider(Qt::Horizontal, rendering);
    opacity->setObjectName(QStringLiteral("volumeOpacitySlider"));
    opacity->setRange(1, 100);
    opacity->setValue(55);
    opacity->setEnabled(false);
    rendering_form->addRow(QStringLiteral("不透明度："), opacity);
    auto* threshold = new QSlider(Qt::Horizontal, rendering);
    threshold->setObjectName(QStringLiteral("volumeThresholdSlider"));
    threshold->setRange(0, 95);
    threshold->setValue(8);
    threshold->setEnabled(false);
    rendering_form->addRow(QStringLiteral("低值阈值："), threshold);
    auto* reset_camera = new QPushButton(QStringLiteral("重置三维视角"), rendering);
    reset_camera->setObjectName(QStringLiteral("resetVolumeCameraButton"));
    reset_camera->setEnabled(false);
    rendering_form->addRow(reset_camera);
    layout->addWidget(rendering);
    layout->addStretch();
    auto* scroll = new QScrollArea(dock);
    scroll->setObjectName(QStringLiteral("modelInformationScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(contents);
    dock->setWidget(scroll);
    return dock;
}

QDockWidget* make_experiment_editor_dock(QMainWindow* window) {
    auto* dock = new QDockWidget(QStringLiteral("实验设置"), window);
    dock->setObjectName(QStringLiteral("experimentEditorDock"));
    dock->setMinimumWidth(340);
    auto* scroll = new QScrollArea(dock);
    scroll->setObjectName(QStringLiteral("experimentEditorScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(new ExperimentEditor(scroll));
    dock->setWidget(scroll);
    return dock;
}

QDockWidget* make_results_dock(QMainWindow* window) {
    auto* dock = new QDockWidget(QStringLiteral("结果工作区"), window);
    dock->setObjectName(QStringLiteral("resultsDock"));
    dock->setMinimumWidth(620);
    dock->setWidget(new ResultWorkspace(dock));
    return dock;
}

} // namespace

MainWindow::MainWindow(QWidget* parent, bool restore_last_project)
    : QMainWindow(parent) {
    setObjectName(QStringLiteral("wave3dMainWindow"));
    setWindowTitle(QStringLiteral("Wave3D 科研实验工作台"));
    resize(1440, 900);
    setDockOptions(
        QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);

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
    horizontal->setHandleWidth(7);
    horizontal->setOpaqueResize(true);
    horizontal->addWidget(new VolumeViewport(horizontal));

    auto* slices = new QSplitter(Qt::Vertical, horizontal);
    slices->setObjectName(QStringLiteral("sliceViewSplitter"));
    slices->setHandleWidth(7);
    slices->setOpaqueResize(true);
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
    auto* model_information_dock = make_model_information_dock(this);
    auto* experiment_editor_dock = make_experiment_editor_dock(this);
    auto* results_dock = make_results_dock(this);
    addDockWidget(Qt::LeftDockWidgetArea, experiment_dock);
    addDockWidget(Qt::BottomDockWidgetArea, log_dock);
    addDockWidget(Qt::RightDockWidgetArea, model_information_dock);
    addDockWidget(Qt::RightDockWidgetArea, experiment_editor_dock);
    addDockWidget(Qt::BottomDockWidgetArea, results_dock);
    tabifyDockWidget(model_information_dock, experiment_editor_dock);
    model_information_dock->raise();
    tabifyDockWidget(log_dock, results_dock);
    log_dock->raise();
    results_dock->hide();
    resizeDocks({log_dock}, {145}, Qt::Vertical);
    resizeDocks({model_information_dock}, {270}, Qt::Horizontal);
#ifndef WAVE3D_DESKTOP_HAS_SEGY
    result_workspace(this)->set_segy_available(false);
#endif

    auto* file_menu = menuBar()->addMenu(QStringLiteral("文件"));
    auto* new_project = file_menu->addAction(QStringLiteral("新建项目"));
    new_project->setObjectName(QStringLiteral("newProjectAction"));
    auto* open_project = file_menu->addAction(QStringLiteral("打开项目"));
    open_project->setObjectName(QStringLiteral("openProjectAction"));

    auto* model_menu = menuBar()->addMenu(QStringLiteral("模型"));
    auto* import_model =
        model_menu->addAction(QStringLiteral("导入 Wave3D HDF5 模型"));
    import_model->setObjectName(QStringLiteral("importHdf5ModelAction"));
    import_model->setEnabled(false);
#ifndef WAVE3D_DESKTOP_HAS_HDF5
    import_model->setToolTip(QStringLiteral("当前构建未启用 HDF5"));
#endif
    auto* convert_segy =
        model_menu->addAction(QStringLiteral("转换 SEG-Y 属性模型…"));
    convert_segy->setObjectName(QStringLiteral("convertSegyModelAction"));
    convert_segy->setEnabled(false);
#if !defined(WAVE3D_DESKTOP_HAS_HDF5) || !defined(WAVE3D_DESKTOP_HAS_SEGY)
    convert_segy->setToolTip(QStringLiteral("当前构建需要同时启用 HDF5 与 SEG-Y"));
#endif

    auto* run_menu = menuBar()->addMenu(QStringLiteral("运行"));
    auto* snapshot = run_menu->addAction(QStringLiteral("保存波场快照（预留）"));
    snapshot->setObjectName(QStringLiteral("snapshotAction"));
    snapshot->setEnabled(false);
    snapshot->setToolTip(
        QStringLiteral("预留接口：当前版本不写入波场快照"));

    auto* toolbar = addToolBar(QStringLiteral("主工具栏"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);
    toolbar->addAction(new_project);
    toolbar->addAction(open_project);
    toolbar->addAction(import_model);
    toolbar->addSeparator();
    auto* validate = toolbar->addAction(QStringLiteral("实验预检"));
    validate->setObjectName(QStringLiteral("validateExperimentAction"));
    validate->setEnabled(false);
    auto* run = toolbar->addAction(QStringLiteral("开始正演"));
    run->setObjectName(QStringLiteral("startRunAction"));
    run->setEnabled(false);

    run_poll_timer_ = new QTimer(this);
    run_poll_timer_->setInterval(100);
    connect(run_poll_timer_, &QTimer::timeout, this, [this] {
        poll_forward_run();
    });

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
    connect(import_model, &QAction::triggered, this, [this] {
        const auto path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("导入 Wave3D HDF5 模型"),
            project_root_.isEmpty()
                ? QString()
                : QDir(project_root_).filePath(QStringLiteral("models")),
            QStringLiteral("Wave3D HDF5 模型 (*.h5 *.hdf5)"));
        if (path.isEmpty()) {
            return;
        }
        QString error;
        if (!import_hdf5_model(path, &error)) {
            QMessageBox::critical(this, QStringLiteral("无法导入模型"), error);
        }
    });
    connect(convert_segy, &QAction::triggered, this, [this] {
#if defined(WAVE3D_DESKTOP_HAS_HDF5) && defined(WAVE3D_DESKTOP_HAS_SEGY)
        const auto request = prompt_segy_model_conversion(this, project_root_);
        if (!request) return;
        QString error;
        if (!convert_segy_model(*request, &error)) {
            QMessageBox::critical(this, QStringLiteral("无法转换 SEG-Y 模型"), error);
        }
#endif
    });
    connect(validate, &QAction::triggered, this, [this] {
        QString error;
        if (!save_experiment_draft(&error)) {
            QMessageBox::critical(this, QStringLiteral("无法保存实验草稿"), error);
            return;
        }
        auto run_id = QStringLiteral("run-%1").arg(
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        int suffix = 1;
        while (QFileInfo::exists(
            QDir(project_root_).filePath(QStringLiteral("runs/") + run_id))) {
            run_id = QStringLiteral("run-%1-%2")
                         .arg(
                             QDateTime::currentDateTimeUtc().toString(
                                 QStringLiteral("yyyyMMdd-HHmmss")))
                         .arg(suffix++);
        }
        if (!preflight_experiment(run_id, &error)) {
            QMessageBox::critical(this, QStringLiteral("实验预检失败"), error);
        }
    });
    connect(run, &QAction::triggered, this, [this] {
        QString error;
        if (!start_prepared_run(&error)) {
            QMessageBox::critical(this, QStringLiteral("无法开始正演"), error);
        }
    });
    connect(
        findChild<QPushButton*>(QStringLiteral("startRunButton")),
        &QPushButton::clicked,
        this,
        [this] {
            QString error;
            if (!start_prepared_run(&error)) {
                QMessageBox::critical(this, QStringLiteral("无法开始正演"), error);
            }
        });
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
    connect(
        findChild<QPushButton*>(QStringLiteral("pauseRunButton")),
        &QPushButton::clicked,
        this,
        [this] {
            if (forward_worker_) {
                forward_worker_->request_pause();
            }
        });
    connect(
        findChild<QPushButton*>(QStringLiteral("resumeRunButton")),
        &QPushButton::clicked,
        this,
        [this] {
            if (forward_worker_) {
                forward_worker_->request_resume();
            }
        });
    connect(
        findChild<QPushButton*>(QStringLiteral("stopRunButton")),
        &QPushButton::clicked,
        this,
        [this] {
            if (forward_worker_) {
                forward_worker_->request_stop();
            }
        });
#endif

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
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
            if (forward_worker_ && forward_worker_->isRunning()) {
                forward_worker_->request_visualization_field(
                    visualization_field_from_key(project_->display_field));
            }
#endif
        });
    connect(
        findChild<QComboBox*>(QStringLiteral("modelPropertySelector")),
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            update_model_view();
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
            if (presented_frame_) {
                present_live_frame(presented_frame_);
            }
#endif
        });
    for (const char* name : {"sliceXSpin", "sliceYSpin", "sliceZSpin"}) {
        connect(
            findChild<QSpinBox*>(QString::fromUtf8(name)),
            &QSpinBox::valueChanged,
            this,
            [this](int) {
                update_model_view();
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
                if (presented_frame_) {
                    present_live_frame(presented_frame_);
                }
#endif
            });
    }
    for (const char* name : {
             "cropXBeginSpin", "cropXEndSpin", "cropYBeginSpin",
             "cropYEndSpin", "cropZBeginSpin", "cropZEndSpin"}) {
        connect(
            findChild<QSpinBox*>(QString::fromUtf8(name)),
            &QSpinBox::valueChanged,
            this,
            [this](int) {
                update_crop_summary();
                update_model_view();
            });
    }
    connect(
        findChild<QPushButton*>(QStringLiteral("createCropButton")),
        &QPushButton::clicked,
        this,
        [this] {
            bool accepted = false;
            const auto stem = QInputDialog::getText(
                this,
                QStringLiteral("创建裁剪模型"),
                QStringLiteral("输出名称（英文、数字、下划线）："),
                QLineEdit::Normal,
                QStringLiteral("crop_001"),
                &accepted);
            if (!accepted) {
                return;
            }
            QString error;
            if (!create_cropped_model(stem, &error)) {
                QMessageBox::critical(this, QStringLiteral("无法创建裁剪模型"), error);
            }
        });
    connect(
        findChild<QSlider*>(QStringLiteral("volumeOpacitySlider")),
        &QSlider::valueChanged,
        this,
        [this](int value) {
            volume_viewport(this)->set_opacity(static_cast<float>(value) / 100.0F);
        });
    connect(
        findChild<QSlider*>(QStringLiteral("volumeThresholdSlider")),
        &QSlider::valueChanged,
        this,
        [this](int value) {
            volume_viewport(this)->set_lower_threshold(
                static_cast<float>(value) / 100.0F);
        });
    connect(
        findChild<QSlider*>(QStringLiteral("liveOpacitySlider")),
        &QSlider::valueChanged,
        this,
        [this](int value) {
            volume_viewport(this)->set_live_opacity(
                static_cast<float>(value) / 100.0F);
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
            if (presented_frame_) {
                update_model_view();
                present_live_frame(presented_frame_);
            }
#endif
        });
    connect(
        findChild<QSlider*>(QStringLiteral("liveThresholdSlider")),
        &QSlider::valueChanged,
        this,
        [this](int value) {
            volume_viewport(this)->set_live_threshold(
                static_cast<float>(value) / 100.0F);
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
            if (presented_frame_) {
                update_model_view();
                present_live_frame(presented_frame_);
            }
#endif
        });
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
    connect(
        findChild<QSpinBox*>(QStringLiteral("displayIntervalSpin")),
        &QSpinBox::valueChanged,
        this,
        [this](int value) {
            if (forward_worker_ && forward_worker_->isRunning()) {
                forward_worker_->request_display_interval(
                    static_cast<std::size_t>(value));
            }
        });
#endif
    connect(
        findChild<QPushButton*>(QStringLiteral("resetVolumeCameraButton")),
        &QPushButton::clicked,
        this,
        [this] { volume_viewport(this)->reset_camera(); });
    experiment_editor(this)->set_callbacks(
        [this] {
            update_experiment_validation();
            update_model_view();
        },
        [this] {
            QString error;
            if (!save_experiment_draft(&error)) {
                QMessageBox::critical(
                    this, QStringLiteral("无法保存实验草稿"), error);
            }
        });
    connect(
        findChild<QListWidget*>(QStringLiteral("moduleNavigation")),
        &QListWidget::currentRowChanged,
        this,
        [this, log_dock, model_information_dock, experiment_editor_dock,
         results_dock](int row) {
            if (row != 6) {
                results_dock->hide();
                log_dock->show();
                log_dock->raise();
            }
            if (row == 1) {
                model_information_dock->show();
                model_information_dock->raise();
                return;
            }
            if (row == 2 || row == 3 || row == 4) {
                experiment_editor_dock->show();
                experiment_editor_dock->raise();
                auto* scroll = findChild<QScrollArea*>(
                    QStringLiteral("experimentEditorScroll"));
                if (row == 2) {
                    scroll->verticalScrollBar()->setValue(0);
                } else if (row == 3) {
                    scroll->ensureWidgetVisible(findChild<QGroupBox*>(
                        QStringLiteral("sourceEditorGroup")));
                } else {
                    scroll->ensureWidgetVisible(findChild<QGroupBox*>(
                        QStringLiteral("acquisitionEditorGroup")));
                }
                return;
            }
            if (row == 6) {
                results_dock->show();
                results_dock->raise();
            }
        });

    QSettings settings;
    if (restore_last_project) {
        restoreGeometry(
            settings.value(QStringLiteral("desktop/geometry")).toByteArray());
        restoreState(
            settings.value(QStringLiteral("desktop/window_state")).toByteArray());
    } else {
        resize(1440, 900);
    }

    statusBar()->setSizeGripEnabled(true);
    statusBar()->showMessage(QStringLiteral("空闲 · 速度模 · 未加载模型"));
    const auto last_project =
        settings.value(QStringLiteral("desktop/last_project")).toString();
    if (restore_last_project && !last_project.isEmpty() &&
        QFileInfo(QDir(last_project).filePath(
                      QString::fromUtf8(kDesktopProjectFile)))
            .isFile()) {
        QString ignored_error;
        static_cast<void>(this->open_project(last_project, &ignored_error));
    }
}

MainWindow::~MainWindow() {
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
    if (forward_worker_) {
        forward_worker_->request_stop();
        forward_worker_->wait();
    }
#endif
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

bool MainWindow::import_hdf5_model(
    const QString& source_path,
    QString* error_message) {
#ifndef WAVE3D_DESKTOP_HAS_HDF5
    static_cast<void>(source_path);
    if (error_message != nullptr) {
        *error_message = QStringLiteral("当前桌面构建未启用 HDF5 模型支持");
    }
    return false;
#else
    if (!project_) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("请先创建或打开项目");
        }
        return false;
    }
    const QFileInfo source(source_path);
    if (!source.isFile()) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("模型文件不存在");
        }
        return false;
    }

    const QDir project_directory(project_root_);
    const auto relative_reference =
        QStringLiteral("models/") + source.fileName();
    const auto destination = project_directory.filePath(relative_reference);
    const auto source_absolute = source.absoluteFilePath();
    const auto destination_absolute = QFileInfo(destination).absoluteFilePath();
    const bool requires_copy = source_absolute != destination_absolute;
    if (requires_copy && QFileInfo::exists(destination_absolute)) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("项目中已存在同名模型，未覆盖原文件");
        }
        return false;
    }

    try {
        bool copied = false;
        try {
            if (requires_copy) {
                copy_file_atomically(source_absolute, destination_absolute);
                copied = true;
            }
            auto scene = std::make_unique<StaticModelScene>(
                StaticModelScene::load_hdf5(destination_absolute));
            auto updated_project = *project_;
            updated_project.model_reference = relative_reference;
            try {
                ProjectWorkspace::save(project_root_, updated_project);
            } catch (...) {
                if (copied) {
                    QFile::remove(destination_absolute);
                }
                throw;
            }
            project_ = std::move(updated_project);
            model_scene_ = std::move(scene);
            volume_property_index_ = -1;
        } catch (...) {
            if (copied) {
                QFile::remove(destination_absolute);
            }
            throw;
        }
        populate_model_information();
        update_model_view();
        findChild<QLabel*>(QStringLiteral("modelStateBadge"))
            ->setText(QStringLiteral("模型已加载"));
        findChild<QDockWidget*>(QStringLiteral("modelInformationDock"))->raise();
        findChild<QTextEdit*>(QStringLiteral("runLog"))
            ->append(QStringLiteral("已加载模型：%1").arg(relative_reference));
        statusBar()->showMessage(QStringLiteral("模型已加载 · 静态中心切面"));
        return true;
    } catch (const std::exception& error) {
        if (error_message != nullptr) {
            *error_message = QString::fromUtf8(error.what());
        }
        return false;
    }
#endif
}

bool MainWindow::convert_segy_model(
    const SegyModelConversionRequest& request,
    QString* error_message) {
#if !defined(WAVE3D_DESKTOP_HAS_HDF5) || !defined(WAVE3D_DESKTOP_HAS_SEGY)
    static_cast<void>(request);
    if (error_message != nullptr) {
        *error_message = QStringLiteral("当前桌面构建需要同时启用 HDF5 与 SEG-Y");
    }
    return false;
#else
    if (!project_) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("请先创建或打开项目");
        }
        return false;
    }
    try {
        const auto artifact = convert_segy_model_artifact(project_root_, request);
        const auto model_path = QDir(project_root_).filePath(artifact.model_reference);
        QString import_error;
        if (!import_hdf5_model(model_path, &import_error)) {
            QFile::remove(model_path);
            QFile::remove(QDir(project_root_).filePath(artifact.manifest_reference));
            throw std::runtime_error(import_error.toStdString());
        }
        findChild<QTextEdit*>(QStringLiteral("runLog"))
            ->append(QStringLiteral("SEG-Y 模型转换清单：%1")
                         .arg(artifact.manifest_reference));
        statusBar()->showMessage(QStringLiteral("SEG-Y 属性模型已转换并加载"));
        return true;
    } catch (const std::exception& error) {
        if (error_message != nullptr) {
            *error_message = QString::fromUtf8(error.what());
        }
        return false;
    }
#endif
}

bool MainWindow::create_cropped_model(
    const QString& output_stem,
    QString* error_message) {
#ifndef WAVE3D_DESKTOP_HAS_HDF5
    static_cast<void>(output_stem);
    if (error_message != nullptr) {
        *error_message = QStringLiteral("当前桌面构建未启用 HDF5 模型支持");
    }
    return false;
#else
    if (!project_ || !model_scene_) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("请先加载有效模型");
        }
        return false;
    }
    std::optional<DerivedModelArtifact> artifact;
    try {
        artifact = create_cropped_model_artifact(
            project_root_,
            project_->model_reference,
            *model_scene_,
            selected_crop_bounds(this),
            output_stem);
        auto next_scene = std::make_unique<StaticModelScene>(
            StaticModelScene::load_hdf5(
                QDir(project_root_).filePath(artifact->model_reference)));
        auto updated_project = *project_;
        updated_project.model_reference = artifact->model_reference;
        ProjectWorkspace::save(project_root_, updated_project);
        project_ = std::move(updated_project);
        model_scene_ = std::move(next_scene);
        volume_property_index_ = -1;
        populate_model_information();
        update_model_view();
        findChild<QTextEdit*>(QStringLiteral("runLog"))
            ->append(QStringLiteral("已创建裁剪模型：%1；来源清单：%2")
                         .arg(
                             artifact->model_reference,
                             artifact->manifest_reference));
        statusBar()->showMessage(QStringLiteral("裁剪模型已创建并加载"));
        return true;
    } catch (const std::exception& error) {
        if (artifact) {
            QFile::remove(QDir(project_root_).filePath(artifact->model_reference));
            QFile::remove(
                QDir(project_root_).filePath(artifact->manifest_reference));
        }
        if (error_message != nullptr) {
            *error_message = QString::fromUtf8(error.what());
        }
        return false;
    }
#endif
}

bool MainWindow::save_experiment_draft(QString* error_message) {
#ifndef WAVE3D_DESKTOP_HAS_HDF5
    if (error_message != nullptr) {
        *error_message = QStringLiteral("当前桌面构建未启用 HDF5 模型支持");
    }
    return false;
#else
    if (!project_ || !model_scene_ || project_->shots.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("请先打开项目并加载有效模型");
        }
        return false;
    }
    try {
        auto draft = experiment_editor(this)->current_draft();
        draft.model_reference = project_->model_reference;
        const auto& summary = model_scene_->summary();
        const auto checked = ExperimentDraftStore::resolve(
            draft, summary.grid, summary.extrema);
#ifdef WAVE3D_DESKTOP_HAS_SEGY
        wave3d::io::require_segy_rev1_sample_axis(
            checked.acquisition.sample_count, checked.simulation.time.dt_s);
#endif
        ExperimentDraftStore::save(project_root_, draft);
        const auto verified = ExperimentDraftStore::load(
            project_root_, draft.shot_id);
        resolved_experiment_ = ExperimentDraftStore::resolve(
            verified, summary.grid, summary.extrema);
        experiment_model_reference_changed_ = false;
        experiment_editor(this)->mark_saved();
        findChild<QTextEdit*>(QStringLiteral("runLog"))
            ->append(QStringLiteral("已保存实验草稿：%1")
                         .arg(ExperimentDraftStore::relative_path(draft.shot_id)));
        statusBar()->showMessage(
            QStringLiteral("工作区、震源与观测系统草稿已保存"));
        return true;
    } catch (const std::exception& error) {
        if (error_message != nullptr) {
            *error_message = QString::fromUtf8(error.what());
        }
        return false;
    }
#endif
}

bool MainWindow::preflight_experiment(
    const QString& run_id,
    QString* error_message) {
#if !defined(WAVE3D_DESKTOP_HAS_HDF5) || !defined(WAVE3D_DESKTOP_HAS_YAML) || \
    !defined(WAVE3D_DESKTOP_HAS_SEGY)
    static_cast<void>(run_id);
    if (error_message != nullptr) {
        *error_message =
            QStringLiteral("实验预检需要同时启用 HDF5、YAML 与 SEG-Y");
    }
    return false;
#else
    if (!project_ || !model_scene_ || !resolved_experiment_ ||
        project_->shots.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("请先完成有效的模型、震源和观测系统配置");
        }
        return false;
    }
    std::optional<PreparedRun> prepared;
    try {
        const auto model_path =
            QStringLiteral("../../") + project_->model_reference;
        wave3d::io::ForwardRunConfiguration configuration{
            resolved_experiment_->simulation,
            resolved_experiment_->source,
            resolved_experiment_->receivers,
            model_path.toStdString(),
            "output"};
        wave3d::io::require_valid_run_configuration(configuration);
        const auto yaml = wave3d::io::resolved_yaml(configuration);
        prepared = ProjectWorkspace::prepare_run(
            project_root_,
            *project_,
            run_id,
            project_->shots.front().id,
            QByteArray::fromStdString(yaml));
        const auto loaded = wave3d::io::load_yaml_run_configuration(
            prepared->configuration_path.toStdString());
        if (wave3d::io::resolved_yaml(loaded) != yaml) {
            throw std::runtime_error(
                "prepared YAML did not round trip to the resolved configuration");
        }
        prepared_run_ = *prepared;
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
        findChild<QAction*>(QStringLiteral("startRunAction"))->setEnabled(true);
        findChild<QPushButton*>(QStringLiteral("startRunButton"))->setEnabled(true);
#else
        findChild<QAction*>(QStringLiteral("startRunAction"))->setToolTip(
            QStringLiteral("当前构建未启用 CUDA 正演执行"));
#endif
        findChild<QLabel*>(QStringLiteral("runStateLabel"))
            ->setText(QStringLiteral("预检完成 · %1").arg(run_id));
        findChild<QTextEdit*>(QStringLiteral("runLog"))
            ->append(
                QStringLiteral("正演预检完成：%1；%2 个接收器；配置 %3")
                    .arg(run_id)
                    .arg(resolved_experiment_->receivers.size())
                    .arg(prepared->configuration_path));
        statusBar()->showMessage(
            QStringLiteral("预检完成 · 已生成不可变运行配置"));
        return true;
    } catch (const std::exception& error) {
        if (prepared) {
            QDir(prepared->directory).removeRecursively();
        }
        if (error_message != nullptr) {
            *error_message = QString::fromUtf8(error.what());
        }
        return false;
    }
#endif
}

bool MainWindow::start_prepared_run(QString* error_message) {
#ifndef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
    if (error_message != nullptr) {
        *error_message = QStringLiteral(
            "当前构建需要同时启用 CUDA、HDF5、YAML 与 SEG-Y 才能执行正演");
    }
    return false;
#else
    if (!prepared_run_) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("请先完成实验预检并生成不可变运行配置");
        }
        return false;
    }
    if (forward_worker_ && forward_worker_->isRunning()) {
        if (error_message != nullptr) {
            *error_message = QStringLiteral("已有正演任务正在运行");
        }
        return false;
    }
    try {
        forward_worker_ = std::make_unique<ForwardRunWorker>(
            prepared_run_->configuration_path, 1);
        const auto field_key =
            findChild<QComboBox*>(QStringLiteral("displayFieldSelector"))
                ->currentData()
                .toString();
        forward_worker_->request_visualization_field(
            visualization_field_from_key(field_key));
        forward_worker_->request_display_interval(
            static_cast<std::size_t>(
                findChild<QSpinBox*>(QStringLiteral("displayIntervalSpin"))
                    ->value()));
        presented_frame_.reset();
        volume_viewport(this)->clear_live_volume();
        setProperty(
            "lastPresentedLiveFrameSequence",
            QVariant::fromValue<qulonglong>(0));
        for (const char* name : {"xyViewport", "xzViewport", "yzViewport"}) {
            findChild<QOpenGLWidget*>(QString::fromUtf8(name))
                ->setProperty(
                    "lastPresentedLiveFrameSequence",
                    QVariant::fromValue<qulonglong>(0));
        }
        findChild<QProgressBar*>(QStringLiteral("runProgress"))->setValue(0);
        findChild<QLabel*>(QStringLiteral("liveFrameLabel"))
            ->setText(QStringLiteral("波场帧：等待第一个同步步"));
        findChild<QLabel*>(QStringLiteral("runStateLabel"))
            ->setText(QStringLiteral("正在准备 CUDA 正演"));
        findChild<QAction*>(QStringLiteral("startRunAction"))->setEnabled(false);
        findChild<QPushButton*>(QStringLiteral("startRunButton"))->setEnabled(false);
        findChild<QPushButton*>(QStringLiteral("pauseRunButton"))->setEnabled(false);
        findChild<QPushButton*>(QStringLiteral("resumeRunButton"))->setEnabled(false);
        findChild<QPushButton*>(QStringLiteral("stopRunButton"))->setEnabled(true);
        set_run_editing_locked(true);
        findChild<QTextEdit*>(QStringLiteral("runLog"))
            ->append(QStringLiteral("后台正演已启动：%1")
                         .arg(prepared_run_->configuration_path));
        statusBar()->showMessage(QStringLiteral("CUDA 正演准备中"));
        forward_worker_->start();
        run_poll_timer_->start();
        return true;
    } catch (const std::exception& error) {
        forward_worker_.reset();
        set_run_editing_locked(false);
        if (error_message != nullptr) {
            *error_message = QString::fromUtf8(error.what());
        }
        return false;
    }
#endif
}

void MainWindow::invalidate_prepared_run() {
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
    if (forward_worker_ && forward_worker_->isRunning()) {
        return;
    }
#endif
    const bool had_prepared = prepared_run_.has_value();
    prepared_run_.reset();
    findChild<QAction*>(QStringLiteral("startRunAction"))->setEnabled(false);
    findChild<QPushButton*>(QStringLiteral("startRunButton"))->setEnabled(false);
    if (had_prepared) {
        findChild<QLabel*>(QStringLiteral("runStateLabel"))
            ->setText(QStringLiteral("配置已修改 · 请重新预检"));
    }
}

void MainWindow::set_run_editing_locked(bool locked) {
    findChild<QAction*>(QStringLiteral("newProjectAction"))->setEnabled(!locked);
    findChild<QAction*>(QStringLiteral("openProjectAction"))->setEnabled(!locked);
    findChild<QAction*>(QStringLiteral("importHdf5ModelAction"))
        ->setEnabled(!locked && project_.has_value());
#if defined(WAVE3D_DESKTOP_HAS_HDF5) && defined(WAVE3D_DESKTOP_HAS_SEGY)
    findChild<QAction*>(QStringLiteral("convertSegyModelAction"))
        ->setEnabled(!locked && project_.has_value());
#endif
    experiment_editor(this)->setEnabled(!locked && model_scene_ != nullptr);
    if (locked) {
        findChild<QAction*>(QStringLiteral("validateExperimentAction"))
            ->setEnabled(false);
        findChild<QPushButton*>(QStringLiteral("createCropButton"))
            ->setEnabled(false);
    } else {
        update_crop_summary();
        update_experiment_validation();
    }
}

void MainWindow::poll_forward_run() {
#ifndef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
    return;
#else
    if (!forward_worker_) {
        run_poll_timer_->stop();
        return;
    }
    const auto snapshot = forward_worker_->snapshot();
    if (snapshot.latest_frame &&
        (!presented_frame_ ||
         snapshot.latest_frame->sequence != presented_frame_->sequence)) {
        try {
            update_model_view();
            present_live_frame(snapshot.latest_frame);
        } catch (const std::exception& error) {
            presented_frame_ = snapshot.latest_frame;
            findChild<QLabel*>(QStringLiteral("liveFrameLabel"))
                ->setText(QStringLiteral("实时波场显示失败：%1")
                              .arg(QString::fromUtf8(error.what())));
            findChild<QTextEdit*>(QStringLiteral("runLog"))
                ->append(QStringLiteral("实时波场显示失败：%1")
                             .arg(QString::fromUtf8(error.what())));
        }
    }
    auto* progress = findChild<QProgressBar*>(QStringLiteral("runProgress"));
    if (snapshot.total_steps > 0) {
        const auto percent = static_cast<int>(
            std::min<std::size_t>(
                100,
                snapshot.completed_steps * 100 / snapshot.total_steps));
        progress->setValue(percent);
    }

    auto* pause = findChild<QPushButton*>(QStringLiteral("pauseRunButton"));
    auto* resume = findChild<QPushButton*>(QStringLiteral("resumeRunButton"));
    auto* stop = findChild<QPushButton*>(QStringLiteral("stopRunButton"));
    auto* state_label = findChild<QLabel*>(QStringLiteral("runStateLabel"));
    pause->setEnabled(snapshot.state == ForwardRunState::Running);
    resume->setEnabled(snapshot.state == ForwardRunState::Paused);
    stop->setEnabled(
        snapshot.state == ForwardRunState::Preparing ||
        snapshot.state == ForwardRunState::Running ||
        snapshot.state == ForwardRunState::Paused);

    switch (snapshot.state) {
    case ForwardRunState::Idle:
        state_label->setText(QStringLiteral("空闲"));
        break;
    case ForwardRunState::Preparing:
        state_label->setText(QStringLiteral("正在准备 CUDA 正演"));
        break;
    case ForwardRunState::Running:
        state_label->setText(
            QStringLiteral("传播中 · %1 / %2 步")
                .arg(snapshot.completed_steps)
                .arg(snapshot.total_steps));
        break;
    case ForwardRunState::Paused:
        state_label->setText(
            QStringLiteral("已暂停 · %1 / %2 步")
                .arg(snapshot.completed_steps)
                .arg(snapshot.total_steps));
        break;
    case ForwardRunState::Stopping:
        state_label->setText(QStringLiteral("正在停止 · 等待当前步完成"));
        break;
    case ForwardRunState::Finalizing:
        state_label->setText(QStringLiteral("正在校验并发布 SEG-Y"));
        break;
    case ForwardRunState::Completed:
        state_label->setText(QStringLiteral("正演完成 · 正在登记结果"));
        break;
    case ForwardRunState::Cancelled:
        state_label->setText(QStringLiteral("已停止"));
        break;
    case ForwardRunState::Failed:
        state_label->setText(QStringLiteral("正演失败"));
        break;
    }

    const bool terminal = snapshot.state == ForwardRunState::Completed ||
                          snapshot.state == ForwardRunState::Cancelled ||
                          snapshot.state == ForwardRunState::Failed;
    if (!terminal || forward_worker_->isRunning()) {
        return;
    }
    forward_worker_->wait();
    run_poll_timer_->stop();

    QString terminal_message;
    try {
        if (!prepared_run_) {
            throw std::logic_error("completed worker has no prepared run identity");
        }
        RunTerminalResult result;
        if (snapshot.state == ForwardRunState::Completed) {
            if (!snapshot.report) {
                throw std::logic_error("completed worker has no production report");
            }
            const auto& report = *snapshot.report;
            RunProduct run_product;
            const std::array output_files{
                std::pair{QStringLiteral("vx"), report.output_segy_paths.vx},
                std::pair{QStringLiteral("vy"), report.output_segy_paths.vy},
                std::pair{QStringLiteral("vz"), report.output_segy_paths.vz}};
            qint64 total_bytes = 0;
            for (const auto& [component, path] : output_files) {
                QFile product(QString::fromStdString(path));
                if (!product.open(QIODevice::ReadOnly)) {
                    throw std::runtime_error(
                        "cannot read completed SEG-Y component product");
                }
                QCryptographicHash hash(QCryptographicHash::Sha256);
                if (!hash.addData(&product)) {
                    throw std::runtime_error(
                        "cannot checksum completed SEG-Y component product");
                }
                const QFileInfo information(product);
                run_product.files.push_back({
                    component,
                    QDir(prepared_run_->directory)
                        .relativeFilePath(information.absoluteFilePath()),
                    QString::fromLatin1(hash.result().toHex()),
                    information.size()});
                total_bytes += information.size();
            }
            run_product.receiver_count = report.receiver_count;
            run_product.sample_count = report.sample_count;
            run_product.device_name = QString::fromStdString(report.device_name);
            run_product.input_load_ms = report.input_load_ms;
            run_product.setup_ms = report.setup_ms;
            run_product.propagation_ms = report.propagation_ms;
            run_product.trace_download_ms = report.trace_download_ms;
            run_product.segy_write_ms = report.segy_write_ms;
            result = {RunTerminalState::Completed, QString(), run_product};
            terminal_message = QStringLiteral(
                                   "正演完成：%1 个接收器 × %2 个采样；"
                                   "Vx/Vy/Vz 三个 SEG-Y，共 %3 字节")
                                   .arg(report.receiver_count)
                                   .arg(report.sample_count)
                                   .arg(total_bytes);
            progress->setValue(100);
        } else if (snapshot.state == ForwardRunState::Cancelled) {
            result = {
                RunTerminalState::Cancelled,
                QStringLiteral("用户在批次边界停止任务"),
                std::nullopt};
            terminal_message = QStringLiteral("正演已停止，未发布 SEG-Y 产品");
        } else {
            result = {
                RunTerminalState::Failed,
                snapshot.diagnostic,
                std::nullopt};
            terminal_message =
                QStringLiteral("正演失败：%1").arg(snapshot.diagnostic);
        }
        const auto result_path =
            ProjectWorkspace::publish_run_result(*prepared_run_, result);
        terminal_message += QStringLiteral("；结果记录 %1").arg(result_path);
        if (snapshot.state == ForwardRunState::Completed) {
            result_workspace(this)->set_project_root(project_root_);
            state_label->setText(QStringLiteral("正演完成"));
        } else if (snapshot.state == ForwardRunState::Cancelled) {
            state_label->setText(QStringLiteral("已停止"));
        } else {
            state_label->setText(QStringLiteral("正演失败"));
        }
    } catch (const std::exception& error) {
        terminal_message = QStringLiteral("运行结果登记失败：%1")
                               .arg(QString::fromUtf8(error.what()));
        state_label->setText(QStringLiteral("结果登记失败"));
    }

    findChild<QTextEdit*>(QStringLiteral("runLog"))->append(terminal_message);
    statusBar()->showMessage(terminal_message);
    volume_viewport(this)->clear_live_volume();
    for (const char* name : {"xyViewport", "xzViewport", "yzViewport"}) {
        findChild<QOpenGLWidget*>(QString::fromUtf8(name))
            ->setProperty(
                "liveWavefieldFrameSequence",
                QVariant::fromValue<qulonglong>(0));
    }
    presented_frame_.reset();
    forward_worker_.reset();
    prepared_run_.reset();
    pause->setEnabled(false);
    resume->setEnabled(false);
    stop->setEnabled(false);
    findChild<QLabel*>(QStringLiteral("liveFrameLabel"))
        ->setText(QStringLiteral("波场帧：运行已结束"));
    set_run_editing_locked(false);
    update_model_view();
#endif
}

#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
void MainWindow::present_live_frame(
    std::shared_ptr<const LiveWavefieldFrame> frame) {
    if (!frame || !model_scene_ ||
        !same_grid_geometry(frame->grid, model_scene_->summary().grid)) {
        throw std::invalid_argument("live frame does not match the active model");
    }
    const auto& grid = frame->grid;
    const std::array<float, 3> extents{
        (grid.nx > 1 ? static_cast<float>(grid.nx - 1) : 1.0F) * grid.dx_m,
        (grid.ny > 1 ? static_cast<float>(grid.ny - 1) : 1.0F) * grid.dy_m,
        (grid.nz > 1 ? static_cast<float>(grid.nz - 1) : 1.0F) * grid.dz_m};
    const auto maximum_extent = *std::max_element(extents.begin(), extents.end());
    volume_viewport(this)->set_live_volume(
        {grid.nx,
         grid.ny,
         grid.nz,
         frame->value_count,
         frame->normalized_values,
         {extents[0] / maximum_extent,
          extents[1] / maximum_extent,
          extents[2] / maximum_extent},
         frame->signed_scale,
         frame->sequence},
        visualization_field_key(frame->field));

    const auto opacity = static_cast<float>(
                             findChild<QSlider*>(
                                 QStringLiteral("liveOpacitySlider"))
                                 ->value()) /
                         100.0F;
    const auto threshold = static_cast<float>(
                               findChild<QSlider*>(
                                   QStringLiteral("liveThresholdSlider"))
                                   ->value()) /
                           100.0F;
    const auto x = static_cast<std::size_t>(
        findChild<QSpinBox*>(QStringLiteral("sliceXSpin"))->value());
    const auto y = static_cast<std::size_t>(
        findChild<QSpinBox*>(QStringLiteral("sliceYSpin"))->value());
    const auto z = static_cast<std::size_t>(
        findChild<QSpinBox*>(QStringLiteral("sliceZSpin"))->value());
    const auto time_text = QStringLiteral("t=%1 ms · 帧 %2")
                               .arg(frame->velocity_time_s * 1000.0, 0, 'f', 3)
                               .arg(frame->sequence);
    const std::array<std::tuple<QString, int, std::size_t, QString>, 3> slices{{
        {QStringLiteral("xyViewport"),
         0,
         z,
         QStringLiteral("z=%1 · %2 m · %3")
             .arg(z)
             .arg(z * grid.dz_m)
             .arg(time_text)},
        {QStringLiteral("xzViewport"),
         1,
         y,
         QStringLiteral("y=%1 · %2 m · %3")
             .arg(y)
             .arg(y * grid.dy_m)
             .arg(time_text)},
        {QStringLiteral("yzViewport"),
         2,
         x,
         QStringLiteral("x=%1 · %2 m · %3")
             .arg(x)
             .arg(x * grid.dx_m)
             .arg(time_text)}}};
    for (const auto& [name, orientation, fixed_index, message] : slices) {
        auto* viewport = scientific_viewport(this, name);
        viewport->set_scientific_image(
            composite_live_wavefield_slice(
                viewport->scientific_image(),
                *frame,
                orientation,
                fixed_index,
                opacity,
                threshold),
            message);
        viewport->setProperty(
            "liveWavefieldFrameSequence",
            QVariant::fromValue<qulonglong>(frame->sequence));
        viewport->setProperty(
            "liveWavefieldCompletedSteps",
            QVariant::fromValue<qulonglong>(frame->completed_steps));
        viewport->setProperty(
            "lastPresentedLiveFrameSequence",
            QVariant::fromValue<qulonglong>(frame->sequence));
    }
    setProperty(
        "lastPresentedLiveFrameSequence",
        QVariant::fromValue<qulonglong>(frame->sequence));
    findChild<QLabel*>(QStringLiteral("liveFrameLabel"))
        ->setText(
            QStringLiteral(
                "%1 · 帧 %2 · 第 %3 步 · %4 ms · [%5, %6] %7 · "
                "提取/传输/归一化 %8/%9/%10 ms")
                .arg(visualization_field_name(frame->field))
                .arg(frame->sequence)
                .arg(frame->completed_steps)
                .arg(frame->velocity_time_s * 1000.0, 0, 'f', 3)
                .arg(frame->physical_minimum, 0, 'e', 3)
                .arg(frame->physical_maximum, 0, 'e', 3)
                .arg(visualization_field_unit(frame->field))
                .arg(frame->extraction_ms, 0, 'f', 3)
                .arg(frame->transfer_ms, 0, 'f', 3)
                .arg(frame->normalization_ms, 0, 'f', 3));
    presented_frame_ = std::move(frame);
}
#endif

const ProjectDocument* MainWindow::current_project() const noexcept {
    return project_ ? &*project_ : nullptr;
}

const QString& MainWindow::current_project_root() const noexcept {
    return project_root_;
}

void MainWindow::closeEvent(QCloseEvent* event) {
#ifdef WAVE3D_DESKTOP_HAS_CUDA_FORWARD
    if (forward_worker_ && forward_worker_->isRunning()) {
        forward_worker_->request_stop();
        forward_worker_->wait();
        poll_forward_run();
    }
#endif
    save_window_settings();
    QMainWindow::closeEvent(event);
}

void MainWindow::activate_project(
    QString root_directory,
    ProjectDocument project) {
    project_root_ = std::move(root_directory);
    project_ = std::move(project);
    clear_model_view();

    findChild<QLabel*>(QStringLiteral("projectNameLabel"))->setText(project_->name);
    findChild<QLabel*>(QStringLiteral("projectPathLabel"))->setText(project_root_);
    findChild<QLabel*>(QStringLiteral("projectShotCountLabel"))
        ->setText(QString::number(project_->shots.size()));
    findChild<QLabel*>(QStringLiteral("workspaceTitle"))->setText(project_->name);
    findChild<QLabel*>(QStringLiteral("modelStateBadge"))
        ->setText(
            project_->model_reference.isEmpty() ? QStringLiteral("未加载模型")
                                                 : QStringLiteral("模型已引用"));
#ifdef WAVE3D_DESKTOP_HAS_HDF5
    findChild<QAction*>(QStringLiteral("importHdf5ModelAction"))->setEnabled(true);
#if defined(WAVE3D_DESKTOP_HAS_HDF5) && defined(WAVE3D_DESKTOP_HAS_SEGY)
    findChild<QAction*>(QStringLiteral("convertSegyModelAction"))->setEnabled(true);
#endif
#endif

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
    result_workspace(this)->set_project_root(project_root_);

#ifdef WAVE3D_DESKTOP_HAS_HDF5
    if (!project_->model_reference.isEmpty()) {
        try {
            model_scene_ = std::make_unique<StaticModelScene>(
                StaticModelScene::load_hdf5(
                    QDir(project_root_).filePath(project_->model_reference)));
            populate_model_information();
            update_model_view();
            findChild<QLabel*>(QStringLiteral("modelStateBadge"))
                ->setText(QStringLiteral("模型已加载"));
        } catch (const std::exception& error) {
            findChild<QLabel*>(QStringLiteral("modelStateBadge"))
                ->setText(QStringLiteral("模型引用无效"));
            findChild<QTextEdit*>(QStringLiteral("runLog"))
                ->append(QStringLiteral("模型自动加载失败：%1")
                             .arg(QString::fromUtf8(error.what())));
        }
    }
#endif
}

void MainWindow::clear_model_view() {
    model_scene_.reset();
    resolved_experiment_.reset();
    invalidate_prepared_run();
    experiment_model_reference_changed_ = false;
    volume_property_index_ = -1;
    volume_viewport(this)->clear_volume();
    findChild<QAction*>(QStringLiteral("validateExperimentAction"))
        ->setEnabled(false);
    experiment_editor(this)->clear_model_context();
    auto* property =
        findChild<QComboBox*>(QStringLiteral("modelPropertySelector"));
    property->setEnabled(false);
    property->setCurrentIndex(0);
    for (const char* name : {
             "modelGridLabel",
             "modelSpacingLabel",
             "modelExtentLabel",
             "modelVpRangeLabel",
             "modelVsRangeLabel",
             "modelDensityRangeLabel",
             "modelCenterLabel"}) {
        findChild<QLabel*>(QString::fromUtf8(name))->setText(QStringLiteral("—"));
    }
    for (const char* name : {
             "sliceXSpin", "sliceYSpin", "sliceZSpin",
             "cropXBeginSpin", "cropXEndSpin", "cropYBeginSpin",
             "cropYEndSpin", "cropZBeginSpin", "cropZEndSpin"}) {
        auto* spin = findChild<QSpinBox*>(QString::fromUtf8(name));
        spin->setEnabled(false);
        spin->setRange(0, 0);
        spin->setValue(0);
    }
    findChild<QLabel*>(QStringLiteral("cropSummaryLabel"))
        ->setText(QStringLiteral("—"));
    findChild<QPushButton*>(QStringLiteral("createCropButton"))
        ->setEnabled(false);
    for (const char* name : {"volumeOpacitySlider", "volumeThresholdSlider"}) {
        findChild<QSlider*>(QString::fromUtf8(name))->setEnabled(false);
    }
    findChild<QPushButton*>(QStringLiteral("resetVolumeCameraButton"))
        ->setEnabled(false);
    for (const auto& viewport : std::array{
             std::pair{QStringLiteral("xyViewport"),
                       QStringLiteral("等待科学数据")},
             std::pair{QStringLiteral("xzViewport"),
                       QStringLiteral("等待科学数据")},
             std::pair{QStringLiteral("yzViewport"),
                       QStringLiteral("等待科学数据")}}) {
        scientific_viewport(this, viewport.first)
            ->clear_scientific_image(viewport.second);
        findChild<QOpenGLWidget*>(viewport.first)
            ->setProperty("sourceMarkerVisible", false);
        findChild<QOpenGLWidget*>(viewport.first)
            ->setProperty("sourceMarkerOnPlane", false);
        findChild<QOpenGLWidget*>(viewport.first)
            ->setProperty("receiverCount", QVariant::fromValue<qulonglong>(0));
    }
}

void MainWindow::populate_model_information() {
#ifndef WAVE3D_DESKTOP_HAS_HDF5
    return;
#else
    if (!model_scene_) {
        return;
    }
    const auto& summary = model_scene_->summary();
    const auto range = [](float minimum, float maximum, const QString& unit) {
        return QStringLiteral("%1 – %2 %3")
            .arg(minimum, 0, 'f', 1)
            .arg(maximum, 0, 'f', 1)
            .arg(unit);
    };
    findChild<QLabel*>(QStringLiteral("modelGridLabel"))
        ->setText(QStringLiteral("%1 × %2 × %3")
                      .arg(summary.grid.nx)
                      .arg(summary.grid.ny)
                      .arg(summary.grid.nz));
    findChild<QLabel*>(QStringLiteral("modelSpacingLabel"))
        ->setText(QStringLiteral("%1 / %2 / %3 m")
                      .arg(summary.grid.dx_m)
                      .arg(summary.grid.dy_m)
                      .arg(summary.grid.dz_m));
    findChild<QLabel*>(QStringLiteral("modelExtentLabel"))
        ->setText(QStringLiteral("%1 / %2 / %3 m")
                      .arg(summary.maximum_coordinate_m.x_m)
                      .arg(summary.maximum_coordinate_m.y_m)
                      .arg(summary.maximum_coordinate_m.z_m));
    findChild<QLabel*>(QStringLiteral("modelVpRangeLabel"))
        ->setText(range(
            summary.extrema.minimum.vp_m_s,
            summary.extrema.maximum.vp_m_s,
            QStringLiteral("m/s")));
    findChild<QLabel*>(QStringLiteral("modelVsRangeLabel"))
        ->setText(range(
            summary.extrema.minimum.vs_m_s,
            summary.extrema.maximum.vs_m_s,
            QStringLiteral("m/s")));
    findChild<QLabel*>(QStringLiteral("modelDensityRangeLabel"))
        ->setText(range(
            summary.extrema.minimum.density_kg_m3,
            summary.extrema.maximum.density_kg_m3,
            QStringLiteral("kg/m³")));
    findChild<QLabel*>(QStringLiteral("modelCenterLabel"))
        ->setText(QStringLiteral("x=%1, y=%2, z=%3")
                      .arg(summary.center_x)
                      .arg(summary.center_y)
                      .arg(summary.center_z));
    findChild<QComboBox*>(QStringLiteral("modelPropertySelector"))
        ->setEnabled(true);
    findChild<QSlider*>(QStringLiteral("volumeOpacitySlider"))->setEnabled(true);
    findChild<QSlider*>(QStringLiteral("volumeThresholdSlider"))->setEnabled(true);
    findChild<QPushButton*>(QStringLiteral("resetVolumeCameraButton"))
        ->setEnabled(true);
    const std::array<std::tuple<const char*, std::size_t, std::size_t>, 3>
        slice_controls{{
            {"sliceXSpin", summary.grid.nx, summary.center_x},
            {"sliceYSpin", summary.grid.ny, summary.center_y},
            {"sliceZSpin", summary.grid.nz, summary.center_z}}};
    for (const auto& [name, count, center] : slice_controls) {
        auto* spin = findChild<QSpinBox*>(QString::fromUtf8(name));
        const QSignalBlocker blocker(spin);
        spin->setRange(0, static_cast<int>(count - 1));
        spin->setValue(static_cast<int>(center));
        spin->setEnabled(true);
    }
    const std::array<std::tuple<const char*, std::size_t, bool>, 6>
        crop_controls{{
            {"cropXBeginSpin", summary.grid.nx, false},
            {"cropXEndSpin", summary.grid.nx, true},
            {"cropYBeginSpin", summary.grid.ny, false},
            {"cropYEndSpin", summary.grid.ny, true},
            {"cropZBeginSpin", summary.grid.nz, false},
            {"cropZEndSpin", summary.grid.nz, true}}};
    for (const auto& [name, count, upper] : crop_controls) {
        auto* spin = findChild<QSpinBox*>(QString::fromUtf8(name));
        const QSignalBlocker blocker(spin);
        spin->setRange(0, static_cast<int>(count - 1));
        spin->setValue(upper ? static_cast<int>(count - 1) : 0);
        spin->setEnabled(true);
    }
    update_crop_summary();
    configure_experiment_editor();
#endif
}

void MainWindow::configure_experiment_editor() {
#ifndef WAVE3D_DESKTOP_HAS_HDF5
    return;
#else
    if (!project_ || !model_scene_ || project_->shots.isEmpty()) {
        experiment_editor(this)->clear_model_context();
        resolved_experiment_.reset();
        return;
    }
    const auto& summary = model_scene_->summary();
    const auto& shot_id = project_->shots.front().id;
    try {
        const bool stored =
            ExperimentDraftStore::exists(project_root_, shot_id);
        auto draft = stored
                         ? ExperimentDraftStore::load(project_root_, shot_id)
                         : ExperimentDraftStore::defaults(
                               shot_id,
                               project_->model_reference,
                               summary.grid,
                               summary.extrema);
        experiment_model_reference_changed_ =
            stored && draft.model_reference != project_->model_reference;
        draft.model_reference = project_->model_reference;
        experiment_editor(this)->set_model_context(
            summary.grid,
            draft,
            stored,
            experiment_model_reference_changed_);
        update_experiment_validation();
    } catch (const std::exception& error) {
        resolved_experiment_.reset();
        experiment_editor(this)->clear_model_context();
        experiment_editor(this)->show_validation_error(
            QString::fromUtf8(error.what()));
        findChild<QTextEdit*>(QStringLiteral("runLog"))
            ->append(QStringLiteral("实验草稿加载失败：%1")
                         .arg(QString::fromUtf8(error.what())));
    }
#endif
}

void MainWindow::update_experiment_validation() {
#ifndef WAVE3D_DESKTOP_HAS_HDF5
    return;
#else
    invalidate_prepared_run();
    if (!project_ || !model_scene_) {
        resolved_experiment_.reset();
        return;
    }
    try {
        auto draft = experiment_editor(this)->current_draft();
        draft.model_reference = project_->model_reference;
        const auto& summary = model_scene_->summary();
        resolved_experiment_ = ExperimentDraftStore::resolve(
            draft, summary.grid, summary.extrema);
#ifdef WAVE3D_DESKTOP_HAS_SEGY
        wave3d::io::require_segy_rev1_sample_axis(
            resolved_experiment_->acquisition.sample_count,
            resolved_experiment_->simulation.time.dt_s);
#endif
        experiment_editor(this)->show_validation(
            *resolved_experiment_, experiment_model_reference_changed_);
#if defined(WAVE3D_DESKTOP_HAS_HDF5) && defined(WAVE3D_DESKTOP_HAS_YAML) && \
    defined(WAVE3D_DESKTOP_HAS_SEGY)
        findChild<QAction*>(QStringLiteral("validateExperimentAction"))
            ->setEnabled(true);
#endif
    } catch (const std::exception& error) {
        resolved_experiment_.reset();
        findChild<QAction*>(QStringLiteral("validateExperimentAction"))
            ->setEnabled(false);
        experiment_editor(this)->show_validation_error(
            QString::fromUtf8(error.what()));
    }
#endif
}

void MainWindow::update_crop_summary() {
#ifndef WAVE3D_DESKTOP_HAS_HDF5
    return;
#else
    if (!model_scene_) {
        return;
    }
    const auto bounds = selected_crop_bounds(this);
    const bool valid = bounds.x_begin < bounds.x_end &&
                       bounds.y_begin < bounds.y_end &&
                       bounds.z_begin < bounds.z_end;
    auto* button = findChild<QPushButton*>(QStringLiteral("createCropButton"));
    button->setEnabled(valid);
    auto* label = findChild<QLabel*>(QStringLiteral("cropSummaryLabel"));
    if (!valid) {
        label->setText(QStringLiteral("范围无效：起点必须不大于终点"));
        return;
    }
    const auto& grid = model_scene_->summary().grid;
    const auto nx = bounds.x_end - bounds.x_begin;
    const auto ny = bounds.y_end - bounds.y_begin;
    const auto nz = bounds.z_end - bounds.z_begin;
    label->setText(QStringLiteral("%1 × %2 × %3 网格 · %4 / %5 / %6 m")
                       .arg(nx)
                       .arg(ny)
                       .arg(nz)
                       .arg((nx - 1) * grid.dx_m)
                       .arg((ny - 1) * grid.dy_m)
                       .arg((nz - 1) * grid.dz_m));
#endif
}

void MainWindow::update_model_view() {
#ifndef WAVE3D_DESKTOP_HAS_HDF5
    return;
#else
    if (!model_scene_) {
        return;
    }
    const auto field =
        findChild<QComboBox*>(QStringLiteral("modelPropertySelector"))
            ->currentData()
            .toString();
    const auto property = field == QStringLiteral("vs")
                              ? ModelProperty::Vs
                              : field == QStringLiteral("density")
                                    ? ModelProperty::Density
                                    : ModelProperty::Vp;
    const auto x = static_cast<std::size_t>(
        findChild<QSpinBox*>(QStringLiteral("sliceXSpin"))->value());
    const auto y = static_cast<std::size_t>(
        findChild<QSpinBox*>(QStringLiteral("sliceYSpin"))->value());
    const auto z = static_cast<std::size_t>(
        findChild<QSpinBox*>(QStringLiteral("sliceZSpin"))->value());
    const auto& grid = model_scene_->summary().grid;
    auto crop = selected_crop_bounds(this);
    if (crop.x_begin >= crop.x_end || crop.y_begin >= crop.y_end ||
        crop.z_begin >= crop.z_end) {
        crop = {0, grid.nx, 0, grid.ny, 0, grid.nz};
    }
    auto* volume = volume_viewport(this);
    const auto property_index =
        findChild<QComboBox*>(QStringLiteral("modelPropertySelector"))
            ->currentIndex();
    if (volume_property_index_ != property_index) {
        volume->set_volume(model_scene_->volume_texture(property), field);
        volume_property_index_ = property_index;
    }
    volume->set_crop_bounds(model_scene_->normalized_crop_bounds(crop));
    std::optional<std::array<double, 3>> source_index;
    std::optional<std::array<float, 3>> source_texture;
    std::vector<std::array<double, 3>> receiver_indices;
    std::vector<std::array<float, 3>> receiver_textures;
    if (resolved_experiment_) {
        const auto& point = resolved_experiment_->source.physical_location;
        source_index = std::array<double, 3>{
            point.x_m / grid.dx_m,
            point.y_m / grid.dy_m,
            point.z_m / grid.dz_m};
        const auto normalized = [](double coordinate, float spacing, std::size_t count) {
            return count == 1
                       ? 0.5F
                       : static_cast<float>(
                             coordinate /
                             (static_cast<double>(spacing) * (count - 1)));
        };
        source_texture = std::array<float, 3>{
            normalized(point.x_m, grid.dx_m, grid.nx),
            normalized(point.y_m, grid.dy_m, grid.ny),
            normalized(point.z_m, grid.dz_m, grid.nz)};
        receiver_indices.reserve(resolved_experiment_->receivers.size());
        receiver_textures.reserve(resolved_experiment_->receivers.size());
        for (const auto& receiver : resolved_experiment_->receivers) {
            receiver_indices.push_back({
                receiver.x_m / grid.dx_m,
                receiver.y_m / grid.dy_m,
                receiver.z_m / grid.dz_m});
            receiver_textures.push_back({
                normalized(receiver.x_m, grid.dx_m, grid.nx),
                normalized(receiver.y_m, grid.dy_m, grid.ny),
                normalized(receiver.z_m, grid.dz_m, grid.nz)});
        }
    }
    volume->set_source_position(source_texture);
    volume->set_receiver_positions(std::move(receiver_textures));
    auto xy = annotate_section(
        model_scene_->xy_slice(property, z),
        0,
        x,
        y,
        z,
        crop,
        source_index,
        receiver_indices);
    scientific_viewport(this, QStringLiteral("xyViewport"))
        ->set_scientific_image(
            std::move(xy),
            QStringLiteral("z=%1 · %2 m").arg(z).arg(
                z * model_scene_->summary().grid.dz_m));
    scientific_viewport(this, QStringLiteral("xzViewport"))
        ->set_scientific_image(
            annotate_section(
                model_scene_->xz_slice(property, y),
                1,
                x,
                y,
                z,
                crop,
                source_index,
                receiver_indices),
            QStringLiteral("y=%1 · %2 m").arg(y).arg(
                y * model_scene_->summary().grid.dy_m));
    scientific_viewport(this, QStringLiteral("yzViewport"))
        ->set_scientific_image(
            annotate_section(
                model_scene_->yz_slice(property, x),
                2,
                x,
                y,
                z,
                crop,
                source_index,
                receiver_indices),
            QStringLiteral("x=%1 · %2 m").arg(x).arg(
                x * model_scene_->summary().grid.dx_m));
    for (const char* name : {"xyViewport", "xzViewport", "yzViewport"}) {
        findChild<QOpenGLWidget*>(QString::fromUtf8(name))
            ->setProperty("sourceMarkerVisible", source_index.has_value());
        findChild<QOpenGLWidget*>(QString::fromUtf8(name))
            ->setProperty(
                "receiverCount",
                QVariant::fromValue<qulonglong>(receiver_indices.size()));
    }
    if (source_index) {
        findChild<QOpenGLWidget*>(QStringLiteral("xyViewport"))
            ->setProperty(
                "sourceMarkerOnPlane", std::abs((*source_index)[2] - z) <= 0.5);
        findChild<QOpenGLWidget*>(QStringLiteral("xzViewport"))
            ->setProperty(
                "sourceMarkerOnPlane", std::abs((*source_index)[1] - y) <= 0.5);
        findChild<QOpenGLWidget*>(QStringLiteral("yzViewport"))
            ->setProperty(
                "sourceMarkerOnPlane", std::abs((*source_index)[0] - x) <= 0.5);
    } else {
        for (const char* name : {"xyViewport", "xzViewport", "yzViewport"}) {
            findChild<QOpenGLWidget*>(QString::fromUtf8(name))
                ->setProperty("sourceMarkerOnPlane", false);
        }
    }
#endif
}

void MainWindow::save_window_settings() {
    QSettings settings;
    settings.setValue(QStringLiteral("desktop/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("desktop/window_state"), saveState());
}

} // namespace wave3d::desktop
