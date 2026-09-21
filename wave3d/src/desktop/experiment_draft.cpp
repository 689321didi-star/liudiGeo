#include "wave3d/desktop/experiment_draft.hpp"

#include "wave3d/core/checked_size.hpp"

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
#include <limits>
#include <stdexcept>
#include <string>

namespace wave3d::desktop {
namespace {

constexpr std::size_t maximum_desktop_receiver_count = 1'100'000;

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
    switch (mode) {
    case DraftSourceMode::IsotropicExplosion:
        return QStringLiteral("isotropic_explosion");
    case DraftSourceMode::MomentTensor:
        return QStringLiteral("moment_tensor");
    case DraftSourceMode::DoubleCouple:
        return QStringLiteral("double_couple");
    }
    fail(QStringLiteral("experiment source mode is unsupported"));
}

DraftSourceMode parse_source_mode(const QString& value) {
    if (value == QStringLiteral("isotropic_explosion")) {
        return DraftSourceMode::IsotropicExplosion;
    }
    if (value == QStringLiteral("moment_tensor")) {
        return DraftSourceMode::MomentTensor;
    }
    if (value == QStringLiteral("double_couple")) {
        return DraftSourceMode::DoubleCouple;
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
    if (!draft.receiver_grid) {
        fail(QStringLiteral("experiment acquisition is missing"));
    }
    const auto& receivers = *draft.receiver_grid;
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
             {QStringLiteral("double_couple"),
              QJsonObject{
                  {QStringLiteral("scalar_moment_nm"),
                   draft.double_couple.scalar_moment_nm},
                  {QStringLiteral("strike_deg"),
                   draft.double_couple.strike_deg},
                  {QStringLiteral("dip_deg"), draft.double_couple.dip_deg},
                  {QStringLiteral("rake_deg"), draft.double_couple.rake_deg}}},
             {QStringLiteral("ricker"),
              QJsonObject{
                  {QStringLiteral("dominant_frequency_hz"),
                   draft.wavelet.dominant_frequency_hz},
                  {QStringLiteral("peak_delay_s"),
                   draft.wavelet.peak_delay_s},
                  {QStringLiteral("peak_rate_s_inv"),
                   draft.wavelet.peak_rate_s_inv}}}}},
        {QStringLiteral("acquisition"),
         QJsonObject{
             {QStringLiteral("mode"), QStringLiteral("surface_rectangular")},
             {QStringLiteral("count_x"),
              static_cast<qint64>(receivers.count_x)},
             {QStringLiteral("count_y"),
              static_cast<qint64>(receivers.count_y)},
             {QStringLiteral("x_range_m"),
              QJsonArray{receivers.minimum_x_m, receivers.maximum_x_m}},
             {QStringLiteral("y_range_m"),
              QJsonArray{receivers.minimum_y_m, receivers.maximum_y_m}},
             {QStringLiteral("depth_m"), receivers.depth_m},
             {QStringLiteral("components"),
              QJsonArray{QStringLiteral("vx"), QStringLiteral("vy"),
                         QStringLiteral("vz")}}}}};
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
    const auto conservative_dt_s = std::min(0.001, limit * 0.8);
    draft.dt_s = std::floor(conservative_dt_s * 1.0e6) / 1.0e6;
    if (!(draft.dt_s > 0.0)) {
        fail(QStringLiteral(
            "model CFL limit is below the minimum SEG-Y microsecond interval"));
    }
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
    draft.receiver_grid = default_receiver_grid(grid);
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
    const std::array<double, 22> finite_values{
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
        draft.moment_tensor_nm.m_yz_nm,
        draft.double_couple.scalar_moment_nm,
        draft.double_couple.strike_deg,
        draft.double_couple.dip_deg,
        draft.double_couple.rake_deg};
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
        draft.source_mode != DraftSourceMode::MomentTensor &&
        draft.source_mode != DraftSourceMode::DoubleCouple) {
        fail(QStringLiteral("experiment source mode is unsupported"));
    }
    require_valid_ricker_wavelet(draft.wavelet);
    if (draft.source_mode == DraftSourceMode::IsotropicExplosion) {
        static_cast<void>(isotropic_explosion(draft.explosion_moment_nm));
    } else if (draft.source_mode == DraftSourceMode::MomentTensor) {
        require_valid_moment_tensor(draft.moment_tensor_nm);
    } else {
        static_cast<void>(double_couple_from_strike_dip_rake(
            draft.double_couple.scalar_moment_nm,
            draft.double_couple.strike_deg,
            draft.double_couple.dip_deg,
            draft.double_couple.rake_deg));
    }
    if (!draft.receiver_grid) {
        fail(QStringLiteral("experiment acquisition is missing"));
    }
    const auto& receivers = *draft.receiver_grid;
    const std::array<double, 5> receiver_values{
        receivers.minimum_x_m,
        receivers.maximum_x_m,
        receivers.minimum_y_m,
        receivers.maximum_y_m,
        receivers.depth_m};
    if (!std::all_of(
            receiver_values.begin(), receiver_values.end(), [](double value) {
                return std::isfinite(value);
            })) {
        fail(QStringLiteral("receiver coordinates must be finite"));
    }
    if (receivers.count_x < 2 || receivers.count_y < 2 ||
        !(receivers.minimum_x_m < receivers.maximum_x_m) ||
        !(receivers.minimum_y_m < receivers.maximum_y_m) ||
        receivers.depth_m != 0.0) {
        fail(QStringLiteral(
            "surface receiver grid needs two or more points per axis, "
            "increasing bounds, and depth 0 m"));
    }
    const auto receiver_count = detail::checked_size_product(
        receivers.count_x,
        receivers.count_y,
        "receiver count overflows size_t");
    if (receiver_count > maximum_desktop_receiver_count) {
        fail(QStringLiteral(
            "desktop receiver grid exceeds the 1,100,000 point safety limit"));
    }
}

