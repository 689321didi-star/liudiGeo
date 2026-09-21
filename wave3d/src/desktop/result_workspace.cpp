#include "wave3d/desktop/result_workspace.hpp"

#include "wave3d/desktop/project_workspace.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wave3d::desktop {
namespace {

constexpr std::size_t kMaximumDisplayedReceivers = 512;

[[noreturn]] void fail(const QString& message) {
    throw std::invalid_argument(message.toStdString());
}

[[nodiscard]] std::uint16_t u16(const QByteArray& bytes, qsizetype offset) {
    if (offset < 0 || offset + 2 > bytes.size()) {
        fail(QStringLiteral("SEG-Y 头字段越界"));
    }
    return qFromBigEndian<std::uint16_t>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

[[nodiscard]] std::int16_t i16(const QByteArray& bytes, qsizetype offset) {
    return static_cast<std::int16_t>(u16(bytes, offset));
}

[[nodiscard]] std::uint32_t u32(const QByteArray& bytes, qsizetype offset) {
    if (offset < 0 || offset + 4 > bytes.size()) {
        fail(QStringLiteral("SEG-Y 头字段越界"));
    }
    return qFromBigEndian<std::uint32_t>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

[[nodiscard]] std::int32_t i32(const QByteArray& bytes, qsizetype offset) {
    return static_cast<std::int32_t>(u32(bytes, offset));
}

[[nodiscard]] float ieee_float(const char* bytes) {
    const auto bits = qFromBigEndian<std::uint32_t>(
        reinterpret_cast<const uchar*>(bytes));
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

[[nodiscard]] double scaled(std::int32_t value, std::int16_t scalar) {
    if (scalar == 0) {
        fail(QStringLiteral("SEG-Y 坐标比例因子不能为零"));
    }
    return scalar < 0 ? static_cast<double>(value) / std::abs(scalar)
                      : static_cast<double>(value) * scalar;
}

[[nodiscard]] bool safe_relative_path(const QString& path) {
    const auto clean = QDir::cleanPath(path);
    return !path.isEmpty() && !QDir::isAbsolutePath(path) && clean == path &&
           clean != QStringLiteral("..") &&
           !clean.startsWith(QStringLiteral("../"));
}

[[nodiscard]] QJsonObject read_json_object(const QString& path) {
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("无法读取结果记录：%1").arg(path));
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("结果记录不是有效 JSON：%1").arg(path));
    }
    return document.object();
}

struct ProductMember {
    QString component;
    QString relative_path;
    QString absolute_path;
    QString sha256;
    qint64 bytes{0};
};

struct RunEntry {
    QString id;
    QString directory;
    QString completed_utc;
    QString device;
    std::size_t receiver_count{0};
    std::size_t sample_count{0};
    double propagation_ms{0.0};
    qint64 total_bytes{0};
    std::array<ProductMember, 3> members;
};

[[nodiscard]] int component_index(const QString& component) {
    if (component == QStringLiteral("vx")) {
        return 0;
    }
    if (component == QStringLiteral("vy")) {
        return 1;
    }
    if (component == QStringLiteral("vz")) {
        return 2;
    }
    fail(QStringLiteral("未知 SEG-Y 分量：%1").arg(component));
}

[[nodiscard]] std::int16_t component_code(int component) {
    return std::array<std::int16_t, 3>{13, 14, 12}
        [static_cast<std::size_t>(component)];
}

[[nodiscard]] RunEntry parse_run(const QString& directory) {
    const auto result_path = QDir(directory).filePath(QStringLiteral("result.json"));
    const auto root = read_json_object(result_path);
    if (root.value(QStringLiteral("schema")).toString() !=
            QString::fromUtf8(kDesktopRunResultSchema) ||
        root.value(QStringLiteral("state")).toString() !=
            QStringLiteral("completed")) {
        fail(QStringLiteral("运行没有可读取的 v2 完成结果"));
    }
    const auto product = root.value(QStringLiteral("product")).toObject();
    if (product.value(QStringLiteral("kind")).toString() !=
        QStringLiteral("segy_rev1_three_component_files")) {
        fail(QStringLiteral("结果不是三文件 SEG-Y 产品"));
    }
    const auto receiver_count = product.value(QStringLiteral("receiver_count")).toInteger();
    const auto sample_count = product.value(QStringLiteral("sample_count")).toInteger();
    const auto files = product.value(QStringLiteral("files")).toArray();
    if (receiver_count <= 0 || sample_count <= 0 || files.size() != 3) {
        fail(QStringLiteral("结果产品维度或文件数无效"));
    }

    RunEntry entry;
    entry.id = QFileInfo(directory).fileName();
    entry.directory = QDir(directory).absolutePath();
    entry.completed_utc = root.value(QStringLiteral("completed_utc")).toString();
    entry.device = product.value(QStringLiteral("device_name")).toString();
    entry.receiver_count = static_cast<std::size_t>(receiver_count);
    entry.sample_count = static_cast<std::size_t>(sample_count);
    entry.propagation_ms = product.value(QStringLiteral("timings_ms"))
                               .toObject()
                               .value(QStringLiteral("propagation"))
                               .toDouble();
    std::array<bool, 3> present{};
    for (const auto value : files) {
        const auto object = value.toObject();
        const auto component = object.value(QStringLiteral("component")).toString();
        const auto index = component_index(component);
        if (present[static_cast<std::size_t>(index)]) {
            fail(QStringLiteral("结果包含重复分量"));
        }
        const auto relative = object.value(QStringLiteral("path")).toString();
        const auto bytes = object.value(QStringLiteral("bytes")).toInteger();
        const auto digest = object.value(QStringLiteral("sha256")).toString();
        const auto digest_bytes = digest.toLatin1();
        const bool valid_digest = digest_bytes.size() == 64 &&
                                  std::all_of(
                                      digest_bytes.begin(),
                                      digest_bytes.end(),
                                      [](char value) {
                                          return (value >= '0' && value <= '9') ||
                                                 (value >= 'a' && value <= 'f');
                                      });
        if (!safe_relative_path(relative) ||
            QFileInfo(relative).fileName() !=
                QStringLiteral("record_%1.sgy").arg(component) ||
            bytes <= 0 || !valid_digest) {
            fail(QStringLiteral("SEG-Y 产品成员元数据无效"));
        }
        const auto absolute = QDir(directory).absoluteFilePath(relative);
        const QFileInfo information(absolute);
        const auto canonical_run = QFileInfo(directory).canonicalFilePath();
        const auto canonical_member = information.canonicalFilePath();
        if (!information.isFile() || information.isSymLink() ||
            canonical_run.isEmpty() ||
            !canonical_member.startsWith(canonical_run + QLatin1Char('/')) ||
            information.size() != bytes) {
            fail(QStringLiteral("SEG-Y 产品成员缺失或大小已变化"));
        }
        entry.members[static_cast<std::size_t>(index)] = {
            component, relative, absolute, digest, bytes};
        entry.total_bytes += bytes;
        present[static_cast<std::size_t>(index)] = true;
    }
    if (!std::all_of(present.begin(), present.end(), [](bool value) { return value; })) {
        fail(QStringLiteral("结果缺少 Vx、Vy 或 Vz 文件"));
    }
    return entry;
}

struct TraceGeometry {
    double source_x{0.0};
    double source_y{0.0};
    double source_z{0.0};
    double receiver_x{0.0};
    double receiver_y{0.0};
    double receiver_z{0.0};
};

[[nodiscard]] TraceGeometry geometry(const QByteArray& header) {
    const auto scalel = i16(header, 68);
    const auto scalco = i16(header, 70);
    return {
        scaled(i32(header, 72), scalco),
        scaled(i32(header, 76), scalco),
        scaled(i32(header, 48), scalel),
        scaled(i32(header, 80), scalco),
        scaled(i32(header, 84), scalco),
        -scaled(i32(header, 40), scalel)};
}

[[nodiscard]] bool same_geometry(
    const TraceGeometry& left,
    const TraceGeometry& right) {
    return left.source_x == right.source_x &&
           left.source_y == right.source_y &&
           left.source_z == right.source_z &&
           left.receiver_x == right.receiver_x &&
           left.receiver_y == right.receiver_y &&
           left.receiver_z == right.receiver_z;
}

struct SegyMetadata {
    std::size_t receiver_count{0};
    std::size_t sample_count{0};
    std::uint16_t dt_us{0};
    std::size_t trace_bytes{0};
    QString textual_header;
};

[[nodiscard]] SegyMetadata inspect_member(
    const ProductMember& member,
    std::size_t expected_receivers,
    std::size_t expected_samples,
    int component) {
    QFile input(member.absolute_path);
    if (!input.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("无法打开 %1").arg(member.absolute_path));
    }
    const auto file_header = input.read(3600);
    if (file_header.size() != 3600) {
        fail(QStringLiteral("SEG-Y 文件头不完整"));
    }
    const auto sample_count = u16(file_header, 3220);
    const auto dt_us = u16(file_header, 3216);
    const auto format = u16(file_header, 3224);
    const auto revision = u16(file_header, 3500);
    const auto fixed = u16(file_header, 3502);
    const auto extended = i16(file_header, 3504);
    if (sample_count != expected_samples || dt_us == 0 || format != 5 ||
        revision != 0x0100 || fixed != 1 || extended != 0) {
        fail(QStringLiteral("SEG-Y 二进制头不符合 Wave3D Revision 1 契约"));
    }
    const auto trace_bytes = std::size_t{240} + 4 * sample_count;
    const auto expected_bytes =
        std::size_t{3600} + expected_receivers * trace_bytes;
    if (static_cast<std::size_t>(member.bytes) != expected_bytes) {
        fail(QStringLiteral("SEG-Y 固定道布局与结果记录不一致"));
    }

