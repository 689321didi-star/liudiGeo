#include "wave3d/desktop/experiment_editor.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace wave3d::desktop {
namespace {

QDoubleSpinBox* double_spin(
    const QString& object_name,
    double minimum,
    double maximum,
    int decimals,
    const QString& suffix,
    QWidget* parent) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setObjectName(object_name);
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setKeyboardTracking(false);
    spin->setSuffix(suffix);
    return spin;
}

QLineEdit* scientific_edit(
    const QString& object_name,
    QWidget* parent) {
    auto* edit = new QLineEdit(parent);
    edit->setObjectName(object_name);
    edit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral(
            "^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$")),
        edit));
    edit->setAlignment(Qt::AlignRight);
    edit->setMinimumWidth(78);
    return edit;
}

double scientific_value(const QLineEdit* edit) {
    bool valid = false;
    const auto value = edit->text().toDouble(&valid);
    if (!valid || !std::isfinite(value)) {
        throw std::invalid_argument("moment value is not a finite number");
    }
    return value;
}

QString scientific_text(double value) {
    return QString::number(value, 'g', 15);
}

QString byte_text(std::size_t bytes) {
    constexpr double mib = 1024.0 * 1024.0;
    constexpr double gib = 1024.0 * mib;
    if (bytes >= static_cast<std::size_t>(gib)) {
        return QStringLiteral("%1 GiB").arg(bytes / gib, 0, 'f', 2);
    }
    return QStringLiteral("%1 MiB").arg(bytes / mib, 0, 'f', 2);
}

} // namespace

