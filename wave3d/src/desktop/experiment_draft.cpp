#include "wave3d/desktop/experiment_draft.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace wave3d::desktop {
namespace {

[[noreturn]] void fail(const QString& message) {
    throw std::invalid_argument(message.toStdString());
}

bool valid_identifier(const QString& value) {
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$"));
    return expression.match(value).hasMatch();
}

bool safe_relative_path(const QString& value) {
    if (value.isEmpty() || QDir::isAbsolutePath(value) ||
        value.contains(QLatin1Char('\\'))) {
        return false;
    }
    const auto clean = QDir::cleanPath(value);
    return clean == value && clean != QStringLiteral(".") &&
           clean != QStringLiteral("..") &&
           !clean.startsWith(QStringLiteral("../"));
}

QString source_mode_name(DraftSourceMode mode) {
    return mode == DraftSourceMode::IsotropicExplosion
               ? QStringLiteral("isotropic_explosion")
               : QStringLiteral("moment_tensor");
}

DraftSourceMode parse_source_mode(const QString& value) {
    if (value == QStringLiteral("isotropic_explosion")) {
        return DraftSourceMode::IsotropicExplosion;
    }
    if (value == QStringLiteral("moment_tensor")) {
        return DraftSourceMode::MomentTensor;
    }
    fail(QStringLiteral("experiment source mode is unsupported"));
}

QJsonArray point_json(const PhysicalPoint3D& point) {
    return {point.x_m, point.y_m, point.z_m};
}

QJsonArray tensor_json(const SymmetricMomentTensor& tensor) {
    return {
        tensor.m_xx_nm,
        tensor.m_yy_nm,
        tensor.m_zz_nm,
        tensor.m_xy_nm,
        tensor.m_xz_nm,
        tensor.m_yz_nm};
}

void require_number_array(
    const QJsonValue& value,
    qsizetype size,
    const QString& name) {
    if (!value.isArray() || value.toArray().size() != size) {
        fail(name + QStringLiteral(" must be a numeric array"));
    }
    for (const auto& entry : value.toArray()) {
        if (!entry.isDouble()) {
            fail(name + QStringLiteral(" must be a numeric array"));
        }
    }
}

QJsonObject draft_json(const ExperimentDraft& draft) {
    return {
        {QStringLiteral("schema"), QString::fromUtf8(kExperimentDraftSchema)},
        {QStringLiteral("shot_id"), draft.shot_id},
        {QStringLiteral("model_reference"), draft.model_reference},
        {QStringLiteral("time"),
         QJsonObject{
             {QStringLiteral("dt_s"), draft.dt_s},
             {QStringLiteral("total_time_s"), draft.total_time_s}}},
        {QStringLiteral("numerics"),
         QJsonObject{
             {QStringLiteral("cfl_safety_factor"), draft.cfl_safety_factor},
             {QStringLiteral("design_frequency_hz"),
              draft.design_frequency_hz}}},
        {QStringLiteral("source"),
         QJsonObject{
             {QStringLiteral("mode"), source_mode_name(draft.source_mode)},
             {QStringLiteral("location_m"), point_json(draft.source_location_m)},
             {QStringLiteral("origin_time_s"), draft.source_origin_time_s},
             {QStringLiteral("explosion_moment_nm"),
              draft.explosion_moment_nm},
             {QStringLiteral("moment_tensor_nm"),
              tensor_json(draft.moment_tensor_nm)},
             {QStringLiteral("ricker"),
              QJsonObject{
                  {QStringLiteral("dominant_frequency_hz"),
                   draft.wavelet.dominant_frequency_hz},
                  {QStringLiteral("peak_delay_s"),
                   draft.wavelet.peak_delay_s},
                  {QStringLiteral("peak_rate_s_inv"),
                   draft.wavelet.peak_rate_s_inv}}}}}};
}

QString absolute_path(const QString& root, const QString& shot_id) {
    return QDir(root).filePath(ExperimentDraftStore::relative_path(shot_id));
}

MaterialExtrema material_extrema(const PhysicalModelExtrema& extrema) {
    return {
        extrema.minimum.vp_m_s,
        extrema.maximum.vp_m_s,
        extrema.minimum.vs_m_s,
        extrema.maximum.vs_m_s,
        extrema.minimum.density_kg_m3,
        extrema.maximum.density_kg_m3};
}

} // namespace