    SegyMetadata metadata;
    metadata.receiver_count = expected_receivers;
    metadata.sample_count = sample_count;
    metadata.dt_us = dt_us;
    metadata.trace_bytes = trace_bytes;
    metadata.textual_header = QString::fromLatin1(file_header.left(3200));
    const auto validate_trace = [&](std::size_t trace) {
        const auto offset = static_cast<qint64>(
            3600 + trace * metadata.trace_bytes);
        if (!input.seek(offset)) {
            fail(QStringLiteral("SEG-Y 道偏移无效"));
        }
        const auto trace_header = input.read(240);
        if (trace_header.size() != 240 ||
            i16(trace_header, 28) != component_code(component) ||
            u16(trace_header, 114) != sample_count ||
            u16(trace_header, 116) != dt_us) {
            fail(QStringLiteral("SEG-Y 道头的分量或采样轴不一致"));
        }
    };
    validate_trace(0);
    if (expected_receivers > 1) {
        validate_trace(expected_receivers - 1);
    }
    return metadata;
}

struct GatherData {
    std::size_t first_receiver{0};
    std::size_t receiver_count{0};
    std::size_t sample_count{0};
    std::uint16_t dt_us{0};
    QString component;
    std::vector<float> values;
    std::vector<TraceGeometry> geometry;
};