ExperimentEditor::ExperimentEditor(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("experimentEditor"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto* identity = new QGroupBox(QStringLiteral("当前实验草稿"), this);
    auto* identity_form = new QFormLayout(identity);
    auto* shot = new QLabel(QStringLiteral("—"), identity);
    shot->setObjectName(QStringLiteral("experimentShotLabel"));
    auto* state = new QLabel(QStringLiteral("请先加载模型"), identity);
    state->setObjectName(QStringLiteral("experimentSaveStateLabel"));
    state->setWordWrap(true);
    identity_form->addRow(QStringLiteral("炮号："), shot);
    identity_form->addRow(QStringLiteral("状态："), state);
    layout->addWidget(identity);

    auto* workspace = new QGroupBox(QStringLiteral("工作区与数值参数"), this);
    workspace->setObjectName(QStringLiteral("workspaceEditorGroup"));
    auto* workspace_form = new QFormLayout(workspace);
    auto* grid = new QLabel(QStringLiteral("—"), workspace);
    grid->setObjectName(QStringLiteral("workspaceGridSummaryLabel"));
    grid->setWordWrap(true);
    auto* storage = new QLabel(QStringLiteral("—"), workspace);
    storage->setObjectName(QStringLiteral("workspaceStorageSummaryLabel"));
    storage->setWordWrap(true);
    workspace_form->addRow(QStringLiteral("模型网格："), grid);
    workspace_form->addRow(QStringLiteral("存储边界："), storage);
    workspace_form->addRow(
        QStringLiteral("时间步长："),
        double_spin(
            QStringLiteral("timeStepMsSpin"),
            0.000001,
            1000.0,
            6,
            QStringLiteral(" ms"),
            workspace));
    workspace_form->addRow(
        QStringLiteral("模拟时长："),
        double_spin(
            QStringLiteral("totalTimeSpin"),
            0.000001,
            10000.0,
            6,
            QStringLiteral(" s"),
            workspace));
    workspace_form->addRow(
        QStringLiteral("CFL 安全系数："),
        double_spin(
            QStringLiteral("cflSafetySpin"),
            0.001,
            0.999,
            3,
            QString(),
            workspace));
    workspace_form->addRow(
        QStringLiteral("设计频率："),
        double_spin(
            QStringLiteral("designFrequencySpin"),
            0.001,
            10000.0,
            3,
            QStringLiteral(" Hz"),
            workspace));
    layout->addWidget(workspace);

    auto* source = new QGroupBox(QStringLiteral("震源"), this);
    source->setObjectName(QStringLiteral("sourceEditorGroup"));
    auto* source_layout = new QVBoxLayout(source);
    auto* source_form = new QFormLayout;
    auto* mode = new QComboBox(source);
    mode->setObjectName(QStringLiteral("sourceModeCombo"));
    mode->addItem(
        QStringLiteral("各向同性爆炸源"),
        static_cast<int>(DraftSourceMode::IsotropicExplosion));
    mode->addItem(
        QStringLiteral("手工对称矩张量"),
        static_cast<int>(DraftSourceMode::MomentTensor));
    mode->addItem(
        QStringLiteral("双力偶（走向/倾角/滑动角）"),
        static_cast<int>(DraftSourceMode::DoubleCouple));
    source_form->addRow(QStringLiteral("类型："), mode);
    for (const auto& specification : std::array{
             std::tuple{QStringLiteral("X："), QStringLiteral("sourceXSpin")},
             std::tuple{QStringLiteral("Y："), QStringLiteral("sourceYSpin")},
             std::tuple{QStringLiteral("Z（向下）："), QStringLiteral("sourceZSpin")}}) {
        source_form->addRow(
            std::get<0>(specification),
            double_spin(
                std::get<1>(specification),
                -1.0e12,
                1.0e12,
                3,
                QStringLiteral(" m"),
                source));
    }
    source_form->addRow(
        QStringLiteral("起始时间："),
        double_spin(
            QStringLiteral("sourceOriginTimeSpin"),
            0.0,
            10000.0,
            6,
            QStringLiteral(" s"),
            source));
    source_form->addRow(
        QStringLiteral("Ricker 主频："),
        double_spin(
            QStringLiteral("sourceFrequencySpin"),
            0.001,
            10000.0,
            3,
            QStringLiteral(" Hz"),
            source));
    source_form->addRow(
        QStringLiteral("峰值延迟："),
        double_spin(
            QStringLiteral("sourcePeakDelaySpin"),
            0.0,
            10000.0,
            6,
            QStringLiteral(" s"),
            source));
    source_form->addRow(
        QStringLiteral("峰值率 (s⁻¹)："),
        scientific_edit(QStringLiteral("sourcePeakRateEdit"), source));
    source_layout->addLayout(source_form);

    auto* source_pages = new QStackedWidget(source);
    source_pages->setObjectName(QStringLiteral("sourceParameterStack"));
    auto* explosion = new QWidget(source_pages);
    auto* explosion_form = new QFormLayout(explosion);
    explosion_form->addRow(
        QStringLiteral("标量矩 (N·m)："),
        scientific_edit(QStringLiteral("explosionMomentEdit"), explosion));
    source_pages->addWidget(explosion);
    auto* tensor = new QWidget(source_pages);
    tensor->setToolTip(QStringLiteral("矩张量分量单位：N·m"));
    auto* tensor_grid = new QGridLayout(tensor);
    const std::array<const char*, 6> tensor_names{
        "momentMxxEdit", "momentMyyEdit", "momentMzzEdit",
        "momentMxyEdit", "momentMxzEdit", "momentMyzEdit"};
    const std::array<const char*, 6> tensor_labels{
        "Mxx", "Myy", "Mzz", "Mxy", "Mxz", "Myz"};
    for (int index = 0; index < 6; ++index) {
        tensor_grid->addWidget(
            new QLabel(QString::fromUtf8(tensor_labels[index]), tensor),
            index / 2,
            (index % 2) * 2);
        tensor_grid->addWidget(
            scientific_edit(QString::fromUtf8(tensor_names[index]), tensor),
            index / 2,
            (index % 2) * 2 + 1);
    }
    source_pages->addWidget(tensor);
    auto* double_couple = new QWidget(source_pages);
    auto* double_couple_form = new QFormLayout(double_couple);
    double_couple_form->addRow(
        QStringLiteral("标量矩 M₀ (N·m)："),
        scientific_edit(
            QStringLiteral("doubleCoupleMomentEdit"), double_couple));
    double_couple_form->addRow(
        QStringLiteral("走向："),
        double_spin(
            QStringLiteral("doubleCoupleStrikeSpin"),
            0.0,
            359.999,
            3,
            QStringLiteral("°"),
            double_couple));
    double_couple_form->addRow(
        QStringLiteral("倾角："),
        double_spin(
            QStringLiteral("doubleCoupleDipSpin"),
            0.0,
            90.0,
            3,
            QStringLiteral("°"),
            double_couple));
    double_couple_form->addRow(
        QStringLiteral("滑动角："),
        double_spin(
            QStringLiteral("doubleCoupleRakeSpin"),
            -180.0,
            180.0,
            3,
            QStringLiteral("°"),
            double_couple));
    auto* convention = new QLabel(
        QStringLiteral(
            "X 东 / Y 北 / Z 下；走向自北顺时针，滑动角从走向朝下倾方向"),
        double_couple);
    convention->setWordWrap(true);
    double_couple_form->addRow(QStringLiteral("约定："), convention);
    auto* resolved_tensor = new QLabel(QStringLiteral("等待有效参数"), double_couple);
    resolved_tensor->setObjectName(QStringLiteral("doubleCoupleTensorLabel"));
    resolved_tensor->setWordWrap(true);
    resolved_tensor->setTextInteractionFlags(Qt::TextSelectableByMouse);
    double_couple_form->addRow(QStringLiteral("转换结果："), resolved_tensor);
    source_pages->addWidget(double_couple);
    source_layout->addWidget(source_pages);
    layout->addWidget(source);

    auto* acquisition = new QGroupBox(QStringLiteral("观测系统"), this);
    acquisition->setObjectName(QStringLiteral("acquisitionEditorGroup"));
    auto* acquisition_layout = new QVBoxLayout(acquisition);
    auto* acquisition_form = new QFormLayout;
    auto* geometry = new QComboBox(acquisition);
    geometry->setObjectName(QStringLiteral("receiverGeometryCombo"));
    geometry->addItem(
        QStringLiteral("规则矩形面阵"),
        static_cast<int>(ReceiverGeometryMode::SurfaceRectangular));
    geometry->addItem(
        QStringLiteral("接收器测线"),
        static_cast<int>(ReceiverGeometryMode::SurfaceLine));
    geometry->addItem(
        QStringLiteral("CSV 显式坐标"),
        static_cast<int>(ReceiverGeometryMode::ExplicitCoordinates));
    acquisition_form->addRow(QStringLiteral("几何类型："), geometry);
    acquisition_layout->addLayout(acquisition_form);

    auto* receiver_pages = new QStackedWidget(acquisition);
    receiver_pages->setObjectName(QStringLiteral("receiverGeometryStack"));
    auto* rectangular_page = new QWidget(receiver_pages);
    auto* rectangular_form = new QFormLayout(rectangular_page);
    for (const auto& specification : std::array{
             std::pair{QStringLiteral("X 方向数量："),
                       QStringLiteral("receiverCountXSpin")},
             std::pair{QStringLiteral("Y 方向数量："),
                       QStringLiteral("receiverCountYSpin")}}) {
        auto* spin = new QSpinBox(rectangular_page);
        spin->setObjectName(specification.second);
        spin->setRange(2, 1001);
        spin->setKeyboardTracking(false);
        rectangular_form->addRow(specification.first, spin);
    }
    for (const auto& specification : std::array{
             std::pair{QStringLiteral("X 起点："),
                       QStringLiteral("receiverMinXSpin")},
             std::pair{QStringLiteral("X 终点："),
                       QStringLiteral("receiverMaxXSpin")},
             std::pair{QStringLiteral("Y 起点："),
                       QStringLiteral("receiverMinYSpin")},
             std::pair{QStringLiteral("Y 终点："),
                       QStringLiteral("receiverMaxYSpin")}}) {
        rectangular_form->addRow(
            specification.first,
            double_spin(
                specification.second,
                0.0,
                1.0e12,
                3,
                QStringLiteral(" m"),
                rectangular_page));
    }
    receiver_pages->addWidget(rectangular_page);

    auto* line_page = new QWidget(receiver_pages);
    auto* line_form = new QFormLayout(line_page);
    auto* line_count = new QSpinBox(line_page);
    line_count->setObjectName(QStringLiteral("receiverLineCountSpin"));
    line_count->setRange(2, 1'100'000);
    line_count->setKeyboardTracking(false);
    line_form->addRow(QStringLiteral("接收点数量："), line_count);
    for (const auto& specification : std::array{
             std::pair{QStringLiteral("起点 X："),
                       QStringLiteral("receiverLineFirstXSpin")},
             std::pair{QStringLiteral("起点 Y："),
                       QStringLiteral("receiverLineFirstYSpin")},
             std::pair{QStringLiteral("终点 X："),
                       QStringLiteral("receiverLineLastXSpin")},
             std::pair{QStringLiteral("终点 Y："),
                       QStringLiteral("receiverLineLastYSpin")}}) {
        line_form->addRow(
            specification.first,
            double_spin(
                specification.second,
                -1.0e12,
                1.0e12,
                3,
                QStringLiteral(" m"),
                line_page));
    }
    receiver_pages->addWidget(line_page);

    auto* explicit_page = new QWidget(receiver_pages);
    auto* explicit_layout = new QVBoxLayout(explicit_page);
    auto* explicit_count = new QLabel(QStringLiteral("尚未导入坐标"), explicit_page);
    explicit_count->setObjectName(QStringLiteral("explicitReceiverCountLabel"));
    explicit_layout->addWidget(explicit_count);
    auto* import_csv = new QPushButton(QStringLiteral("导入 CSV…"), explicit_page);
    import_csv->setObjectName(QStringLiteral("importReceiverCsvButton"));
    explicit_layout->addWidget(import_csv);
    receiver_pages->addWidget(explicit_page);
    acquisition_layout->addWidget(receiver_pages);

    auto* common_form = new QFormLayout;
    common_form->addRow(
        QStringLiteral("整体平移 X："),
        double_spin(
            QStringLiteral("receiverTranslateXSpin"),
            -1.0e12,
            1.0e12,
            3,
            QStringLiteral(" m"),
            acquisition));
    common_form->addRow(
        QStringLiteral("整体平移 Y："),
        double_spin(
            QStringLiteral("receiverTranslateYSpin"),
            -1.0e12,
            1.0e12,
            3,
            QStringLiteral(" m"),
            acquisition));
    auto* receiver_depth = double_spin(
        QStringLiteral("receiverDepthSpin"),
        0.0,
        0.0,
        3,
        QStringLiteral(" m"),
        acquisition);
    receiver_depth->setToolTip(QStringLiteral("当前求解范围固定为自由表面接收"));
    common_form->addRow(QStringLiteral("接收深度："), receiver_depth);
    auto* components = new QLabel(QStringLiteral("Vx / Vy / Vz"), acquisition);
    components->setObjectName(QStringLiteral("receiverComponentsLabel"));
    common_form->addRow(QStringLiteral("记录分量："), components);
    auto* estimate = new QLabel(QStringLiteral("等待模型"), acquisition);
    estimate->setObjectName(QStringLiteral("acquisitionEstimateLabel"));
    estimate->setWordWrap(true);
    common_form->addRow(QStringLiteral("规模预估："), estimate);
    acquisition_layout->addLayout(common_form);
    auto* template_actions = new QHBoxLayout;
    auto* load_template = new QPushButton(QStringLiteral("载入模板…"), acquisition);
    load_template->setObjectName(QStringLiteral("loadAcquisitionTemplateButton"));
    auto* save_template = new QPushButton(QStringLiteral("保存模板…"), acquisition);
    save_template->setObjectName(QStringLiteral("saveAcquisitionTemplateButton"));
    template_actions->addWidget(load_template);
    template_actions->addWidget(save_template);
    acquisition_layout->addLayout(template_actions);
    layout->addWidget(acquisition);

    auto* validation = new QLabel(QStringLiteral("等待模型"), this);
    validation->setObjectName(QStringLiteral("experimentValidationLabel"));
    validation->setWordWrap(true);
    validation->setMinimumHeight(66);
    layout->addWidget(validation);
    auto* actions = new QHBoxLayout;
    actions->addStretch();
    auto* save = new QPushButton(QStringLiteral("保存实验草稿"), this);
    save->setObjectName(QStringLiteral("saveExperimentDraftButton"));
    save->setEnabled(false);
    actions->addWidget(save);
    layout->addLayout(actions);
    layout->addStretch();

    const auto publish = [this] { publish_change(); };
    for (auto* spin : findChildren<QDoubleSpinBox*>()) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, publish);
    }
    for (auto* spin : findChildren<QSpinBox*>()) {
        connect(spin, &QSpinBox::valueChanged, this, publish);
    }
    for (auto* edit : findChildren<QLineEdit*>()) {
        connect(edit, &QLineEdit::textChanged, this, publish);
    }
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int) {
        update_source_mode_page();
        publish_change();
    });
    connect(geometry, &QComboBox::currentIndexChanged, this, [this](int) {
        update_receiver_mode_page();
        publish_change();
    });
    connect(import_csv, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(
            this, QStringLiteral("导入接收器 CSV"), QString(),
            QStringLiteral("CSV (*.csv);;所有文件 (*)"));
        if (!path.isEmpty()) {
            QString error;
            if (!import_receiver_csv_file(path, &error)) {
                show_validation_error(error);
            }
        }
    });
    connect(save_template, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存观测系统模板"), QString(),
            QStringLiteral("Wave3D acquisition (*.wave3d-acquisition.json)"));
        if (!path.isEmpty()) {
            QString error;
            if (!save_acquisition_template_file(path, &error)) {
                show_validation_error(error);
            }
        }
    });
    connect(load_template, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(
            this, QStringLiteral("载入观测系统模板"), QString(),
            QStringLiteral("Wave3D acquisition (*.wave3d-acquisition.json);;"
                           "所有文件 (*)"));
        if (!path.isEmpty()) {
            QString error;
            if (!load_acquisition_template_file(path, &error)) {
                show_validation_error(error);
            }
        }
    });
    connect(save, &QPushButton::clicked, this, [this] {
        if (save_callback_) {
            save_callback_();
        }
    });
    setEnabled(false);
}