ExperimentDraft ExperimentDraftStore::defaults(
    const QString& shot_id,
    const QString& model_reference,
    const Grid3D& grid,
    const PhysicalModelExtrema& extrema) {
    require_valid_grid_geometry(grid);
    ExperimentDraft draft;
    draft.shot_id = shot_id;
    draft.model_reference = model_reference;
    const auto limit = elastic_cfl_dt_limit_s(
        grid, extrema.maximum.vp_m_s, draft.cfl_safety_factor);
    draft.dt_s = std::min(0.001, limit * 0.8);
    const auto maximum_spacing = std::max(
        {static_cast<double>(grid.dx_m),
         static_cast<double>(grid.dy_m),
         static_cast<double>(grid.dz_m)});
    const auto maximum_design_frequency =
        static_cast<double>(extrema.minimum.vs_m_s) /
        (minimum_design_points_per_wavelength * maximum_spacing);
    draft.design_frequency_hz =
        maximum_design_frequency >= 9.0
            ? 9.0
            : maximum_design_frequency * 0.8;
    draft.wavelet.dominant_frequency_hz = draft.design_frequency_hz / 3.0;
    draft.wavelet.peak_delay_s =
        1.0 / draft.wavelet.dominant_frequency_hz;
    draft.source_location_m = {
        static_cast<double>(grid.nx / 2) * grid.dx_m,
        static_cast<double>(grid.ny / 2) * grid.dy_m,
        static_cast<double>(grid.nz / 4) * grid.dz_m};
    validate(draft);
    static_cast<void>(resolve(draft, grid, extrema));
    return draft;
}

void ExperimentDraftStore::validate(const ExperimentDraft& draft) {
    if (!valid_identifier(draft.shot_id)) {
        fail(QStringLiteral("experiment shot_id must be a safe identifier"));
    }
    if (!safe_relative_path(draft.model_reference)) {
        fail(QStringLiteral("experiment model reference must be a safe path"));
    }
    const std::array<double, 18> finite_values{
        draft.dt_s,
        draft.total_time_s,
        draft.cfl_safety_factor,
        draft.design_frequency_hz,
        draft.source_location_m.x_m,
        draft.source_location_m.y_m,
        draft.source_location_m.z_m,
        draft.source_origin_time_s,
        draft.explosion_moment_nm,
        draft.wavelet.dominant_frequency_hz,
        draft.wavelet.peak_delay_s,
        draft.wavelet.peak_rate_s_inv,
        draft.moment_tensor_nm.m_xx_nm,
        draft.moment_tensor_nm.m_yy_nm,
        draft.moment_tensor_nm.m_zz_nm,
        draft.moment_tensor_nm.m_xy_nm,
        draft.moment_tensor_nm.m_xz_nm,
        draft.moment_tensor_nm.m_yz_nm};
    if (!std::all_of(finite_values.begin(), finite_values.end(), [](double value) {
            return std::isfinite(value);
        })) {
        fail(QStringLiteral("experiment draft values must be finite"));
    }
    if (!(draft.dt_s > 0.0) || !(draft.total_time_s > 0.0) ||
        !(draft.cfl_safety_factor > 0.0 && draft.cfl_safety_factor < 1.0) ||
        !(draft.design_frequency_hz > 0.0) ||
        draft.source_origin_time_s < 0.0) {
        fail(QStringLiteral("experiment time and numerical values are invalid"));
    }
    if (draft.source_mode != DraftSourceMode::IsotropicExplosion &&
        draft.source_mode != DraftSourceMode::MomentTensor) {
        fail(QStringLiteral("experiment source mode is unsupported"));
    }
    require_valid_ricker_wavelet(draft.wavelet);
    if (draft.source_mode == DraftSourceMode::IsotropicExplosion) {
        static_cast<void>(isotropic_explosion(draft.explosion_moment_nm));
    } else {
        require_valid_moment_tensor(draft.moment_tensor_nm);
    }
}

ResolvedExperimentDraft ExperimentDraftStore::resolve(
    const ExperimentDraft& draft,
    const Grid3D& grid,
    const PhysicalModelExtrema& extrema) {
    validate(draft);
    SimulationConfig simulation;
    simulation.grid = grid;
    simulation.time = {draft.dt_s, draft.total_time_s};
    simulation.material = material_extrema(extrema);
    simulation.top_boundary = grid.z_boundary.lower_absorbing == 0
                                  ? TopBoundary::FreeSurface
                                  : TopBoundary::Absorbing;
    simulation.numerics = {
        draft.cfl_safety_factor, draft.design_frequency_hz};
    const auto errors = validate_staggered_elastic(simulation);
    if (!errors.empty()) {
        throw std::invalid_argument(errors.front());
    }
    const auto moment =
        draft.source_mode == DraftSourceMode::IsotropicExplosion
            ? isotropic_explosion(draft.explosion_moment_nm)
            : draft.moment_tensor_nm;
    auto source = prepare_moment_tensor_source(
        grid,
        draft.source_location_m,
        draft.source_origin_time_s,
        moment,
        draft.wavelet);
    return {simulation, source, elastic_numerical_report(simulation)};
}

QString ExperimentDraftStore::relative_path(const QString& shot_id) {
    if (!valid_identifier(shot_id)) {
        fail(QStringLiteral("experiment shot_id must be a safe identifier"));
    }
    return QStringLiteral("source/%1.experiment.json").arg(shot_id);
}

bool ExperimentDraftStore::exists(
    const QString& project_root,
    const QString& shot_id) {
    return QFileInfo::exists(absolute_path(project_root, shot_id));
}

