#include "wave3d/desktop/experiment_editor.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
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
#include <QStackedWidget>
#include <QStandardItemModel>
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
    mode->addItem(QStringLiteral("各向同性爆炸源"), 0);
    mode->addItem(QStringLiteral("手工对称矩张量"), 1);
    mode->addItem(QStringLiteral("双力偶（后续）"), 2);
    mode->setToolTip(QStringLiteral("后续提供走向/倾角/滑动角转换"));
    if (auto* items = qobject_cast<QStandardItemModel*>(mode->model())) {
        items->item(2)->setEnabled(false);
    }
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
    source_layout->addWidget(source_pages);
    layout->addWidget(source);

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
    for (auto* edit : findChildren<QLineEdit*>()) {
        connect(edit, &QLineEdit::textChanged, this, publish);
    }
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int) {
        update_source_mode_page();
        publish_change();
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
        ->setCurrentIndex(
            draft.source_mode == DraftSourceMode::IsotropicExplosion ? 0 : 1);
    update_source_mode_page();
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
    draft.source_mode =
        findChild<QComboBox*>(QStringLiteral("sourceModeCombo"))->currentIndex() == 0
            ? DraftSourceMode::IsotropicExplosion
            : DraftSourceMode::MomentTensor;
    draft.explosion_moment_nm = scientific_value(
        findChild<QLineEdit*>(QStringLiteral("explosionMomentEdit")));
    draft.moment_tensor_nm = {
        scientific_value(findChild<QLineEdit*>(QStringLiteral("momentMxxEdit"))),
        scientific_value(findChild<QLineEdit*>(QStringLiteral("momentMyyEdit"))),
        scientific_value(findChild<QLineEdit*>(QStringLiteral("momentMzzEdit"))),
        scientific_value(findChild<QLineEdit*>(QStringLiteral("momentMxyEdit"))),
        scientific_value(findChild<QLineEdit*>(QStringLiteral("momentMxzEdit"))),
        scientific_value(findChild<QLineEdit*>(QStringLiteral("momentMyzEdit")))};
    draft.wavelet = {
        findChild<QDoubleSpinBox*>(QStringLiteral("sourceFrequencySpin"))->value(),
        findChild<QDoubleSpinBox*>(QStringLiteral("sourcePeakDelaySpin"))->value(),
        scientific_value(
            findChild<QLineEdit*>(QStringLiteral("sourcePeakRateEdit")))};
    return draft;
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
        ->setCurrentIndex(index == 0 ? 0 : 1);
}

} // namespace wave3d::desktop