void ExperimentEditor::set_callbacks(
    std::function<void()> change_callback,
    std::function<void()> save_callback) {
    change_callback_ = std::move(change_callback);
    save_callback_ = std::move(save_callback);
}

void ExperimentEditor::set_model_context(
    const Grid3D& grid,
    const ExperimentDraft& draft,
    bool loaded_from_disk,
    bool model_reference_changed) {
    populating_ = true;
    shot_id_ = draft.shot_id;
    model_reference_ = draft.model_reference;
    findChild<QLabel*>(QStringLiteral("experimentShotLabel"))->setText(shot_id_);
    findChild<QLabel*>(QStringLiteral("workspaceGridSummaryLabel"))
        ->setText(QStringLiteral("%1 × %2 × %3；%4 / %5 / %6 m（来自模型，只读）")
                      .arg(grid.nx)
                      .arg(grid.ny)
                      .arg(grid.nz)
                      .arg(grid.dx_m)
                      .arg(grid.dy_m)
                      .arg(grid.dz_m));
    findChild<QLabel*>(QStringLiteral("workspaceStorageSummaryLabel"))
        ->setText(QStringLiteral("halo=%1；X %2/%3，Y %4/%5，Z %6/%7；%8")
                      .arg(grid.halo)
                      .arg(grid.x_boundary.lower_absorbing)
                      .arg(grid.x_boundary.upper_absorbing)
                      .arg(grid.y_boundary.lower_absorbing)
                      .arg(grid.y_boundary.upper_absorbing)
                      .arg(grid.z_boundary.lower_absorbing)
                      .arg(grid.z_boundary.upper_absorbing)
                      .arg(grid.z_boundary.lower_absorbing == 0
                               ? QStringLiteral("自由表面")
                               : QStringLiteral("顶部吸收")));
    findChild<QDoubleSpinBox*>(QStringLiteral("timeStepMsSpin"))
        ->setValue(draft.dt_s * 1000.0);
    findChild<QDoubleSpinBox*>(QStringLiteral("totalTimeSpin"))
        ->setValue(draft.total_time_s);
    findChild<QDoubleSpinBox*>(QStringLiteral("cflSafetySpin"))
        ->setValue(draft.cfl_safety_factor);
    findChild<QDoubleSpinBox*>(QStringLiteral("designFrequencySpin"))
        ->setValue(draft.design_frequency_hz);
    const std::array<std::pair<const char*, double>, 3> locations{{
        {"sourceXSpin", draft.source_location_m.x_m},
        {"sourceYSpin", draft.source_location_m.y_m},
        {"sourceZSpin", draft.source_location_m.z_m}}};
    for (const auto& [name, value] : locations) {
        auto* spin = findChild<QDoubleSpinBox*>(QString::fromUtf8(name));
        spin->setValue(value);
    }
    findChild<QDoubleSpinBox*>(QStringLiteral("sourceOriginTimeSpin"))
        ->setValue(draft.source_origin_time_s);
    findChild<QDoubleSpinBox*>(QStringLiteral("sourceFrequencySpin"))
        ->setValue(draft.wavelet.dominant_frequency_hz);
    findChild<QDoubleSpinBox*>(QStringLiteral("sourcePeakDelaySpin"))
        ->setValue(draft.wavelet.peak_delay_s);
    findChild<QLineEdit*>(QStringLiteral("sourcePeakRateEdit"))
        ->setText(scientific_text(draft.wavelet.peak_rate_s_inv));
    findChild<QLineEdit*>(QStringLiteral("explosionMomentEdit"))
        ->setText(scientific_text(draft.explosion_moment_nm));
    findChild<QLineEdit*>(QStringLiteral("doubleCoupleMomentEdit"))
        ->setText(scientific_text(draft.double_couple.scalar_moment_nm));
    findChild<QDoubleSpinBox*>(QStringLiteral("doubleCoupleStrikeSpin"))
        ->setValue(draft.double_couple.strike_deg);
    findChild<QDoubleSpinBox*>(QStringLiteral("doubleCoupleDipSpin"))
        ->setValue(draft.double_couple.dip_deg);
    findChild<QDoubleSpinBox*>(QStringLiteral("doubleCoupleRakeSpin"))
        ->setValue(draft.double_couple.rake_deg);
    const std::array<std::pair<const char*, double>, 6> tensor_values{{
        {"momentMxxEdit", draft.moment_tensor_nm.m_xx_nm},
        {"momentMyyEdit", draft.moment_tensor_nm.m_yy_nm},
        {"momentMzzEdit", draft.moment_tensor_nm.m_zz_nm},
        {"momentMxyEdit", draft.moment_tensor_nm.m_xy_nm},
        {"momentMxzEdit", draft.moment_tensor_nm.m_xz_nm},
        {"momentMyzEdit", draft.moment_tensor_nm.m_yz_nm}}};
    for (const auto& [name, value] : tensor_values) {
        findChild<QLineEdit*>(QString::fromUtf8(name))
            ->setText(scientific_text(value));
    }
    findChild<QComboBox*>(QStringLiteral("sourceModeCombo"))
        ->setCurrentIndex(findChild<QComboBox*>(QStringLiteral("sourceModeCombo"))
                              ->findData(static_cast<int>(draft.source_mode)));
    update_source_mode_page();
    populate_acquisition(
        draft.acquisition.value_or(ExperimentDraftStore::default_acquisition(grid)));
    findChild<QLabel*>(QStringLiteral("experimentSaveStateLabel"))
        ->setText(model_reference_changed
                      ? QStringLiteral("草稿来自另一模型，请重新验证并保存")
                      : loaded_from_disk ? QStringLiteral("已加载保存的草稿")
                                         : QStringLiteral("尚未保存"));
    setEnabled(true);
    populating_ = false;
}

