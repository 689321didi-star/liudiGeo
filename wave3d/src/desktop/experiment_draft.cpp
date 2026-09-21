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
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>

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

QString receiver_mode_name(ReceiverGeometryMode mode) {
    switch (mode) {
    case ReceiverGeometryMode::SurfaceRectangular:
        return QStringLiteral("surface_rectangular");
    case ReceiverGeometryMode::SurfaceLine:
        return QStringLiteral("surface_line");
    case ReceiverGeometryMode::ExplicitCoordinates:
        return QStringLiteral("explicit_coordinates");
    }
    fail(QStringLiteral("receiver geometry mode is unsupported"));
}

ReceiverGeometryMode parse_receiver_mode(const QString& value) {
    if (value == QStringLiteral("surface_rectangular")) {
        return ReceiverGeometryMode::SurfaceRectangular;
    }
    if (value == QStringLiteral("surface_line")) {
        return ReceiverGeometryMode::SurfaceLine;
    }
    if (value == QStringLiteral("explicit_coordinates")) {
        return ReceiverGeometryMode::ExplicitCoordinates;
    }
    fail(QStringLiteral("receiver geometry mode is unsupported"));
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

QJsonArray points_json(const std::vector<PhysicalPoint3D>& points) {
    QJsonArray result;
    for (const auto& point : points) {
        result.append(point_json(point));
    }
    return result;
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

std::size_t checked_json_count(const QJsonValue& value, const QString& name) {
    if (!value.isDouble()) {
        fail(name + QStringLiteral(" must be an integer"));
    }
    const auto count = value.toDouble();
    if (count < 0.0 || std::floor(count) != count ||
        count > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        fail(name + QStringLiteral(" must be an integer"));
    }
    return static_cast<std::size_t>(count);
}

void validate_receiver_locations(
    const std::vector<PhysicalPoint3D>& receivers,
    bool check_duplicates = true) {
    if (receivers.empty()) {
        fail(QStringLiteral("receiver geometry must not be empty"));
    }
    if (receivers.size() > maximum_desktop_receiver_count) {
        fail(QStringLiteral(
            "desktop receiver geometry exceeds the 1,100,000 point safety limit"));
    }
    std::set<std::tuple<double, double, double>> unique;
    for (const auto& point : receivers) {
        if (!std::isfinite(point.x_m) || !std::isfinite(point.y_m) ||
            !std::isfinite(point.z_m)) {
            fail(QStringLiteral("receiver coordinates must be finite"));
        }
        if (point.z_m != 0.0) {
            fail(QStringLiteral("desktop receivers must remain at depth 0 m"));
        }
        if (check_duplicates &&
            !unique.emplace(point.x_m, point.y_m, point.z_m).second) {
            fail(QStringLiteral("receiver geometry contains duplicate coordinates"));
        }
    }
}

std::vector<PhysicalPoint3D> receiver_locations(
    const AcquisitionGeometry& acquisition) {
    if (!std::isfinite(acquisition.translate_x_m) ||
        !std::isfinite(acquisition.translate_y_m)) {
        fail(QStringLiteral("receiver translation must be finite"));
    }
    std::vector<PhysicalPoint3D> result;
    if (acquisition.mode == ReceiverGeometryMode::SurfaceRectangular) {
        const auto& grid = acquisition.rectangular;
        if (grid.count_x < 2 || grid.count_y < 2 ||
            !std::isfinite(grid.minimum_x_m) ||
            !std::isfinite(grid.maximum_x_m) ||
            !std::isfinite(grid.minimum_y_m) ||
            !std::isfinite(grid.maximum_y_m) ||
            !std::isfinite(grid.depth_m) ||
            !(grid.minimum_x_m < grid.maximum_x_m) ||
            !(grid.minimum_y_m < grid.maximum_y_m) || grid.depth_m != 0.0) {
            fail(QStringLiteral(
                "surface receiver grid needs finite increasing bounds, two or "
                "more points per axis, and depth 0 m"));
        }
        const auto count = detail::checked_size_product(
            grid.count_x, grid.count_y, "receiver count overflows size_t");
        if (count > maximum_desktop_receiver_count) {
            fail(QStringLiteral(
                "desktop receiver geometry exceeds the 1,100,000 point safety limit"));
        }
        result.reserve(count);
        for (std::size_t iy = 0; iy < grid.count_y; ++iy) {
            const auto fy = static_cast<double>(iy) /
                            static_cast<double>(grid.count_y - 1);
            const auto y = grid.minimum_y_m +
                           fy * (grid.maximum_y_m - grid.minimum_y_m);
            for (std::size_t ix = 0; ix < grid.count_x; ++ix) {
                const auto fx = static_cast<double>(ix) /
                                static_cast<double>(grid.count_x - 1);
                result.push_back({
                    grid.minimum_x_m +
                        fx * (grid.maximum_x_m - grid.minimum_x_m),
                    y,
                    grid.depth_m});
            }
        }
    } else if (acquisition.mode == ReceiverGeometryMode::SurfaceLine) {
        const auto& line = acquisition.line;
        if (line.count < 2 || !std::isfinite(line.first_x_m) ||
            !std::isfinite(line.first_y_m) ||
            !std::isfinite(line.last_x_m) ||
            !std::isfinite(line.last_y_m) ||
            !std::isfinite(line.depth_m) || line.depth_m != 0.0 ||
            (line.first_x_m == line.last_x_m &&
             line.first_y_m == line.last_y_m)) {
            fail(QStringLiteral(
                "surface receiver line needs distinct finite endpoints, two or "
                "more points, and depth 0 m"));
        }
        if (line.count > maximum_desktop_receiver_count) {
            fail(QStringLiteral(
                "desktop receiver geometry exceeds the 1,100,000 point safety limit"));
        }
        result.reserve(line.count);
        for (std::size_t index = 0; index < line.count; ++index) {
            const auto fraction = static_cast<double>(index) /
                                  static_cast<double>(line.count - 1);
            result.push_back({
                line.first_x_m + fraction * (line.last_x_m - line.first_x_m),
                line.first_y_m + fraction * (line.last_y_m - line.first_y_m),
                line.depth_m});
        }
    } else if (acquisition.mode ==
               ReceiverGeometryMode::ExplicitCoordinates) {
        result = acquisition.explicit_coordinates;
    } else {
        fail(QStringLiteral("receiver geometry mode is unsupported"));
    }
    for (auto& point : result) {
        point.x_m += acquisition.translate_x_m;
        point.y_m += acquisition.translate_y_m;
    }
    validate_receiver_locations(
        result, acquisition.mode == ReceiverGeometryMode::ExplicitCoordinates);
    return result;
}

QJsonObject acquisition_json(const AcquisitionGeometry& acquisition) {
    const auto& rectangular = acquisition.rectangular;
    const auto& line = acquisition.line;
    return {
        {QStringLiteral("mode"), receiver_mode_name(acquisition.mode)},
        {QStringLiteral("translation_m"),
         QJsonArray{acquisition.translate_x_m, acquisition.translate_y_m}},
        {QStringLiteral("rectangular"),
         QJsonObject{
             {QStringLiteral("count_x"), static_cast<qint64>(rectangular.count_x)},
             {QStringLiteral("count_y"), static_cast<qint64>(rectangular.count_y)},
             {QStringLiteral("x_range_m"),
              QJsonArray{rectangular.minimum_x_m, rectangular.maximum_x_m}},
             {QStringLiteral("y_range_m"),
              QJsonArray{rectangular.minimum_y_m, rectangular.maximum_y_m}},
             {QStringLiteral("depth_m"), rectangular.depth_m}}},
        {QStringLiteral("line"),
         QJsonObject{
             {QStringLiteral("count"), static_cast<qint64>(line.count)},
             {QStringLiteral("first_m"),
              QJsonArray{line.first_x_m, line.first_y_m, line.depth_m}},
             {QStringLiteral("last_m"),
              QJsonArray{line.last_x_m, line.last_y_m, line.depth_m}}}},
        {QStringLiteral("explicit_coordinates_m"),
         points_json(acquisition.explicit_coordinates)},
        {QStringLiteral("components"),
         QJsonArray{QStringLiteral("vx"), QStringLiteral("vy"),
                    QStringLiteral("vz")}}};
}

void require_receiver_components(const QJsonObject& object) {
    const auto components = object.value(QStringLiteral("components")).toArray();
    if (components.size() != 3 ||
        components[0].toString() != QStringLiteral("vx") ||
        components[1].toString() != QStringLiteral("vy") ||
        components[2].toString() != QStringLiteral("vz")) {
        fail(QStringLiteral("experiment receiver components must be vx,vy,vz"));
    }
}

AcquisitionGeometry parse_acquisition_v4(const QJsonObject& object) {
    if (!object.value(QStringLiteral("mode")).isString() ||
        !object.value(QStringLiteral("rectangular")).isObject() ||
        !object.value(QStringLiteral("line")).isObject() ||
        !object.value(QStringLiteral("explicit_coordinates_m")).isArray()) {
        fail(QStringLiteral("experiment acquisition values are invalid"));
    }
    require_number_array(
        object.value(QStringLiteral("translation_m")),
        2,
        QStringLiteral("acquisition.translation_m"));
    require_receiver_components(object);
    const auto rectangular = object.value(QStringLiteral("rectangular")).toObject();
    require_number_array(
        rectangular.value(QStringLiteral("x_range_m")),
        2,
        QStringLiteral("acquisition.rectangular.x_range_m"));
    require_number_array(
        rectangular.value(QStringLiteral("y_range_m")),
        2,
        QStringLiteral("acquisition.rectangular.y_range_m"));
    if (!rectangular.value(QStringLiteral("depth_m")).isDouble()) {
        fail(QStringLiteral("acquisition rectangular depth must be numeric"));
    }
    const auto line = object.value(QStringLiteral("line")).toObject();
    require_number_array(
        line.value(QStringLiteral("first_m")),
        3,
        QStringLiteral("acquisition.line.first_m"));
    require_number_array(
        line.value(QStringLiteral("last_m")),
        3,
        QStringLiteral("acquisition.line.last_m"));
    const auto translation = object.value(QStringLiteral("translation_m")).toArray();
    const auto x_range = rectangular.value(QStringLiteral("x_range_m")).toArray();
    const auto y_range = rectangular.value(QStringLiteral("y_range_m")).toArray();
    const auto first = line.value(QStringLiteral("first_m")).toArray();
    const auto last = line.value(QStringLiteral("last_m")).toArray();
    AcquisitionGeometry result;
    result.mode = parse_receiver_mode(
        object.value(QStringLiteral("mode")).toString());
    result.translate_x_m = translation[0].toDouble();
    result.translate_y_m = translation[1].toDouble();
    result.rectangular = {
        checked_json_count(
            rectangular.value(QStringLiteral("count_x")),
            QStringLiteral("acquisition.rectangular.count_x")),
        checked_json_count(
            rectangular.value(QStringLiteral("count_y")),
            QStringLiteral("acquisition.rectangular.count_y")),
        x_range[0].toDouble(),
        x_range[1].toDouble(),
        y_range[0].toDouble(),
        y_range[1].toDouble(),
        rectangular.value(QStringLiteral("depth_m")).toDouble()};
    result.line = {
        checked_json_count(
            line.value(QStringLiteral("count")),
            QStringLiteral("acquisition.line.count")),
        first[0].toDouble(),
        first[1].toDouble(),
        last[0].toDouble(),
        last[1].toDouble(),
        first[2].toDouble()};
    if (last[2].toDouble() != result.line.depth_m) {
        fail(QStringLiteral("receiver line endpoints must share one depth"));
    }
    const auto explicit_points =
        object.value(QStringLiteral("explicit_coordinates_m")).toArray();
    if (explicit_points.size() >
        static_cast<qsizetype>(maximum_desktop_receiver_count)) {
        fail(QStringLiteral(
            "desktop receiver geometry exceeds the 1,100,000 point safety limit"));
    }
    result.explicit_coordinates.reserve(
        static_cast<std::size_t>(explicit_points.size()));
    for (const auto& value : explicit_points) {
        require_number_array(
            value, 3, QStringLiteral("acquisition.explicit_coordinates_m row"));
        const auto point = value.toArray();
        result.explicit_coordinates.push_back(
            {point[0].toDouble(), point[1].toDouble(), point[2].toDouble()});
    }
    static_cast<void>(receiver_locations(result));
    return result;
}

AcquisitionGeometry parse_legacy_acquisition(const QJsonObject& object) {
    if (object.value(QStringLiteral("mode")).toString() !=
            QStringLiteral("surface_rectangular") ||
        !object.value(QStringLiteral("depth_m")).isDouble()) {
        fail(QStringLiteral("legacy experiment acquisition values are invalid"));
    }
    require_receiver_components(object);
    require_number_array(
        object.value(QStringLiteral("x_range_m")),
        2,
        QStringLiteral("acquisition.x_range_m"));
    require_number_array(
        object.value(QStringLiteral("y_range_m")),
        2,
        QStringLiteral("acquisition.y_range_m"));
    const auto x_range = object.value(QStringLiteral("x_range_m")).toArray();
    const auto y_range = object.value(QStringLiteral("y_range_m")).toArray();
    AcquisitionGeometry result;
    result.rectangular = {
        checked_json_count(
            object.value(QStringLiteral("count_x")),
            QStringLiteral("acquisition.count_x")),
        checked_json_count(
            object.value(QStringLiteral("count_y")),
            QStringLiteral("acquisition.count_y")),
        x_range[0].toDouble(),
        x_range[1].toDouble(),
        y_range[0].toDouble(),
        y_range[1].toDouble(),
        object.value(QStringLiteral("depth_m")).toDouble()};
    result.line = {
        result.rectangular.count_x,
        result.rectangular.minimum_x_m,
        0.5 * (result.rectangular.minimum_y_m +
               result.rectangular.maximum_y_m),
        result.rectangular.maximum_x_m,
        0.5 * (result.rectangular.minimum_y_m +
               result.rectangular.maximum_y_m),
        result.rectangular.depth_m};
    static_cast<void>(receiver_locations(result));
    return result;
}

QJsonObject draft_json(const ExperimentDraft& draft) {
    if (!draft.acquisition) {
        fail(QStringLiteral("experiment acquisition is missing"));
    }
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
        {QStringLiteral("acquisition"), acquisition_json(*draft.acquisition)}};
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
    draft.acquisition = default_acquisition(grid);
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
    if (!draft.acquisition) {
        fail(QStringLiteral("experiment acquisition is missing"));
    }
    static_cast<void>(receiver_locations(*draft.acquisition));
}

AcquisitionGeometry ExperimentDraftStore::default_acquisition(
    const Grid3D& grid) {
    require_valid_grid_geometry(grid);
    AcquisitionGeometry result;
    result.rectangular = {
        101,
        101,
        0.0,
        static_cast<double>(grid.nx - 1) * grid.dx_m,
        0.0,
        static_cast<double>(grid.ny - 1) * grid.dy_m,
        0.0};
    result.line = {
        101,
        0.0,
        static_cast<double>(grid.ny / 2) * grid.dy_m,
        static_cast<double>(grid.nx - 1) * grid.dx_m,
        static_cast<double>(grid.ny / 2) * grid.dy_m,
        0.0};
    return result;
}

std::vector<PhysicalPoint3D> ExperimentDraftStore::generate_receivers(
    const AcquisitionGeometry& acquisition,
    const Grid3D& grid) {
    require_valid_grid_geometry(grid);
    auto result = receiver_locations(acquisition);
    for (const auto& point : result) {
        static_cast<void>(physical_to_storage_coordinate(grid, point));
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

std::vector<PhysicalPoint3D> ExperimentDraftStore::parse_receiver_csv(
    const QByteArray& csv) {
    auto lines = csv.split('\n');
    while (!lines.empty() && lines.front().trimmed().isEmpty()) {
        lines.removeFirst();
    }
    if (lines.empty()) {
        fail(QStringLiteral("receiver CSV is empty"));
    }
    auto header = lines.takeFirst().trimmed();
    if (header.startsWith("\xEF\xBB\xBF")) {
        header.remove(0, 3);
    }
    const auto header_columns = header.split(',');
    if (header_columns.size() != 3 ||
        header_columns[0].trimmed() != QByteArray("x_m") ||
        header_columns[1].trimmed() != QByteArray("y_m") ||
        header_columns[2].trimmed() != QByteArray("z_m")) {
        fail(QStringLiteral("receiver CSV header must be x_m,y_m,z_m"));
    }
    std::vector<PhysicalPoint3D> result;
    result.reserve(static_cast<std::size_t>(lines.size()));
    for (qsizetype row = 0; row < lines.size(); ++row) {
        const auto line = lines[row].trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const auto columns = line.split(',');
        if (columns.size() != 3) {
            fail(QStringLiteral("receiver CSV row %1 must have three columns")
                     .arg(row + 2));
        }
        bool x_valid = false;
        bool y_valid = false;
        bool z_valid = false;
        const auto x = columns[0].trimmed().toDouble(&x_valid);
        const auto y = columns[1].trimmed().toDouble(&y_valid);
        const auto z = columns[2].trimmed().toDouble(&z_valid);
        if (!x_valid || !y_valid || !z_valid) {
            fail(QStringLiteral("receiver CSV row %1 is not numeric").arg(row + 2));
        }
        result.push_back({x, y, z});
        if (result.size() > maximum_desktop_receiver_count) {
            fail(QStringLiteral(
                "desktop receiver geometry exceeds the 1,100,000 point safety limit"));
        }
    }
    validate_receiver_locations(result);
    return result;
}

void ExperimentDraftStore::save_acquisition_template(
    const QString& path,
    const AcquisitionGeometry& acquisition) {
    static_cast<void>(receiver_locations(acquisition));
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("cannot open acquisition template output"));
    }
    const QJsonObject root{
        {QStringLiteral("schema"),
         QString::fromUtf8(kAcquisitionTemplateSchema)},
        {QStringLiteral("acquisition"), acquisition_json(acquisition)}};
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        fail(QStringLiteral("cannot atomically publish acquisition template"));
    }
}