[[nodiscard]] GatherData read_gather(
    const std::array<ProductMember, 3>& members,
    const std::array<SegyMetadata, 3>& metadata,
    int selected_component,
    std::size_t first_receiver,
    std::size_t receiver_count) {
    if (receiver_count == 0 || receiver_count > kMaximumDisplayedReceivers ||
        first_receiver >= metadata[0].receiver_count ||
        receiver_count > metadata[0].receiver_count - first_receiver) {
        fail(QStringLiteral("接收器显示范围无效"));
    }
    std::array<std::unique_ptr<QFile>, 3> inputs;
    for (std::size_t component = 0; component < inputs.size(); ++component) {
        inputs[component] =
            std::make_unique<QFile>(members[component].absolute_path);
        if (!inputs[component]->open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("无法读取 SEG-Y 道集"));
        }
    }
    GatherData result;
    result.first_receiver = first_receiver;
    result.receiver_count = receiver_count;
    result.sample_count = metadata[0].sample_count;
    result.dt_us = metadata[0].dt_us;
    result.component =
        members[static_cast<std::size_t>(selected_component)].component;
    result.values.resize(receiver_count * metadata[0].sample_count);
    result.geometry.reserve(receiver_count);
    QByteArray sample_bytes(
        static_cast<qsizetype>(4 * metadata[0].sample_count), 0);
    for (std::size_t local = 0; local < receiver_count; ++local) {
        const auto receiver = first_receiver + local;
        std::optional<TraceGeometry> common_geometry;
        for (std::size_t component = 0; component < inputs.size(); ++component) {
            const auto offset = static_cast<qint64>(
                3600 + receiver * metadata[component].trace_bytes);
            if (!inputs[component]->seek(offset)) {
                fail(QStringLiteral("SEG-Y 道偏移无效"));
            }
            const auto trace_header = inputs[component]->read(240);
            if (trace_header.size() != 240 ||
                i16(trace_header, 28) !=
                    component_code(static_cast<int>(component)) ||
                u16(trace_header, 114) != metadata[component].sample_count ||
                u16(trace_header, 116) != metadata[component].dt_us) {
                fail(QStringLiteral("SEG-Y 所选道头的分量或采样轴不一致"));
            }
            const auto trace_geometry = geometry(trace_header);
            if (common_geometry &&
                !same_geometry(*common_geometry, trace_geometry)) {
                fail(QStringLiteral("三个 SEG-Y 文件的所选炮检几何不一致"));
            }
            common_geometry = trace_geometry;
            if (static_cast<int>(component) == selected_component &&
                inputs[component]->read(
                    sample_bytes.data(), sample_bytes.size()) !=
                    sample_bytes.size()) {
                fail(QStringLiteral("SEG-Y 道样点读取失败"));
            }
        }
        result.geometry.push_back(*common_geometry);
        for (std::size_t sample = 0; sample < metadata[0].sample_count; ++sample) {
            const auto value = ieee_float(sample_bytes.constData() + 4 * sample);
            if (!std::isfinite(value)) {
                fail(QStringLiteral("SEG-Y 包含非有限样点"));
            }
            result.values[local * metadata[0].sample_count + sample] = value;
        }
    }
    return result;
}