void ExperimentEditor::clear_model_context() {
    populating_ = true;
    shot_id_.clear();
    model_reference_.clear();
    explicit_receivers_.clear();
    findChild<QLabel*>(QStringLiteral("experimentShotLabel"))
        ->setText(QStringLiteral("—"));
    findChild<QLabel*>(QStringLiteral("workspaceGridSummaryLabel"))
        ->setText(QStringLiteral("—"));
    findChild<QLabel*>(QStringLiteral("workspaceStorageSummaryLabel"))
        ->setText(QStringLiteral("—"));
    findChild<QLabel*>(QStringLiteral("experimentSaveStateLabel"))
        ->setText(QStringLiteral("请先加载模型"));
    findChild<QLabel*>(QStringLiteral("experimentValidationLabel"))
        ->setText(QStringLiteral("等待模型"));
    findChild<QLabel*>(QStringLiteral("acquisitionEstimateLabel"))
        ->setText(QStringLiteral("等待模型"));
    findChild<QLabel*>(QStringLiteral("explicitReceiverCountLabel"))
        ->setText(QStringLiteral("尚未导入坐标"));
    setEnabled(false);
    populating_ = false;
}

ExperimentDraft ExperimentEditor::current_draft() const {
    if (shot_id_.isEmpty() || model_reference_.isEmpty()) {
        throw std::logic_error("experiment editor has no model context");
    }
    ExperimentDraft draft;
    draft.shot_id = shot_id_;
    draft.model_reference = model_reference_;
    draft.dt_s =
        findChild<QDoubleSpinBox*>(QStringLiteral("timeStepMsSpin"))->value() /
        1000.0;
    draft.total_time_s =
        findChild<QDoubleSpinBox*>(QStringLiteral("totalTimeSpin"))->value();
    draft.cfl_safety_factor =
        findChild<QDoubleSpinBox*>(QStringLiteral("cflSafetySpin"))->value();
    draft.design_frequency_hz =
        findChild<QDoubleSpinBox*>(QStringLiteral("designFrequencySpin"))->value();
    draft.source_location_m = {
        findChild<QDoubleSpinBox*>(QStringLiteral("sourceXSpin"))->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("sourceYSpin"))->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("sourceZSpin"))->value()};
    draft.source_origin_time_s =
        findChild<QDoubleSpinBox*>(QStringLiteral("sourceOriginTimeSpin"))
            ->value();
    draft.source_mode = static_cast<DraftSourceMode>(
        findChild<QComboBox*>(QStringLiteral("sourceModeCombo"))
            ->currentData()
            .toInt());
    draft.explosion_moment_nm = scientific_value(
        findChild<QLineEdit*>(QStringLiteral("explosionMomentEdit")));
    draft.moment_tensor_nm = {
        scientific_value(findChild<QLineEdit*>(QStringLiteral("momentMxxEdit"))),
        scientific_value(findChild<QLineEdit*>(QStringLiteral("momentMyyEdit"))),
        scientific_value(findChild<QLineEdit*>(QStringLiteral("momentMzzEdit"))),
        scientific_value(findChild<QLineEdit*>(QStringLiteral("momentMxyEdit"))),
        scientific_value(findChild<QLineEdit*>(QStringLiteral("momentMxzEdit"))),
        scientific_value(findChild<QLineEdit*>(QStringLiteral("momentMyzEdit")))};
    draft.double_couple = {
        scientific_value(
            findChild<QLineEdit*>(QStringLiteral("doubleCoupleMomentEdit"))),
        findChild<QDoubleSpinBox*>(QStringLiteral("doubleCoupleStrikeSpin"))
            ->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("doubleCoupleDipSpin"))->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("doubleCoupleRakeSpin"))
            ->value()};
    draft.wavelet = {
        findChild<QDoubleSpinBox*>(QStringLiteral("sourceFrequencySpin"))->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("sourcePeakDelaySpin"))->value(),
        scientific_value(
            findChild<QLineEdit*>(QStringLiteral("sourcePeakRateEdit")))};
    draft.acquisition = current_acquisition();
    return draft;
}