RectangularReceiverGrid ExperimentDraftStore::default_receiver_grid(
    const Grid3D& grid) {
    require_valid_grid_geometry(grid);
    return {
        101,
        101,
        0.0,
        static_cast<double>(grid.nx - 1) * grid.dx_m,
        0.0,
        static_cast<double>(grid.ny - 1) * grid.dy_m,
        0.0};
}

std::vector<PhysicalPoint3D> ExperimentDraftStore::generate_receivers(
    const RectangularReceiverGrid& receiver_grid,
    const Grid3D& grid) {
    ExperimentDraft validation;
    validation.shot_id = QStringLiteral("validation");
    validation.model_reference = QStringLiteral("models/validation.h5");
    validation.receiver_grid = receiver_grid;
    validate(validation);
    const auto count = detail::checked_size_product(
        receiver_grid.count_x,
        receiver_grid.count_y,
        "receiver count overflows size_t");
    std::vector<PhysicalPoint3D> result;
    result.reserve(count);
    for (std::size_t iy = 0; iy < receiver_grid.count_y; ++iy) {
        const auto y_fraction = static_cast<double>(iy) /
                                static_cast<double>(receiver_grid.count_y - 1);
        const auto y = receiver_grid.minimum_y_m +
                       y_fraction *
                           (receiver_grid.maximum_y_m -
                            receiver_grid.minimum_y_m);
        for (std::size_t ix = 0; ix < receiver_grid.count_x; ++ix) {
            const auto x_fraction = static_cast<double>(ix) /
                                    static_cast<double>(receiver_grid.count_x - 1);
            const PhysicalPoint3D point{
                receiver_grid.minimum_x_m +
                    x_fraction *
                        (receiver_grid.maximum_x_m -
                         receiver_grid.minimum_x_m),
                y,
                receiver_grid.depth_m};
            static_cast<void>(physical_to_storage_coordinate(grid, point));
            result.push_back(point);
        }
    }
    return result;
}

AcquisitionEstimate ExperimentDraftStore::acquisition_estimate(
    std::size_t receiver_count,
    std::size_t sample_count) {
    if (receiver_count == 0 || sample_count == 0) {
        fail(QStringLiteral("receiver and sample counts must be positive"));
    }
    const auto component_traces = detail::checked_size_product(
        receiver_count, std::size_t{3}, "component trace count overflows size_t");
    const auto trace_values = detail::checked_size_product(
        component_traces,
        sample_count,
        "three-component trace value count overflows size_t");
    const auto raw_bytes = detail::checked_size_product(
        trace_values, sizeof(float), "raw trace byte count overflows size_t");
    const auto sample_bytes = detail::checked_size_product(
        sample_count, sizeof(float), "SEG-Y sample bytes overflow size_t");
    if (sample_bytes > std::numeric_limits<std::size_t>::max() - 240) {
        fail(QStringLiteral("SEG-Y trace bytes overflow size_t"));
    }
    const auto trace_bytes = sample_bytes + 240;
    const auto all_trace_bytes = detail::checked_size_product(
        component_traces, trace_bytes, "SEG-Y trace bytes overflow size_t");
    const auto file_headers = detail::checked_size_product(
        std::size_t{3}, std::size_t{3600}, "SEG-Y headers overflow size_t");
    const auto segy_bytes = detail::checked_size_add(
        all_trace_bytes, file_headers, "SEG-Y files overflow size_t");
    return {
        receiver_count,
        sample_count,
        trace_values,
        raw_bytes,
        segy_bytes};
}