[[nodiscard]] QColor sample_color(float normalized) {
    const auto value = std::clamp(normalized, -1.0F, 1.0F);
    if (value < 0.0F) {
        const auto weight = -value;
        return QColor(
            static_cast<int>(247 + (33 - 247) * weight),
            static_cast<int>(247 + (102 - 247) * weight),
            static_cast<int>(247 + (172 - 247) * weight));
    }
    return QColor(
        static_cast<int>(247 + (178 - 247) * value),
        static_cast<int>(247 + (24 - 247) * value),
        static_cast<int>(247 + (43 - 247) * value));
}

[[nodiscard]] QImage render_gather(const GatherData& gather, float* clip_out) {
    std::vector<float> absolute;
    absolute.reserve(gather.values.size());
    for (const auto value : gather.values) {
        absolute.push_back(std::abs(value));
    }
    const auto clip_index = static_cast<std::size_t>(
        std::floor(0.995 * static_cast<double>(absolute.size() - 1)));
    std::nth_element(
        absolute.begin(), absolute.begin() + clip_index, absolute.end());
    auto clip = absolute[clip_index];
    if (!(clip > 0.0F)) {
        clip = *std::max_element(absolute.begin(), absolute.end());
    }
    *clip_out = clip;

    constexpr int width = 1200;
    constexpr int height = 760;
    constexpr int left = 88;
    constexpr int right = 28;
    constexpr int top = 54;
    constexpr int bottom = 66;
    const int plot_width = width - left - right;
    const int plot_height = height - top - bottom;
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(QColor(12, 24, 34));
    for (int y = 0; y < plot_height; ++y) {
        const auto sample = std::min(
            gather.sample_count - 1,
            static_cast<std::size_t>(
                static_cast<double>(y) * gather.sample_count / plot_height));
        for (int x = 0; x < plot_width; ++x) {
            const auto trace = std::min(
                gather.receiver_count - 1,
                static_cast<std::size_t>(
                    static_cast<double>(x) * gather.receiver_count / plot_width));
            image.setPixelColor(
                left + x,
                top + y,
                sample_color(
                    clip > 0.0F
                        ? gather.values[trace * gather.sample_count + sample] /
                              clip
                        : 0.0F));
        }
    }

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(135, 166, 182), 1));
    painter.drawRect(left, top, plot_width, plot_height);
    painter.setPen(QColor(220, 234, 241));
    painter.setFont(QFont(QStringLiteral("Sans Serif"), 12, QFont::DemiBold));
    painter.drawText(
        QRect(left, 10, plot_width, 30),
        Qt::AlignCenter,
        QStringLiteral("%1 粒子速度道集 · 显示裁剪 P99.5 = ±%2 m/s")
            .arg(gather.component.toUpper())
            .arg(clip, 0, 'g', 6));
    painter.setFont(QFont(QStringLiteral("Sans Serif"), 9));
    painter.drawText(
        QRect(left, height - 45, plot_width, 24),
        Qt::AlignCenter,
        QStringLiteral("接收器索引 %1 — %2")
            .arg(gather.first_receiver)
            .arg(gather.first_receiver + gather.receiver_count - 1));
    const auto end_time =
        gather.sample_count * static_cast<double>(gather.dt_us) * 1.0e-6;
    painter.drawText(8, top + 5, QStringLiteral("%1 s").arg(
        gather.dt_us * 1.0e-6, 0, 'g', 6));
    painter.drawText(8, top + plot_height, QStringLiteral("%1 s").arg(
        end_time, 0, 'g', 6));
    painter.save();
    painter.translate(22, top + plot_height / 2);
    painter.rotate(-90.0);
    painter.drawText(
        QRect(-plot_height / 2, -18, plot_height, 24),
        Qt::AlignCenter,
        QStringLiteral("时间 / s"));
    painter.restore();
    return image;
}