AcquisitionGeometry ExperimentEditor::current_acquisition() const {
    AcquisitionGeometry result;
    result.mode = static_cast<ReceiverGeometryMode>(
        findChild<QComboBox*>(QStringLiteral("receiverGeometryCombo"))
            ->currentData()
            .toInt());
    result.rectangular = {
        static_cast<std::size_t>(
            findChild<QSpinBox*>(QStringLiteral("receiverCountXSpin"))->value()),
        static_cast<std::size_t>(
            findChild<QSpinBox*>(QStringLiteral("receiverCountYSpin"))->value()),
        findChild<QDoubleSpinBox*>(QStringLiteral("receiverMinXSpin"))->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("receiverMaxXSpin"))->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("receiverMinYSpin"))->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("receiverMaxYSpin"))->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("receiverDepthSpin"))->value()};
    result.line = {
        static_cast<std::size_t>(
            findChild<QSpinBox*>(QStringLiteral("receiverLineCountSpin"))->value()),
        findChild<QDoubleSpinBox*>(QStringLiteral("receiverLineFirstXSpin"))
            ->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("receiverLineFirstYSpin"))
            ->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("receiverLineLastXSpin"))
            ->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("receiverLineLastYSpin"))
            ->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("receiverDepthSpin"))->value()};
    result.explicit_coordinates = explicit_receivers_;
    result.translate_x_m =
        findChild<QDoubleSpinBox*>(QStringLiteral("receiverTranslateXSpin"))
            ->value();
    result.translate_y_m =
        findChild<QDoubleSpinBox*>(QStringLiteral("receiverTranslateYSpin"))
            ->value();
    return result;
}

