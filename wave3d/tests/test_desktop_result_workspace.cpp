#include "wave3d/desktop/project_workspace.hpp"
#include "wave3d/desktop/result_workspace.hpp"
#include "wave3d/io/segy.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <array>
#include <iostream>
#include <stdexcept>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QString sha256(const QString& path) {
    QFile input(path);
    expect(input.open(QIODevice::ReadOnly), "cannot hash SEG-Y fixture");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    expect(hash.addData(&input), "cannot consume SEG-Y fixture hash");
    return QString::fromLatin1(hash.result().toHex());
}

wave3d::io::ThreeComponentTraces traces() {
    wave3d::MomentTensorSource source{};
    source.physical_location = {100.0, 200.0, 300.0};
    source.storage_location = {16.0, 26.0, 36.0};
    source.origin_time_s = 0.0;
    source.moment = wave3d::isotropic_explosion(1.0e12);
    source.wavelet = {25.0, 0.04, 1.0};
    wave3d::io::ThreeComponentTraces result{
        6,
        8,
        0.001,
        {{0.0, 10.0, 0.0},
         {10.0, 10.0, 0.0},
         {20.0, 10.0, 0.0},
         {30.0, 10.0, 0.0},
         {40.0, 10.0, 0.0},
         {50.0, 10.0, 0.0}},
        source,
        std::vector<float>(48),
        std::vector<float>(48),
        std::vector<float>(48)};
    for (std::size_t receiver = 0; receiver < result.receiver_count; ++receiver) {
        for (std::size_t sample = 0; sample < result.sample_count; ++sample) {
            const auto index = receiver * result.sample_count + sample;
            const auto base = static_cast<float>((receiver + 1) * (sample + 1));
            result.vx_m_s[index] = base * 1.0e-6F;
            result.vy_m_s[index] = -base * 2.0e-6F;
            result.vz_m_s[index] = base * 3.0e-6F;
        }
    }
    return result;
}

void test_completed_triplet_is_inspected_and_exported() {
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary directory creation failed");
    const auto root = QDir(temporary.path()).filePath(QStringLiteral("project"));
    const auto project = wave3d::desktop::ProjectWorkspace::create(
        root, QStringLiteral("结果工作区测试"));
    const auto run = wave3d::desktop::ProjectWorkspace::prepare_run(
        root,
        project,
        QStringLiteral("run-001"),
        QStringLiteral("shot-001"),
        QByteArray("schema: wave3d.forward.v2\n"));

    const auto fixture = traces();
    const std::array components{
        wave3d::io::SegyComponent::Vx,
        wave3d::io::SegyComponent::Vy,
        wave3d::io::SegyComponent::Vz};
    const std::array names{
        QStringLiteral("vx"), QStringLiteral("vy"), QStringLiteral("vz")};
    wave3d::desktop::RunProduct product;
    for (std::size_t index = 0; index < components.size(); ++index) {
        const auto relative = QStringLiteral("output/record_%1.sgy").arg(names[index]);
        const auto absolute = QDir(run.directory).filePath(relative);
        wave3d::io::write_component_segy(
            absolute.toStdString(), fixture, components[index]);
        product.files.push_back({
            names[index], relative, sha256(absolute), QFileInfo(absolute).size()});
    }
    product.receiver_count = fixture.receiver_count;
    product.sample_count = fixture.sample_count;
    product.device_name = QStringLiteral("fixture GPU");
    product.propagation_ms = 12.5;
    static_cast<void>(wave3d::desktop::ProjectWorkspace::publish_run_result(
        run,
        {wave3d::desktop::RunTerminalState::Completed, QString(), product}));

    const auto bad_directory =
        QDir(root).filePath(QStringLiteral("runs/run-invalid"));
    expect(QDir().mkpath(bad_directory), "cannot create invalid result fixture");
    QFile bad_result(QDir(bad_directory).filePath(QStringLiteral("result.json")));
    expect(
        bad_result.open(QIODevice::WriteOnly) &&
            bad_result.write(QJsonDocument(QJsonObject{
                {QStringLiteral("schema"),
                 QString::fromUtf8(wave3d::desktop::kDesktopRunResultSchema)},
                {QStringLiteral("state"), QStringLiteral("completed")}})
                                 .toJson()) > 0,
        "cannot create invalid result record");
    bad_result.close();

    const auto before_vx = sha256(QDir(run.directory).filePath(
        QStringLiteral("output/record_vx.sgy")));
    const auto before_result = sha256(QDir(run.directory).filePath(
        QStringLiteral("result.json")));

    wave3d::desktop::ResultWorkspace workspace;
    workspace.set_project_root(root);
    expect(
        workspace.completed_run_count() == 1 &&
            workspace.selected_run_id() == QStringLiteral("run-001") &&
            !workspace.current_gather_image().isNull(),
        "result workspace did not discover and open the valid completed run");
    expect(
        workspace.current_header_report().contains(QStringLiteral("Revision 1")) &&
            workspace.current_header_report().contains(
                QStringLiteral("Trace identification code 13")),
        "result workspace did not report Vx SEG-Y metadata");

    QString error;
    expect(
        workspace.set_component(QStringLiteral("vy"), &error) &&
            workspace.set_receiver_window(2, 3, &error),
        "component or receiver selection failed");
    expect(
        workspace.property("resultGatherComponent").toString() ==
                QStringLiteral("vy") &&
            workspace.property("resultGatherReceiverCount").toULongLong() == 3 &&
            workspace.current_header_report().contains(
                QStringLiteral("Trace identification code 14")) &&
            workspace.current_header_report().contains(
                QStringLiteral("首个接收器 #2")) &&
            workspace.current_header_report().contains(
                QStringLiteral("末个接收器 #4")),
        "selected component/window metadata is incorrect");

    const auto export_path = QDir(temporary.path()).filePath(
        QStringLiteral("selected_gather.png"));
    expect(
        workspace.export_current_png(export_path, &error) &&
            QFileInfo(export_path).isFile() && QFileInfo(export_path).size() > 0,
        "PNG result export failed");
    expect(
        !workspace.export_current_png(
            QDir(temporary.path()).filePath(QStringLiteral("invalid.svg")), &error),
        "non-PNG result export was accepted");
    expect(
        sha256(QDir(run.directory).filePath(
            QStringLiteral("output/record_vx.sgy"))) == before_vx &&
            sha256(QDir(run.directory).filePath(
                QStringLiteral("result.json"))) == before_result,
        "result inspection or export modified immutable project data");

    const auto vy_path = QDir(run.directory).filePath(
        QStringLiteral("output/record_vy.sgy"));
    QFile changed_component(vy_path);
    expect(
        changed_component.open(QIODevice::ReadWrite) &&
            changed_component.seek(3600 + 28) &&
            changed_component.write(QByteArray::fromHex("000d")) == 2,
        "cannot create mismatched component-code fixture");
    changed_component.close();
    workspace.set_project_root(root);
    expect(
        !workspace.select_run(QStringLiteral("run-001"), &error),
        "result workspace accepted a mismatched component code");
    expect(
        changed_component.open(QIODevice::ReadWrite) &&
            changed_component.seek(3600 + 28) &&
            changed_component.write(QByteArray::fromHex("000e")) == 2,
        "cannot restore component-code fixture");
    changed_component.close();
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    try {
        test_completed_triplet_is_inspected_and_exported();
        std::cout << "desktop SEG-Y result workspace tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