[[nodiscard]] QString header_report(
    const RunEntry& run,
    const SegyMetadata& metadata,
    const GatherData& gather,
    int component,
    std::size_t first,
    std::size_t count) {
    const auto& first_geometry = gather.geometry.front();
    const auto& last_geometry = gather.geometry.back();
    QString report = QStringLiteral(
        "运行：%1\n分量：%2（Trace identification code %3）\n"
        "接收器：%4；采样：%5；dt：%6 μs；记录时长：%7 s\n"
        "数据格式：SEG-Y Revision 1 · big-endian IEEE float32 · 固定长度道\n"
        "震源 XYZ：(%8, %9, %10) m\n"
        "首个接收器 #%11 XYZ：(%12, %13, %14) m\n"
        "末个接收器 #%15 XYZ：(%16, %17, %18) m\n\n"
        "3200 字节文本头：\n")
                         .arg(run.id)
                         .arg(run.members[static_cast<std::size_t>(component)]
                                  .component.toUpper())
                         .arg(component_code(component))
                         .arg(metadata.receiver_count)
                         .arg(metadata.sample_count)
                         .arg(metadata.dt_us)
                         .arg(metadata.sample_count * metadata.dt_us * 1.0e-6, 0, 'g', 8)
                         .arg(first_geometry.source_x, 0, 'g', 10)
                         .arg(first_geometry.source_y, 0, 'g', 10)
                         .arg(first_geometry.source_z, 0, 'g', 10)
                         .arg(first)
                         .arg(first_geometry.receiver_x, 0, 'g', 10)
                         .arg(first_geometry.receiver_y, 0, 'g', 10)
                         .arg(first_geometry.receiver_z, 0, 'g', 10)
                         .arg(first + count - 1)
                         .arg(last_geometry.receiver_x, 0, 'g', 10)
                         .arg(last_geometry.receiver_y, 0, 'g', 10)
                         .arg(last_geometry.receiver_z, 0, 'g', 10);
    for (int card = 0; card < 40; ++card) {
        report += metadata.textual_header.mid(card * 80, 80).trimmed() + QLatin1Char('\n');
    }
    return report;
}

class GatherCanvas final : public QWidget {
public:
    explicit GatherCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("resultGatherCanvas"));
        setMinimumSize(420, 60);
    }

    void set_image(QImage image) {
        image_ = std::move(image);
        setProperty("hasGather", !image_.isNull());
        update();
    }

    [[nodiscard]] const QImage& image() const noexcept { return image_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(9, 20, 29));
        if (image_.isNull()) {
            painter.setPen(QColor(132, 167, 184));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("选择完成运行以查看 SEG-Y 道集"));
            return;
        }
        const auto target_size = image_.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect target(
            rect().center() - QPoint(target_size.width() / 2, target_size.height() / 2),
            target_size);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, image_);
    }

private:
    QImage image_;
};

} // namespace