void ExperimentEditor::populate_acquisition(
    const AcquisitionGeometry& acquisition) {
    findChild<QComboBox*>(QStringLiteral("receiverGeometryCombo"))
        ->setCurrentIndex(
            findChild<QComboBox*>(QStringLiteral("receiverGeometryCombo"))
                ->findData(static_cast<int>(acquisition.mode)));
    findChild<QSpinBox*>(QStringLiteral("receiverCountXSpin"))
        ->setValue(static_cast<int>(acquisition.rectangular.count_x));
    findChild<QSpinBox*>(QStringLiteral("receiverCountYSpin"))
        ->setValue(static_cast<int>(acquisition.rectangular.count_y));
    const std::array<std::pair<const char*, double>, 4> rectangular_values{{
        {"receiverMinXSpin", acquisition.rectangular.minimum_x_m},
        {"receiverMaxXSpin", acquisition.rectangular.maximum_x_m},
        {"receiverMinYSpin", acquisition.rectangular.minimum_y_m},
        {"receiverMaxYSpin", acquisition.rectangular.maximum_y_m}}};
    for (const auto& [name, value] : rectangular_values) {
        findChild<QDoubleSpinBox*>(QString::fromUtf8(name))->setValue(value);
    }
    findChild<QSpinBox*>(QStringLiteral("receiverLineCountSpin"))
        ->setValue(static_cast<int>(acquisition.line.count));
    const std::array<std::pair<const char*, double>, 4> line_values{{
        {"receiverLineFirstXSpin", acquisition.line.first_x_m},
        {"receiverLineFirstYSpin", acquisition.line.first_y_m},
        {"receiverLineLastXSpin", acquisition.line.last_x_m},
        {"receiverLineLastYSpin", acquisition.line.last_y_m}}};
    for (const auto& [name, value] : line_values) {
        findChild<QDoubleSpinBox*>(QString::fromUtf8(name))->setValue(value);
    }
    findChild<QDoubleSpinBox*>(QStringLiteral("receiverDepthSpin"))
        ->setValue(0.0);
    findChild<QDoubleSpinBox*>(QStringLiteral("receiverTranslateXSpin"))
        ->setValue(acquisition.translate_x_m);
    findChild<QDoubleSpinBox*>(QStringLiteral("receiverTranslateYSpin"))
        ->setValue(acquisition.translate_y_m);
    explicit_receivers_ = acquisition.explicit_coordinates;
    findChild<QLabel*>(QStringLiteral("explicitReceiverCountLabel"))
        ->setText(explicit_receivers_.empty()
                      ? QStringLiteral("尚未导入坐标")
                      : QStringLiteral("已载入 %1 个有序接收点")
                            .arg(explicit_receivers_.size()));
    update_receiver_mode_page();
}

