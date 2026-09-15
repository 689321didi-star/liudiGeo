#include "wave3d/desktop/model_derivation.hpp"

#include "wave3d/io/hdf5.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <stdexcept>

namespace wave3d::desktop {
namespace {

QString file_sha256(const QString& path) {
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("cannot open model for SHA-256");
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input)) {
        throw std::runtime_error("cannot calculate model SHA-256");
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool safe_model_reference(const QString& reference) {
    return !reference.isEmpty() && !QDir::isAbsolutePath(reference) &&
           !reference.contains(QLatin1Char('\\')) &&
           QDir::cleanPath(reference) == reference &&
           reference.startsWith(QStringLiteral("models/")) &&
           !reference.startsWith(QStringLiteral("models/../"));
}

QJsonObject axis_bounds(
    std::size_t begin,
    std::size_t end,
    float spacing_m) {
    return {
        {QStringLiteral("begin"), static_cast<qint64>(begin)},
        {QStringLiteral("end_exclusive"), static_cast<qint64>(end)},
        {QStringLiteral("origin_offset_m"),
         static_cast<double>(begin) * spacing_m}};
}

void write_manifest(const QString& path, const QJsonObject& object) {
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        throw std::runtime_error("cannot open derivation manifest");
    }
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        throw std::runtime_error("cannot atomically publish derivation manifest");
    }
}

} // namespace

DerivedModelArtifact create_cropped_model_artifact(
    const QString& project_root,
    const QString& source_model_reference,
    const StaticModelScene& source_scene,
    const ModelCropBounds& bounds,
    const QString& output_stem) {
    static const QRegularExpression safe_stem(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$"));
    if (!safe_stem.match(output_stem).hasMatch()) {
        throw std::invalid_argument("derived model name is not a safe identifier");
    }
    if (!safe_model_reference(source_model_reference)) {
        throw std::invalid_argument("source model reference is not safe");
    }
    const QDir root(project_root);
    const auto source_path = root.filePath(source_model_reference);
    if (QFileInfo(source_path).canonicalFilePath() !=
        QFileInfo(source_scene.source_path()).canonicalFilePath()) {
        throw std::invalid_argument("source scene does not match project reference");
    }

    const auto model_reference =
        QStringLiteral("models/") + output_stem + QStringLiteral(".h5");
    const auto manifest_reference = QStringLiteral("manifests/models/") +
                                    output_stem + QStringLiteral(".json");
    const auto model_path = root.filePath(model_reference);
    const auto manifest_path = root.filePath(manifest_reference);
    if (QFileInfo::exists(model_path) || QFileInfo::exists(manifest_path)) {
        throw std::invalid_argument("derived model or manifest already exists");
    }
    if (!root.mkpath(QStringLiteral("manifests/models"))) {
        throw std::runtime_error("cannot create model manifest directory");
    }

    const auto temporary_path = root.filePath(
        QStringLiteral("models/.crop-") +
        QUuid::createUuid().toString(QUuid::WithoutBraces) +
        QStringLiteral(".h5"));
    bool model_published = false;
    try {
        const auto cropped = source_scene.cropped_model(bounds);
        io::write_hdf5_model(temporary_path.toStdString(), cropped);
        const auto verified = io::read_hdf5_model(temporary_path.toStdString());
        if (!same_grid_geometry(cropped.grid, verified.grid) ||
            cropped.vp_m_s != verified.vp_m_s ||
            cropped.vs_m_s != verified.vs_m_s ||
            cropped.density_kg_m3 != verified.density_kg_m3) {
            throw std::runtime_error("derived HDF5 verification failed");
        }
        if (!QFile::rename(temporary_path, model_path)) {
            throw std::runtime_error("cannot atomically publish derived model");
        }
        model_published = true;

        const auto source_sha256 = file_sha256(source_path);
        const auto output_sha256 = file_sha256(model_path);
        const auto& grid = cropped.grid;
        const QJsonObject manifest{
            {QStringLiteral("schema"),
             QString::fromUtf8(kModelDerivationSchema)},
            {QStringLiteral("operation"), QStringLiteral("crop")},
            {QStringLiteral("created_utc"),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("source_model"), source_model_reference},
            {QStringLiteral("source_sha256"), source_sha256},
            {QStringLiteral("output_model"), model_reference},
            {QStringLiteral("output_sha256"), output_sha256},
            {QStringLiteral("coordinate_origin"),
             QStringLiteral("local_zero_with_source_offset")},
            {QStringLiteral("bounds"),
             QJsonObject{
                 {QStringLiteral("x"), axis_bounds(
                      bounds.x_begin, bounds.x_end, grid.dx_m)},
                 {QStringLiteral("y"), axis_bounds(
                      bounds.y_begin, bounds.y_end, grid.dy_m)},
                 {QStringLiteral("z"), axis_bounds(
                      bounds.z_begin, bounds.z_end, grid.dz_m)}}},
            {QStringLiteral("dimensions"),
             QJsonObject{
                 {QStringLiteral("nx"), static_cast<qint64>(grid.nx)},
                 {QStringLiteral("ny"), static_cast<qint64>(grid.ny)},
                 {QStringLiteral("nz"), static_cast<qint64>(grid.nz)}}},
            {QStringLiteral("spacing_m"),
             QJsonObject{
                 {QStringLiteral("dx"), grid.dx_m},
                 {QStringLiteral("dy"), grid.dy_m},
                 {QStringLiteral("dz"), grid.dz_m}}}};
        write_manifest(manifest_path, manifest);
        return {
            model_reference,
            manifest_reference,
            source_sha256,
            output_sha256};
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
