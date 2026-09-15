#include "wave3d/desktop/model_derivation.hpp"

#include "wave3d/desktop/project_workspace.hpp"
#include "wave3d/io/hdf5.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QString digest(const QString& path) {
    QFile input(path);
    expect(input.open(QIODevice::ReadOnly), "cannot read digest input");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    expect(hash.addData(&input), "cannot hash test input");
    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject object_from_file(const QString& path) {
    QFile input(path);
    expect(input.open(QIODevice::ReadOnly), "cannot read manifest");
    const auto document = QJsonDocument::fromJson(input.readAll());
    expect(document.isObject(), "manifest is not an object");
    return document.object();
}

wave3d::PhysicalModel fixture() {
    const wave3d::Grid3D grid{
        5, 4, 3,
        10.0F, 20.0F, 30.0F,
        6,
        {2, 2}, {3, 3}, {0, 4}};
    auto model = wave3d::make_homogeneous_model(
        grid, {3600.0F, 2000.0F, 2400.0F});
    for (std::size_t index = 0; index < model.cell_count(); ++index) {
        model.vp_m_s[index] += static_cast<float>(index);
        model.vs_m_s[index] += static_cast<float>(index) * 0.25F;
        model.density_kg_m3[index] += static_cast<float>(index) * 0.5F;
    }
    return model;
}

void test_crop_artifact_contract() {
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary project directory failed");
    const auto root = QDir(temporary.path()).filePath(QStringLiteral("project"));
    static_cast<void>(wave3d::desktop::ProjectWorkspace::create(
        root, QStringLiteral("Crop provenance")));
    const auto source_reference = QStringLiteral("models/source.h5");
    const auto source_path = QDir(root).filePath(source_reference);
    wave3d::io::write_hdf5_model(source_path.toStdString(), fixture());
    const auto scene = wave3d::desktop::StaticModelScene::load_hdf5(source_path);
    const wave3d::desktop::ModelCropBounds bounds{1, 5, 1, 4, 0, 2};
    const auto artifact = wave3d::desktop::create_cropped_model_artifact(
        root, source_reference, scene, bounds, QStringLiteral("crop_001"));

    const auto output_path = QDir(root).filePath(artifact.model_reference);
    const auto manifest_path = QDir(root).filePath(artifact.manifest_reference);
    expect(
        artifact.model_reference == QStringLiteral("models/crop_001.h5") &&
            artifact.manifest_reference ==
                QStringLiteral("manifests/models/crop_001.json"),
        "derived artifact references changed");
    expect(
        artifact.source_sha256 == digest(source_path) &&
            artifact.output_sha256 == digest(output_path),
        "derived artifact SHA-256 values are incorrect");
    const auto output = wave3d::io::read_hdf5_model(output_path.toStdString());
    expect(
        output.grid.nx == 4 && output.grid.ny == 3 && output.grid.nz == 2,
        "derived HDF5 dimensions changed");

    const auto manifest = object_from_file(manifest_path);
    expect(
        manifest.value(QStringLiteral("schema")).toString() ==
                QString::fromUtf8(wave3d::desktop::kModelDerivationSchema) &&
            manifest.value(QStringLiteral("source_sha256")).toString() ==
                artifact.source_sha256 &&
            manifest.value(QStringLiteral("output_sha256")).toString() ==
                artifact.output_sha256,
        "derivation manifest identity or checksum changed");
    const auto axes = manifest.value(QStringLiteral("bounds")).toObject();
    expect(
        axes.value(QStringLiteral("x")).toObject()
                    .value(QStringLiteral("begin"))
                    .toInt() == 1 &&
            axes.value(QStringLiteral("x")).toObject()
                    .value(QStringLiteral("end_exclusive"))
                    .toInt() == 5 &&
            axes.value(QStringLiteral("y")).toObject()
                    .value(QStringLiteral("origin_offset_m"))
                    .toDouble() == 20.0 &&
            axes.value(QStringLiteral("z")).toObject()
                    .value(QStringLiteral("origin_offset_m"))
                    .toDouble() == 0.0,
        "crop bounds or reversible origin offsets changed");

    bool rejected = false;
    try {
        static_cast<void>(wave3d::desktop::create_cropped_model_artifact(
            root, source_reference, scene, bounds, QStringLiteral("crop_001")));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "existing derived model must not be overwritten");
    rejected = false;
    try {
        static_cast<void>(wave3d::desktop::create_cropped_model_artifact(
            root, source_reference, scene, bounds, QStringLiteral("../bad")));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "unsafe derived model name must be rejected");
    rejected = false;
    try {
        static_cast<void>(wave3d::desktop::create_cropped_model_artifact(
            root,
            source_reference,
            scene,
            {1, 1, 0, 2, 0, 2},
            QStringLiteral("invalid_bounds")));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "invalid crop bounds must be rejected");
    expect(
        !QFileInfo::exists(
            QDir(root).filePath(QStringLiteral("models/invalid_bounds.h5"))) &&
            !QFileInfo::exists(QDir(root).filePath(
                QStringLiteral("manifests/models/invalid_bounds.json"))) &&
            QDir(QDir(root).filePath(QStringLiteral("models")))
                .entryList(
                    {QStringLiteral(".crop-*.h5")},
                    QDir::Files | QDir::Hidden)
                .isEmpty(),
        "failed crop derivation left temporary or published artifacts");
    expect(
        digest(source_path) == artifact.source_sha256,
        "crop derivation changed the source model bytes");
}

} // namespace

int main() {
    try {
        test_crop_artifact_contract();
        std::cout << "desktop model derivation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "desktop model derivation test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