bool ExperimentEditor::import_receiver_csv_file(
    const QString& path,
    QString* error) {
    try {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) {
            throw std::invalid_argument("cannot open receiver CSV");
        }
        const auto receivers = ExperimentDraftStore::parse_receiver_csv(
            input.readAll());
        const auto was_populating = populating_;
        populating_ = true;
        explicit_receivers_ = receivers;
        auto* combo =
            findChild<QComboBox*>(QStringLiteral("receiverGeometryCombo"));
        combo->setCurrentIndex(combo->findData(
            static_cast<int>(ReceiverGeometryMode::ExplicitCoordinates)));
        findChild<QLabel*>(QStringLiteral("explicitReceiverCountLabel"))
            ->setText(QStringLiteral("已载入 %1 个有序接收点")
                          .arg(explicit_receivers_.size()));
        update_receiver_mode_page();
        populating_ = was_populating;
        publish_change();
        return true;
    } catch (const std::exception& exception) {
        if (error) {
            *error = QString::fromUtf8(exception.what());
        }
        return false;
    }
}

bool ExperimentEditor::save_acquisition_template_file(
    const QString& path,
    QString* error) const {
    try {
        ExperimentDraftStore::save_acquisition_template(
            path, current_acquisition());
        return true;
    } catch (const std::exception& exception) {
        if (error) {
            *error = QString::fromUtf8(exception.what());
        }
        return false;
    }
}

