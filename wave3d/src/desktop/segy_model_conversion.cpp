#include "wave3d/desktop/segy_model_conversion.hpp"

#include "wave3d/io/hdf5.hpp"
#include "wave3d/io/segy.hpp"
#include "wave3d/model/physical_model.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <stdexcept>
#include <string>

namespace wave3d::desktop {
namespace {

QString file_sha256(const QString& path) {
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("cannot open SEG-Y model input for SHA-256");
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input)) {
        throw std::runtime_error("cannot calculate SEG-Y model SHA-256");
    }
    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject source_record(const char* property, const QString& path) {
    const QFileInfo file(path);
    if (!file.isFile()) {
        throw std::invalid_argument(
            std::string(property) + " SEG-Y input is not a regular file");
    }
    return {
        {QStringLiteral("property"), QString::fromUtf8(property)},
        {QStringLiteral("path"), file.canonicalFilePath()},
        {QStringLiteral("byte_count"), file.size()},
        {QStringLiteral("sha256"), file_sha256(file.absoluteFilePath())}};
}

QJsonObject axis_boundary(const AxisBoundary& boundary) {
    return {
        {QStringLiteral("lower"), static_cast<qint64>(boundary.lower_absorbing)},
        {QStringLiteral("upper"), static_cast<qint64>(boundary.upper_absorbing)}};
}

void write_manifest(const QString& path, const QJsonObject& object) {
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        throw std::runtime_error("cannot open SEG-Y conversion manifest");
    }
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        throw std::runtime_error("cannot publish SEG-Y conversion manifest");
    }
}

} // namespace

SegyModelConversionArtifact convert_segy_model_artifact(
    const QString& project_root,
    const SegyModelConversionRequest& request) {
    static const QRegularExpression safe_stem(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$"));
    if (!safe_stem.match(request.output_stem).hasMatch()) {
        throw std::invalid_argument("converted model name is not a safe identifier");
    }
    require_valid_grid_geometry(request.grid);
    if (request.grid.halo == 0) {
        throw std::invalid_argument("converted model halo must be positive");
    }

    const QDir root(project_root);
    const auto model_reference = QStringLiteral("models/") +
                                 request.output_stem + QStringLiteral(".h5");
    const auto manifest_reference = QStringLiteral("manifests/models/") +
                                    request.output_stem + QStringLiteral(".json");
    const auto model_path = root.filePath(model_reference);
    const auto manifest_path = root.filePath(manifest_reference);
    if (QFileInfo::exists(model_path) || QFileInfo::exists(manifest_path)) {
        throw std::invalid_argument("converted model or manifest already exists");
    }
    if (!root.mkpath(QStringLiteral("models")) ||
        !root.mkpath(QStringLiteral("manifests/models"))) {
        throw std::runtime_error("cannot create converted-model directories");
    }

    const QJsonArray sources{
        source_record("vp", request.vp_path),
        source_record("vs", request.vs_path),
        source_record("density", request.density_path)};
    const auto read_property = [&](const QString& path) {
        return io::read_ieee_segy_volume_zyx(
            path.toStdString(),
            request.grid.nx,
            request.grid.ny,
            request.grid.nz);
    };
    const PhysicalModel model{
        request.grid,
        read_property(request.vp_path),
        read_property(request.vs_path),
        read_property(request.density_path)};
    require_valid_physical_model(model);

    const auto temporary_path = root.filePath(
        QStringLiteral("models/.segy-conversion-") +
        QUuid::createUuid().toString(QUuid::WithoutBraces) +
        QStringLiteral(".h5"));
    bool model_published = false;
    try {
        io::write_hdf5_model(temporary_path.toStdString(), model);
        const auto verified = io::read_hdf5_model(temporary_path.toStdString());
        if (!same_grid_geometry(model.grid, verified.grid) ||
            model.vp_m_s != verified.vp_m_s ||
            model.vs_m_s != verified.vs_m_s ||
            model.density_kg_m3 != verified.density_kg_m3) {
            throw std::runtime_error("converted HDF5 verification failed");
        }
        if (!QFile::rename(temporary_path, model_path)) {
            throw std::runtime_error("cannot publish converted HDF5 model");
        }
        model_published = true;
        const auto output_sha256 = file_sha256(model_path);
        const auto& grid = request.grid;
        const QJsonObject manifest{
            {QStringLiteral("schema"),
             QString::fromUtf8(kSegyModelConversionSchema)},
            {QStringLiteral("operation"),
             QStringLiteral("segy_property_volumes_to_hdf5")},
            {QStringLiteral("created_utc"),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("sources"), sources},
            {QStringLiteral("input_contract"),
             QJsonObject{
                 {QStringLiteral("sample_format"),
                  QStringLiteral("IEEE_FLOAT32_BIG_ENDIAN")},
                 {QStringLiteral("trace_order"), QStringLiteral("x_fast_then_y")},
                 {QStringLiteral("sample_axis"), QStringLiteral("z_down")},
                 {QStringLiteral("property_units"),
                  QJsonObject{{QStringLiteral("vp"), QStringLiteral("m/s")},
                              {QStringLiteral("vs"), QStringLiteral("m/s")},
                              {QStringLiteral("density"), QStringLiteral("kg/m3")}}}}},
            {QStringLiteral("output_model"), model_reference},
            {QStringLiteral("output_sha256"), output_sha256},
            {QStringLiteral("dimensions"),
             QJsonObject{{QStringLiteral("nx"), static_cast<qint64>(grid.nx)},
                         {QStringLiteral("ny"), static_cast<qint64>(grid.ny)},
                         {QStringLiteral("nz"), static_cast<qint64>(grid.nz)}}},
            {QStringLiteral("spacing_m"),
             QJsonObject{{QStringLiteral("dx"), grid.dx_m},
                         {QStringLiteral("dy"), grid.dy_m},
                         {QStringLiteral("dz"), grid.dz_m}}},
            {QStringLiteral("halo"), static_cast<qint64>(grid.halo)},
            {QStringLiteral("absorbing_boundary_cells"),
             QJsonObject{{QStringLiteral("x"), axis_boundary(grid.x_boundary)},
                         {QStringLiteral("y"), axis_boundary(grid.y_boundary)},
                         {QStringLiteral("z"), axis_boundary(grid.z_boundary)}}},
            {QStringLiteral("output_axes"), QStringLiteral("z,y,x")},
            {QStringLiteral("coordinate_convention"),
             QStringLiteral("x=east,y=north,z=down; local origin=(0,0,0)")}};
        write_manifest(manifest_path, manifest);
        return {model_reference, manifest_reference, output_sha256};
    } catch (...) {
        QFile::remove(temporary_path);
        if (model_published) {
            QFile::remove(model_path);
        }
        QFile::remove(manifest_path);
        throw;
    }
}

} // namespace wave3d::desktop
