#include "wave3d/desktop/project_workspace.hpp"
#include "wave3d/desktop/segy_model_conversion.hpp"
#include "wave3d/io/hdf5.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void put_u16(
    std::vector<unsigned char>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    bytes[offset] = static_cast<unsigned char>(value >> 8U);
    bytes[offset + 1] = static_cast<unsigned char>(value);
}

void put_float(std::ofstream& output, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const unsigned char bytes[4]{
        static_cast<unsigned char>(bits >> 24U),
        static_cast<unsigned char>(bits >> 16U),
        static_cast<unsigned char>(bits >> 8U),
        static_cast<unsigned char>(bits)};
    output.write(reinterpret_cast<const char*>(bytes), 4);
}

void write_volume(
    const QString& path,
    std::size_t trace_count,
    std::size_t sample_count,
    const std::vector<float>& values) {
    std::vector<unsigned char> headers(3600, 0);
    std::fill(
        headers.begin(),
        headers.begin() + 3200,
        static_cast<unsigned char>(' '));
    put_u16(headers, 3200 + 20, static_cast<std::uint16_t>(sample_count));
    put_u16(headers, 3200 + 24, 5);
    std::ofstream output(path.toStdString(), std::ios::binary);
    output.write(reinterpret_cast<const char*>(headers.data()), headers.size());
    for (std::size_t trace = 0; trace < trace_count; ++trace) {
        std::vector<unsigned char> trace_header(240, 0);
        put_u16(trace_header, 114, static_cast<std::uint16_t>(sample_count));
        output.write(
            reinterpret_cast<const char*>(trace_header.data()), trace_header.size());
        for (std::size_t sample = 0; sample < sample_count; ++sample) {
            put_float(output, values[trace * sample_count + sample]);
        }
    }
    expect(output.good(), "cannot write SEG-Y fixture");
}

QString sha256(const QString& path) {
    QFile input(path);
    expect(input.open(QIODevice::ReadOnly), "cannot open checksum fixture");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    expect(hash.addData(&input), "cannot hash fixture");
    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject read_object(const QString& path) {
    QFile input(path);
    expect(input.open(QIODevice::ReadOnly), "cannot open conversion manifest");
    const auto document = QJsonDocument::fromJson(input.readAll());
    expect(document.isObject(), "conversion manifest is not an object");
    return document.object();
}

void test_conversion_contract() {
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary directory failed");
    const auto root = QDir(temporary.path()).filePath(QStringLiteral("project"));
    static_cast<void>(wave3d::desktop::ProjectWorkspace::create(
        root, QStringLiteral("SEG-Y conversion")));
    const auto vp_path = QDir(temporary.path()).filePath(QStringLiteral("vp.sgy"));
    const auto vs_path = QDir(temporary.path()).filePath(QStringLiteral("vs.sgy"));
    const auto rho_path = QDir(temporary.path()).filePath(QStringLiteral("rho.sgy"));
    const std::vector<float> vp{
        3000, 3001, 3010, 3011, 3020, 3021, 3030, 3031};
    const std::vector<float> vs{
        1500, 1501, 1510, 1511, 1520, 1521, 1530, 1531};
    const std::vector<float> rho{
        2200, 2201, 2210, 2211, 2220, 2221, 2230, 2231};
    write_volume(vp_path, 4, 2, vp);
    write_volume(vs_path, 4, 2, vs);
    write_volume(rho_path, 4, 2, rho);

    const wave3d::Grid3D grid{
        2, 2, 2, 10.0F, 20.0F, 30.0F, 6,
        {5, 6}, {7, 8}, {0, 9}};
    const wave3d::desktop::SegyModelConversionRequest request{
        vp_path, vs_path, rho_path, QStringLiteral("converted_001"), grid};
    const auto artifact =
        wave3d::desktop::convert_segy_model_artifact(root, request);
    const auto model_path = QDir(root).filePath(artifact.model_reference);
    const auto manifest_path = QDir(root).filePath(artifact.manifest_reference);
    const auto model = wave3d::io::read_hdf5_model(model_path.toStdString());
    expect(
        wave3d::same_grid_geometry(model.grid, grid) &&
            model.vp_m_s == std::vector<float>({3000, 3010, 3020, 3030,
                                                3001, 3011, 3021, 3031}) &&
            model.vs_m_s.front() == 1500 && model.density_kg_m3.back() == 2231,
        "SEG-Y trace-major values did not become canonical [z][y][x]");
    const auto manifest = read_object(manifest_path);
    const auto sources = manifest.value(QStringLiteral("sources")).toArray();
    expect(
        manifest.value(QStringLiteral("schema")).toString() ==
                QString::fromUtf8(wave3d::desktop::kSegyModelConversionSchema) &&
            manifest.value(QStringLiteral("output_sha256")).toString() ==
                sha256(model_path) &&
            sources.size() == 3 &&
            sources[0].toObject().value(QStringLiteral("sha256")).toString() ==
                sha256(vp_path) &&
            manifest.value(QStringLiteral("input_contract")).toObject()
                    .value(QStringLiteral("trace_order")).toString() ==
                QStringLiteral("x_fast_then_y"),
        "SEG-Y conversion provenance is incomplete");

    bool rejected = false;
    try {
        static_cast<void>(wave3d::desktop::convert_segy_model_artifact(root, request));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "conversion must refuse an output collision");

    auto mismatch = request;
    mismatch.output_stem = QStringLiteral("mismatch");
    mismatch.grid.nz = 3;
    rejected = false;
    try {
        static_cast<void>(wave3d::desktop::convert_segy_model_artifact(root, mismatch));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(
        rejected &&
            !QFile::exists(QDir(root).filePath(
                QStringLiteral("models/mismatch.h5"))) &&
            !QFile::exists(QDir(root).filePath(
                QStringLiteral("manifests/models/mismatch.json"))),
        "mismatched SEG-Y layout published a partial artifact");
}

} // namespace

int main() {
    try {
        test_conversion_contract();
        std::cout << "desktop SEG-Y model conversion tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "desktop SEG-Y model conversion test failed: " << error.what() << '\n';
        return 1;
    }
}