bool ExperimentEditor::load_acquisition_template_file(
    const QString& path,
    QString* error) {
    try {
        const auto acquisition =
            ExperimentDraftStore::load_acquisition_template(path);
        const auto was_populating = populating_;
        populating_ = true;
        populate_acquisition(acquisition);
        populating_ = was_populating;
        publish_change();
        return true;
    } catch (const std::exception& exception) {
        if (error) {
            *error = QString::fromUtf8(exception.what());
        }
        return false;
    }
}

void ExperimentEditor::show_validation(
    const ResolvedExperimentDraft& resolved,
    bool model_reference_changed) {
    const auto& report = resolved.numerical;
    const auto minimum_ppw = *std::min_element(
        report.shear_points_per_wavelength.begin(),
        report.shear_points_per_wavelength.end());
    findChild<QLabel*>(QStringLiteral("experimentValidationLabel"))
        ->setText(
            QStringLiteral(
                "参数有效 · %1 步\nCFL 上限 %2 ms，当前 %3% · 最低 S 波 %4 点/波长 · %5 点/周期")
                .arg(resolved.simulation.time.step_count())
                .arg(report.cfl_dt_limit_s * 1000.0, 0, 'f', 4)
                .arg(report.cfl_fraction * 100.0, 0, 'f', 1)
                .arg(minimum_ppw, 0, 'f', 2)
                .arg(report.time_samples_per_period, 0, 'f', 1));
    findChild<QLabel*>(QStringLiteral("acquisitionEstimateLabel"))
        ->setText(
            QStringLiteral(
                "%1 个接收器 × %2 采样 × 3 分量\n"
                "float32 道数据 %3；3 个 SEG-Y Rev1 合计 %4")
                .arg(resolved.acquisition.receiver_count)
                .arg(resolved.acquisition.sample_count)
                .arg(byte_text(resolved.acquisition.raw_trace_bytes))
                .arg(byte_text(resolved.acquisition.segy_bytes)));
    const auto& moment = resolved.source.moment;
    findChild<QLabel*>(QStringLiteral("doubleCoupleTensorLabel"))
        ->setText(
            QStringLiteral("Mxx=%1  Myy=%2  Mzz=%3\nMxy=%4  Mxz=%5  Myz=%6 N·m")
                .arg(moment.m_xx_nm, 0, 'g', 7)
                .arg(moment.m_yy_nm, 0, 'g', 7)
                .arg(moment.m_zz_nm, 0, 'g', 7)
                .arg(moment.m_xy_nm, 0, 'g', 7)
                .arg(moment.m_xz_nm, 0, 'g', 7)
                .arg(moment.m_yz_nm, 0, 'g', 7));
    findChild<QPushButton*>(QStringLiteral("saveExperimentDraftButton"))
        ->setEnabled(true);
    if (model_reference_changed) {
        findChild<QLabel*>(QStringLiteral("experimentSaveStateLabel"))
            ->setText(QStringLiteral("模型已变化，保存后绑定当前模型"));
    }
}

void ExperimentEditor::show_validation_error(const QString& message) {
    findChild<QLabel*>(QStringLiteral("experimentValidationLabel"))
        ->setText(QStringLiteral("参数无效：%1").arg(message));
    findChild<QPushButton*>(QStringLiteral("saveExperimentDraftButton"))
        ->setEnabled(false);
    findChild<QLabel*>(QStringLiteral("acquisitionEstimateLabel"))
        ->setText(QStringLiteral("无法生成观测系统或输出预估"));
    findChild<QLabel*>(QStringLiteral("doubleCoupleTensorLabel"))
        ->setText(QStringLiteral("参数无效，无法转换"));
}

void ExperimentEditor::mark_saved() {
    findChild<QLabel*>(QStringLiteral("experimentSaveStateLabel"))
        ->setText(QStringLiteral("已保存"));
}

void ExperimentEditor::publish_change() {
    if (populating_) {
        return;
    }
    findChild<QLabel*>(QStringLiteral("experimentSaveStateLabel"))
        ->setText(QStringLiteral("有未保存修改"));
    if (change_callback_) {
        change_callback_();
    }
}

void ExperimentEditor::update_source_mode_page() {
    const auto index =
        findChild<QComboBox*>(QStringLiteral("sourceModeCombo"))->currentIndex();
    findChild<QStackedWidget*>(QStringLiteral("sourceParameterStack"))
        ->setCurrentIndex(index);
}

void ExperimentEditor::update_receiver_mode_page() {
    const auto index =
        findChild<QComboBox*>(QStringLiteral("receiverGeometryCombo"))
            ->currentIndex();
    findChild<QStackedWidget*>(QStringLiteral("receiverGeometryStack"))
        ->setCurrentIndex(index);
}

} // namespace wave3d::desktop