class ResultWorkspace::Impl final {
public:
    explicit Impl(ResultWorkspace* owner) : owner_(owner) {
        auto* layout = new QVBoxLayout(owner_);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(8);

        auto* title = new QLabel(QStringLiteral("正演结果与 SEG-Y 检查"), owner_);
        title->setObjectName(QStringLiteral("resultsWorkspaceTitle"));
        layout->addWidget(title);

        runs_ = new QTableWidget(owner_);
        runs_->setObjectName(QStringLiteral("resultRunTable"));
        runs_->setColumnCount(6);
        runs_->setHorizontalHeaderLabels({
            QStringLiteral("运行"), QStringLiteral("完成时间"),
            QStringLiteral("道×采样"), QStringLiteral("设备"),
            QStringLiteral("传播 / ms"), QStringLiteral("SEG-Y 合计")});
        runs_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        runs_->horizontalHeader()->setStretchLastSection(true);
        runs_->setSelectionBehavior(QAbstractItemView::SelectRows);
        runs_->setSelectionMode(QAbstractItemView::SingleSelection);
        runs_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        runs_->setMaximumHeight(185);
        layout->addWidget(runs_);

        auto* controls = new QHBoxLayout;
        component_ = new QComboBox(owner_);
        component_->setObjectName(QStringLiteral("resultComponentSelector"));
        component_->addItem(QStringLiteral("Vx / inline"), QStringLiteral("vx"));
        component_->addItem(QStringLiteral("Vy / crossline"), QStringLiteral("vy"));
        component_->addItem(QStringLiteral("Vz / vertical"), QStringLiteral("vz"));
        first_ = new QSpinBox(owner_);
        first_->setObjectName(QStringLiteral("resultFirstReceiverSpin"));
        first_->setPrefix(QStringLiteral("起始 #"));
        count_ = new QSpinBox(owner_);
        count_->setObjectName(QStringLiteral("resultReceiverCountSpin"));
        count_->setPrefix(QStringLiteral("数量 "));
        count_->setRange(1, static_cast<int>(kMaximumDisplayedReceivers));
        load_ = new QPushButton(QStringLiteral("读取道集"), owner_);
        load_->setObjectName(QStringLiteral("loadResultGatherButton"));
        export_ = new QPushButton(QStringLiteral("导出 PNG"), owner_);
        export_->setObjectName(QStringLiteral("exportResultPngButton"));
        controls->addWidget(component_);
        controls->addWidget(first_);
        controls->addWidget(count_);
        controls->addWidget(load_);
        controls->addWidget(export_);
        controls->addStretch();
        layout->addLayout(controls);

        auto* splitter = new QSplitter(Qt::Horizontal, owner_);
        canvas_ = new GatherCanvas(splitter);
        header_ = new QPlainTextEdit(splitter);
        header_->setObjectName(QStringLiteral("resultHeaderReport"));
        header_->setReadOnly(true);
        header_->setMinimumWidth(330);
        header_->setLineWrapMode(QPlainTextEdit::NoWrap);
        splitter->addWidget(canvas_);
        splitter->addWidget(header_);
        splitter->setStretchFactor(0, 3);
        splitter->setStretchFactor(1, 2);
        layout->addWidget(splitter, 1);

        status_ = new QLabel(QStringLiteral("尚未打开项目"), owner_);
        status_->setObjectName(QStringLiteral("resultWorkspaceStatus"));
        status_->setWordWrap(true);
        layout->addWidget(status_);

        QObject::connect(runs_, &QTableWidget::cellClicked, owner_, [this](int row, int) {
            if (row >= 0 && row < runs_->rowCount()) {
                QString ignored;
                static_cast<void>(select_run(runs_->item(row, 0)->text(), &ignored));
            }
        });
        QObject::connect(load_, &QPushButton::clicked, owner_, [this] {
            QString error;
            if (!load_selection(&error)) {
                status_->setText(error);
            }
        });
        QObject::connect(
            component_, &QComboBox::currentIndexChanged, owner_, [this](int) {
                if (active_entry_) {
                    QString ignored;
                    static_cast<void>(load_selection(&ignored));
                }
            });
        QObject::connect(export_, &QPushButton::clicked, owner_, [this] {
            const auto path = QFileDialog::getSaveFileName(
                owner_, QStringLiteral("导出当前道集"),
                selected_run_.isEmpty() ? QStringLiteral("gather.png")
                                        : selected_run_ + QStringLiteral("_gather.png"),
                QStringLiteral("PNG 图像 (*.png)"));
            if (path.isEmpty()) {
                return;
            }
            QString error;
            if (!export_png(path, &error)) {
                status_->setText(error);
            }
        });
    }

    void set_available(bool available) {
        available_ = available;
        runs_->setEnabled(available);
        component_->setEnabled(available);
        first_->setEnabled(available);
        count_->setEnabled(available);
        load_->setEnabled(available);
        export_->setEnabled(available && !canvas_->image().isNull());
        if (!available) {
            status_->setText(QStringLiteral("当前构建未启用 SEG-Y 结果查看"));
        }
    }