ResolvedExperimentDraft ExperimentDraftStore::resolve(
    const ExperimentDraft& draft,
    const Grid3D& grid,
    const PhysicalModelExtrema& extrema) {
    validate(draft);
    if (!draft.receiver_grid) {
        fail(QStringLiteral("experiment acquisition is missing"));
    }
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
    SymmetricMomentTensor moment;
    switch (draft.source_mode) {
    case DraftSourceMode::IsotropicExplosion:
        moment = isotropic_explosion(draft.explosion_moment_nm);
        break;
    case DraftSourceMode::MomentTensor:
        moment = draft.moment_tensor_nm;
        break;
    case DraftSourceMode::DoubleCouple:
        moment = double_couple_from_strike_dip_rake(
            draft.double_couple.scalar_moment_nm,
            draft.double_couple.strike_deg,
            draft.double_couple.dip_deg,
            draft.double_couple.rake_deg);
        break;
    }
    auto source = prepare_moment_tensor_source(
        grid,
        draft.source_location_m,
        draft.source_origin_time_s,
        moment,
        draft.wavelet);
    auto receivers = generate_receivers(*draft.receiver_grid, grid);
    auto acquisition = acquisition_estimate(
        receivers.size(), simulation.time.step_count());
    return {
        simulation,
        source,
        elastic_numerical_report(simulation),
        std::move(receivers),
        acquisition};
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
    const auto schema = root.value(QStringLiteral("schema")).toString();
    const bool legacy_v1 =
        schema == QString::fromUtf8(kLegacyExperimentDraftSchema);
    const bool legacy_v2 =
        schema == QString::fromUtf8(kLegacyExperimentDraftSchemaV2);
    if (!legacy_v1 && !legacy_v2 &&
        schema != QString::fromUtf8(kExperimentDraftSchema)) {
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
    if (!legacy_v1 && !legacy_v2) {
        if (!source.value(QStringLiteral("double_couple")).isObject()) {
            fail(QStringLiteral(
                "experiment double-couple parameters have invalid type"));
        }
        const auto double_couple =
            source.value(QStringLiteral("double_couple")).toObject();
        if (!double_couple.value(QStringLiteral("scalar_moment_nm")).isDouble() ||
            !double_couple.value(QStringLiteral("strike_deg")).isDouble() ||
            !double_couple.value(QStringLiteral("dip_deg")).isDouble() ||
            !double_couple.value(QStringLiteral("rake_deg")).isDouble()) {
            fail(QStringLiteral(
                "experiment double-couple parameters have invalid types"));
        }
        draft.double_couple = {
            double_couple.value(QStringLiteral("scalar_moment_nm")).toDouble(),
            double_couple.value(QStringLiteral("strike_deg")).toDouble(),
            double_couple.value(QStringLiteral("dip_deg")).toDouble(),
            double_couple.value(QStringLiteral("rake_deg")).toDouble()};
    }
    draft.wavelet = {
        ricker.value(QStringLiteral("dominant_frequency_hz")).toDouble(),
        ricker.value(QStringLiteral("peak_delay_s")).toDouble(),
        ricker.value(QStringLiteral("peak_rate_s_inv")).toDouble()};
    if (!legacy_v1) {
        if (!root.value(QStringLiteral("acquisition")).isObject()) {
            fail(QStringLiteral("experiment acquisition has invalid type"));
        }
        const auto acquisition =
            root.value(QStringLiteral("acquisition")).toObject();
        if (acquisition.value(QStringLiteral("mode")).toString() !=
                QStringLiteral("surface_rectangular") ||
            !acquisition.value(QStringLiteral("count_x")).isDouble() ||
            !acquisition.value(QStringLiteral("count_y")).isDouble() ||
            !acquisition.value(QStringLiteral("depth_m")).isDouble()) {
            fail(QStringLiteral("experiment acquisition values are invalid"));
        }
        require_number_array(
            acquisition.value(QStringLiteral("x_range_m")),
            2,
            QStringLiteral("acquisition.x_range_m"));
        require_number_array(
            acquisition.value(QStringLiteral("y_range_m")),
            2,
            QStringLiteral("acquisition.y_range_m"));
        const auto components =
            acquisition.value(QStringLiteral("components")).toArray();
        if (components.size() != 3 ||
            components[0].toString() != QStringLiteral("vx") ||
            components[1].toString() != QStringLiteral("vy") ||
            components[2].toString() != QStringLiteral("vz")) {
            fail(QStringLiteral("experiment receiver components must be vx,vy,vz"));
        }
        const auto count_x = acquisition.value(QStringLiteral("count_x")).toDouble();
        const auto count_y = acquisition.value(QStringLiteral("count_y")).toDouble();
        if (count_x < 0.0 || count_y < 0.0 ||
            std::floor(count_x) != count_x || std::floor(count_y) != count_y ||
            count_x > static_cast<double>(std::numeric_limits<std::size_t>::max()) ||
            count_y > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
            fail(QStringLiteral("experiment receiver counts must be integers"));
        }
        const auto x_range =
            acquisition.value(QStringLiteral("x_range_m")).toArray();
        const auto y_range =
            acquisition.value(QStringLiteral("y_range_m")).toArray();
        draft.receiver_grid = RectangularReceiverGrid{
            static_cast<std::size_t>(count_x),
            static_cast<std::size_t>(count_y),
            x_range[0].toDouble(),
            x_range[1].toDouble(),
            y_range[0].toDouble(),
            y_range[1].toDouble(),
            acquisition.value(QStringLiteral("depth_m")).toDouble()};
    }
    if (legacy_v1) {
        auto legacy_validation = draft;
        legacy_validation.receiver_grid =
            RectangularReceiverGrid{2, 2, 0.0, 1.0, 0.0, 1.0, 0.0};
        validate(legacy_validation);
    } else {
        validate(draft);
    }
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