AcquisitionGeometry ExperimentDraftStore::load_acquisition_template(
    const QString& path) {
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("cannot open acquisition template"));
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("acquisition template is not valid JSON"));
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("schema")).toString() !=
            QString::fromUtf8(kAcquisitionTemplateSchema) ||
        !root.value(QStringLiteral("acquisition")).isObject()) {
        fail(QStringLiteral("unsupported acquisition template schema"));
    }
    return parse_acquisition_v4(
        root.value(QStringLiteral("acquisition")).toObject());
}

ResolvedExperimentDraft ExperimentDraftStore::resolve(
    const ExperimentDraft& draft,
    const Grid3D& grid,
    const PhysicalModelExtrema& extrema) {
    validate(draft);
    if (!draft.acquisition) {
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
    auto receivers = generate_receivers(*draft.acquisition, grid);
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
    const bool legacy_v3 =
        schema == QString::fromUtf8(kLegacyExperimentDraftSchemaV3);
    if (!legacy_v1 && !legacy_v2 && !legacy_v3 &&
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
        const auto acquisition = root.value(QStringLiteral("acquisition")).toObject();
        draft.acquisition = legacy_v2 || legacy_v3
                                ? parse_legacy_acquisition(acquisition)
                                : parse_acquisition_v4(acquisition);
    }
    if (legacy_v1) {
        auto legacy_validation = draft;
        legacy_validation.acquisition = AcquisitionGeometry{};
        legacy_validation.acquisition->rectangular =
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