    void refresh(QString project_root) {
        project_root_ = std::move(project_root);
        entries_.clear();
        selected_run_.clear();
        canvas_->set_image({});
        header_->clear();
        runs_->setRowCount(0);
        if (project_root_.isEmpty()) {
            status_->setText(QStringLiteral("尚未打开项目"));
            return;
        }
        const QDir runs_directory(QDir(project_root_).filePath(QStringLiteral("runs")));
        const auto run_ids = runs_directory.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        QStringList rejected;
        for (const auto& run_id : run_ids) {
            const auto directory = runs_directory.filePath(run_id);
            if (!QFileInfo::exists(QDir(directory).filePath(QStringLiteral("result.json")))) {
                continue;
            }
            try {
                const auto root = read_json_object(
                    QDir(directory).filePath(QStringLiteral("result.json")));
                if (root.value(QStringLiteral("state")).toString() !=
                    QStringLiteral("completed")) {
                    continue;
                }
                entries_.push_back(parse_run(directory));
            } catch (const std::exception&) {
                rejected.push_back(run_id);
            }
        }
        std::sort(entries_.begin(), entries_.end(), [](const auto& left, const auto& right) {
            return left.completed_utc > right.completed_utc;
        });
        runs_->setRowCount(static_cast<int>(entries_.size()));
        owner_->setProperty(
            "completedResultRunCount", static_cast<int>(entries_.size()));
        for (int row = 0; row < runs_->rowCount(); ++row) {
            const auto& entry = entries_[static_cast<std::size_t>(row)];
            const std::array values{
                entry.id,
                entry.completed_utc,
                QStringLiteral("%1 × %2").arg(entry.receiver_count).arg(entry.sample_count),
                entry.device,
                QString::number(entry.propagation_ms, 'f', 2),
                QStringLiteral("%1 MiB").arg(
                    entry.total_bytes / (1024.0 * 1024.0), 0, 'f', 2)};
            for (int column = 0; column < runs_->columnCount(); ++column) {
                runs_->setItem(row, column, new QTableWidgetItem(
                    values[static_cast<std::size_t>(column)]));
            }
        }
        const auto discovery = QStringLiteral("发现 %1 个完成运行%2")
                                   .arg(entries_.size())
                                   .arg(rejected.isEmpty()
                                            ? QString()
                                            : QStringLiteral("；忽略无效结果：%1")
                                                  .arg(rejected.join(
                                                      QStringLiteral(", "))));
        status_->setText(
            available_
                ? discovery
                : QStringLiteral("当前构建未启用 SEG-Y 结果查看；%1")
                      .arg(discovery));
        if (available_ && !entries_.empty()) {
            QString ignored;
            static_cast<void>(select_run(entries_.front().id, &ignored));
        }
    }

    bool select_run(const QString& run_id, QString* error_message) {
        try {
            const auto iterator = std::find_if(
                entries_.begin(), entries_.end(), [&](const auto& entry) {
                    return entry.id == run_id;
                });
            if (iterator == entries_.end()) {
                fail(QStringLiteral("项目中没有完成运行：%1").arg(run_id));
            }
            std::array<SegyMetadata, 3> metadata;
            metadata[0] = inspect_member(
                iterator->members[0], iterator->receiver_count,
                iterator->sample_count, 0);
            metadata[1] = inspect_member(
                iterator->members[1], iterator->receiver_count,
                iterator->sample_count, 1);
            metadata[2] = inspect_member(
                iterator->members[2], iterator->receiver_count,
                iterator->sample_count, 2);
            if (metadata[0].dt_us != metadata[1].dt_us ||
                metadata[0].dt_us != metadata[2].dt_us) {
                fail(QStringLiteral("三个 SEG-Y 文件的采样间隔不一致"));
            }
            active_entry_ = *iterator;
            metadata_ = std::move(metadata);
            selected_run_ = run_id;
            first_->setRange(0, static_cast<int>(iterator->receiver_count - 1));
            first_->setValue(0);
            count_->setMaximum(static_cast<int>(std::min(
                iterator->receiver_count, kMaximumDisplayedReceivers)));
            count_->setValue(static_cast<int>(std::min(
                iterator->receiver_count, std::size_t{101})));
            for (int row = 0; row < runs_->rowCount(); ++row) {
                if (runs_->item(row, 0)->text() == run_id) {
                    runs_->selectRow(row);
                    break;
                }
            }
            return load_selection(error_message);
        } catch (const std::exception& error) {
            if (error_message != nullptr) {
                *error_message = QString::fromUtf8(error.what());
            }
            return false;
        }
    }