ExperimentDraft ExperimentDraftStore::load(
    const QString& project_root,
    const QString& shot_id) {
    QFile input(absolute_path(project_root, shot_id));
    if (!input.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("cannot open experiment draft"));
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("experiment draft is not valid JSON"));
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("schema")).toString() !=
        QString::fromUtf8(kExperimentDraftSchema)) {
        fail(QStringLiteral("unsupported experiment draft schema"));
    }
    if (!root.value(QStringLiteral("shot_id")).isString() ||
        !root.value(QStringLiteral("model_reference")).isString() ||
        !root.value(QStringLiteral("time")).isObject() ||
        !root.value(QStringLiteral("numerics")).isObject() ||
        !root.value(QStringLiteral("source")).isObject()) {
        fail(QStringLiteral("experiment draft has invalid value types"));
    }
    const auto time = root.value(QStringLiteral("time")).toObject();
    const auto numerics = root.value(QStringLiteral("numerics")).toObject();
    const auto source = root.value(QStringLiteral("source")).toObject();
    if (!time.value(QStringLiteral("dt_s")).isDouble() ||
        !time.value(QStringLiteral("total_time_s")).isDouble() ||
        !numerics.value(QStringLiteral("cfl_safety_factor")).isDouble() ||
        !numerics.value(QStringLiteral("design_frequency_hz")).isDouble() ||
        !source.value(QStringLiteral("mode")).isString() ||
        !source.value(QStringLiteral("origin_time_s")).isDouble() ||
        !source.value(QStringLiteral("explosion_moment_nm")).isDouble() ||
        !source.value(QStringLiteral("ricker")).isObject()) {
        fail(QStringLiteral("experiment draft has invalid value types"));
    }
    require_number_array(
        source.value(QStringLiteral("location_m")),
        3,
        QStringLiteral("source.location_m"));
    require_number_array(
        source.value(QStringLiteral("moment_tensor_nm")),
        6,
        QStringLiteral("source.moment_tensor_nm"));
    const auto ricker = source.value(QStringLiteral("ricker")).toObject();
    if (!ricker.value(QStringLiteral("dominant_frequency_hz")).isDouble() ||
        !ricker.value(QStringLiteral("peak_delay_s")).isDouble() ||
        !ricker.value(QStringLiteral("peak_rate_s_inv")).isDouble()) {
        fail(QStringLiteral("experiment Ricker values have invalid types"));
    }
    const auto location = source.value(QStringLiteral("location_m")).toArray();
    const auto tensor =
        source.value(QStringLiteral("moment_tensor_nm")).toArray();
    ExperimentDraft draft;
    draft.shot_id = root.value(QStringLiteral("shot_id")).toString();
    draft.model_reference =
        root.value(QStringLiteral("model_reference")).toString();
    draft.dt_s = time.value(QStringLiteral("dt_s")).toDouble();
    draft.total_time_s = time.value(QStringLiteral("total_time_s")).toDouble();
    draft.cfl_safety_factor =
        numerics.value(QStringLiteral("cfl_safety_factor")).toDouble();
    draft.design_frequency_hz =
        numerics.value(QStringLiteral("design_frequency_hz")).toDouble();
    draft.source_mode =
        parse_source_mode(source.value(QStringLiteral("mode")).toString());
    draft.source_location_m = {
        location[0].toDouble(), location[1].toDouble(), location[2].toDouble()};
    draft.source_origin_time_s =
        source.value(QStringLiteral("origin_time_s")).toDouble();
    draft.explosion_moment_nm =
        source.value(QStringLiteral("explosion_moment_nm")).toDouble();
    draft.moment_tensor_nm = {
        tensor[0].toDouble(),
        tensor[1].toDouble(),
        tensor[2].toDouble(),
        tensor[3].toDouble(),
        tensor[4].toDouble(),
        tensor[5].toDouble()};
    draft.wavelet = {
        ricker.value(QStringLiteral("dominant_frequency_hz")).toDouble(),
        ricker.value(QStringLiteral("peak_delay_s")).toDouble(),
        ricker.value(QStringLiteral("peak_rate_s_inv")).toDouble()};
    validate(draft);
    if (draft.shot_id != shot_id) {
        fail(QStringLiteral("experiment draft shot identity does not match file"));
    }
    return draft;
}

void ExperimentDraftStore::save(
    const QString& project_root,
    const ExperimentDraft& draft) {
    validate(draft);
    const QDir root(project_root);
    if (!QFileInfo(root.filePath(QStringLiteral("source"))).isDir()) {
        fail(QStringLiteral("project source directory is missing"));
    }
    QSaveFile output(absolute_path(project_root, draft.shot_id));
    if (!output.open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("cannot open experiment draft output"));
    }
    const auto bytes =
        QJsonDocument(draft_json(draft)).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        fail(QStringLiteral("cannot atomically publish experiment draft"));
    }
}

} // namespace wave3d::desktop