    bool load_selection(QString* error_message) {
        try {
            if (!available_) {
                fail(QStringLiteral("当前构建未启用 SEG-Y 结果查看"));
            }
            if (!active_entry_) {
                fail(QStringLiteral("尚未选择完成运行"));
            }
            const auto component = component_->currentIndex();
            const auto first = static_cast<std::size_t>(first_->value());
            const auto count = static_cast<std::size_t>(count_->value());
            if (count > active_entry_->receiver_count - first) {
                fail(QStringLiteral("接收器范围超过记录末尾"));
            }
            const auto gather = read_gather(
                active_entry_->members, metadata_, component, first, count);
            float clip = 0.0F;
            canvas_->set_image(render_gather(gather, &clip));
            header_->setPlainText(header_report(
                *active_entry_, metadata_[static_cast<std::size_t>(component)],
                gather, component, first, count));
            export_->setEnabled(true);
            status_->setText(
                QStringLiteral("已读取 %1 · %2 · 接收器 %3—%4 · P99.5 ±%5 m/s")
                    .arg(active_entry_->id)
                    .arg(component_->currentData().toString().toUpper())
                    .arg(first)
                    .arg(first + count - 1)
                    .arg(clip, 0, 'g', 6));
            owner_->setProperty("selectedResultRun", active_entry_->id);
            owner_->setProperty("resultGatherReceiverCount", static_cast<qulonglong>(count));
            owner_->setProperty("resultGatherComponent", component_->currentData());
            return true;
        } catch (const std::exception& error) {
            if (error_message != nullptr) {
                *error_message = QString::fromUtf8(error.what());
            }
            return false;
        }
    }

    bool set_component(const QString& component, QString* error_message) {
        const auto index = component_->findData(component);
        if (index < 0) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("未知结果分量：%1").arg(component);
            }
            return false;
        }
        const QSignalBlocker blocker(component_);
        component_->setCurrentIndex(index);
        return load_selection(error_message);
    }

    bool set_receiver_window(
        std::size_t first,
        std::size_t count,
        QString* error_message) {
        if (first > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
            count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("接收器范围超过桌面控件限制");
            }
            return false;
        }
        first_->setValue(static_cast<int>(first));
        count_->setValue(static_cast<int>(count));
        if (static_cast<std::size_t>(first_->value()) != first ||
            static_cast<std::size_t>(count_->value()) != count) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("接收器范围无效或超过 512 道显示上限");
            }
            return false;
        }
        return load_selection(error_message);
    }

    bool export_png(const QString& path, QString* error_message) const {
        if (canvas_->image().isNull()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("当前没有可导出的道集图像");
            }
            return false;
        }
        if (QFileInfo(path).suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("结果截图必须使用 .png 扩展名");
            }
            return false;
        }
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) ||
            !canvas_->image().save(&output, "PNG") || !output.commit()) {
            if (error_message != nullptr) {
                *error_message = QStringLiteral("无法写入 PNG：%1").arg(path);
            }
            return false;
        }
        return true;
    }

    ResultWorkspace* owner_{nullptr};
    QString project_root_;
    bool available_{true};
    std::vector<RunEntry> entries_;
    std::optional<RunEntry> active_entry_;
    std::array<SegyMetadata, 3> metadata_;
    QString selected_run_;
    QTableWidget* runs_{nullptr};
    QComboBox* component_{nullptr};
    QSpinBox* first_{nullptr};
    QSpinBox* count_{nullptr};
    QPushButton* load_{nullptr};
    QPushButton* export_{nullptr};
    GatherCanvas* canvas_{nullptr};
    QPlainTextEdit* header_{nullptr};
    QLabel* status_{nullptr};
};

ResultWorkspace::ResultWorkspace(QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(this)) {
    setObjectName(QStringLiteral("resultWorkspace"));
}

ResultWorkspace::~ResultWorkspace() = default;

void ResultWorkspace::set_project_root(const QString& project_root) {
    impl_->refresh(project_root);
}

void ResultWorkspace::set_segy_available(bool available) {
    impl_->set_available(available);
}

int ResultWorkspace::completed_run_count() const noexcept {
    return static_cast<int>(impl_->entries_.size());
}

QString ResultWorkspace::selected_run_id() const {
    return impl_->selected_run_;
}

QImage ResultWorkspace::current_gather_image() const {
    return impl_->canvas_->image();
}

QString ResultWorkspace::current_header_report() const {
    return impl_->header_->toPlainText();
}

bool ResultWorkspace::select_run(
    const QString& run_id,
    QString* error_message) {
    return impl_->select_run(run_id, error_message);
}

bool ResultWorkspace::set_component(
    const QString& component,
    QString* error_message) {
    return impl_->set_component(component, error_message);
}

bool ResultWorkspace::set_receiver_window(
    std::size_t first_receiver,
    std::size_t receiver_count,
    QString* error_message) {
    return impl_->set_receiver_window(
        first_receiver, receiver_count, error_message);
}

bool ResultWorkspace::export_current_png(
    const QString& path,
    QString* error_message) const {
    return impl_->export_png(path, error_message);
}

} // namespace wave3d::desktop
