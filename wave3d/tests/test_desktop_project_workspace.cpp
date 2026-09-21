#include "wave3d/desktop/project_workspace.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Operation>
void expect_rejected(Operation operation, const char* message) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, message);
}

QJsonObject read_object(const QString& path) {
    QFile input(path);
    expect(input.open(QIODevice::ReadOnly), "cannot read test JSON");
    const auto document = QJsonDocument::fromJson(input.readAll());
    expect(document.isObject(), "test JSON is not an object");
    return document.object();
}

void write_object(const QString& path, const QJsonObject& object) {
    QFile output(path);
    expect(
        output.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "cannot write test JSON");
    expect(
        output.write(QJsonDocument(object).toJson()) > 0,
        "cannot populate test JSON");
}

void test_project_round_trip_and_run_contract() {
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary directory creation failed");
    const auto root = QDir(temporary.path()).filePath(QStringLiteral("research"));
    auto project = wave3d::desktop::ProjectWorkspace::create(
        root, QStringLiteral("Overthrust 正演实验"));

    expect(
        project.project_id.startsWith(QStringLiteral("project-")),
        "new project has no stable identity");
    expect(project.shots.size() == 1, "new project must start with one shot");
    expect(
        project.shots.front().id == QStringLiteral("shot-001"),
        "default shot identity changed");
    expect(
        project.display_field == QStringLiteral("speed"),
        "new project display field must default to speed");

    const QDir directory(root);
    for (const auto* relative : {
             "source", "models", "figures/model", "runs", "manifests"}) {
        expect(
            QFileInfo(directory.filePath(QString::fromUtf8(relative))).isDir(),
            "standard project directory is missing");
    }
    const auto document_path =
        directory.filePath(QString::fromUtf8(wave3d::desktop::kDesktopProjectFile));
    expect(QFileInfo::exists(document_path), "project document is missing");
    expect(
        read_object(document_path).value(QStringLiteral("schema")).toString() ==
            QString::fromUtf8(wave3d::desktop::kDesktopProjectSchema),
        "project schema identity changed");

    project.model_reference = QStringLiteral("models/elastic_model.h5");
    project.queue_enabled = true;
    project.display_field = QStringLiteral("vz");
    project.shots.push_back(
        {QStringLiteral("shot-002"), QStringLiteral("炮 2")});
    wave3d::desktop::ProjectWorkspace::save(root, project);
    const auto loaded = wave3d::desktop::ProjectWorkspace::load(root);
    expect(loaded.project_id == project.project_id, "project ID did not round trip");
    expect(loaded.name == project.name, "project name did not round trip");
    expect(
        loaded.model_reference == project.model_reference,
        "model reference did not round trip");
    expect(loaded.queue_enabled, "queue preference did not round trip");
    expect(
        loaded.display_field == QStringLiteral("vz"),
        "display field did not round trip");
    expect(loaded.shots.size() == 2, "shot table did not round trip");

    const QByteArray configuration(
        "schema: wave3d.forward.v2\noutput_directory: output\n");
    const auto run = wave3d::desktop::ProjectWorkspace::prepare_run(
        root,
        loaded,
        QStringLiteral("forward_001"),
        QStringLiteral("shot-002"),
        configuration);
    expect(QFileInfo(run.directory).isDir(), "run directory is missing");
    for (const auto* relative : {"output", "figures", "logs", "reports"}) {
        expect(
            QFileInfo(QDir(run.directory).filePath(QString::fromUtf8(relative)))
                .isDir(),
            "run product directory is missing");
    }
    QFile retained(run.configuration_path);
    expect(retained.open(QIODevice::ReadOnly), "retained config cannot be read");
    expect(
        retained.readAll() == configuration,
        "resolved YAML bytes must be retained exactly");
    const auto expected_digest = QString::fromLatin1(
        QCryptographicHash::hash(configuration, QCryptographicHash::Sha256)
            .toHex());
    expect(
        run.configuration_sha256 == expected_digest,
        "reported configuration digest is incorrect");
    const auto manifest = read_object(run.manifest_path);
    expect(
        manifest.value(QStringLiteral("schema")).toString() ==
            QString::fromUtf8(wave3d::desktop::kDesktopRunSchema),
        "run manifest schema identity changed");
    expect(
        manifest.value(QStringLiteral("project_id")).toString() ==
                loaded.project_id &&
            manifest.value(QStringLiteral("shot_id")).toString() ==
                QStringLiteral("shot-002") &&
            manifest.value(QStringLiteral("configuration_sha256")).toString() ==
                expected_digest,
        "run manifest identity or digest is incorrect");

    wave3d::desktop::RunProduct product;
    for (const auto* component : {"vx", "vy", "vz"}) {
        const auto relative_path = QStringLiteral("output/record_%1.sgy")
                                       .arg(QString::fromUtf8(component));
        const auto product_bytes =
            QByteArray("segy-fixture-") + QByteArray(component);
        QFile product_file(QDir(run.directory).filePath(relative_path));
        expect(
            product_file.open(QIODevice::WriteOnly),
            "cannot create result fixture");
        expect(
            product_file.write(product_bytes) == product_bytes.size(),
            "cannot write result fixture");
        product_file.close();
        product.files.push_back({
            QString::fromUtf8(component),
            relative_path,
            QString::fromLatin1(
                QCryptographicHash::hash(
                    product_bytes, QCryptographicHash::Sha256)
                    .toHex()),
            product_bytes.size()});
    }
    product.receiver_count = 12;
    product.sample_count = 40;
    product.device_name = QStringLiteral("fixture GPU");
    product.propagation_ms = 25.0;

    auto incomplete_product = product;
    incomplete_product.files.removeLast();
    expect_rejected(
        [&] {
            static_cast<void>(
                wave3d::desktop::ProjectWorkspace::publish_run_result(
                    run,
                    {wave3d::desktop::RunTerminalState::Completed,
                     QString(),
                     incomplete_product}));
        },
        "completed product accepted a missing component file");
    auto duplicated_product = product;
    duplicated_product.files[2].component = QStringLiteral("vx");
    expect_rejected(
        [&] {
            static_cast<void>(
                wave3d::desktop::ProjectWorkspace::publish_run_result(
                    run,
                    {wave3d::desktop::RunTerminalState::Completed,
                     QString(),
                     duplicated_product}));
        },
        "completed product accepted a duplicate component label");
    auto mislabeled_product = product;
    mislabeled_product.files[2].component = QStringLiteral("pressure");
    expect_rejected(
        [&] {
            static_cast<void>(
                wave3d::desktop::ProjectWorkspace::publish_run_result(
                    run,
                    {wave3d::desktop::RunTerminalState::Completed,
                     QString(),
                     mislabeled_product}));
        },
        "completed product accepted an unsupported component label");
    auto swapped_product = product;
    std::swap(
        swapped_product.files[0].relative_path,
        swapped_product.files[1].relative_path);
    expect_rejected(
        [&] {
            static_cast<void>(
                wave3d::desktop::ProjectWorkspace::publish_run_result(
                    run,
                    {wave3d::desktop::RunTerminalState::Completed,
                     QString(),
                     swapped_product}));
        },
        "completed product accepted mislabeled component paths");
    const auto first_product_path =
        QDir(run.directory).filePath(product.files.front().relative_path);
    QFile changed_product(first_product_path);
    expect(
        changed_product.open(QIODevice::Append) &&
            changed_product.write("changed") == 7,
        "cannot mutate result fixture");
    changed_product.close();
    expect_rejected(
        [&] {
            static_cast<void>(
                wave3d::desktop::ProjectWorkspace::publish_run_result(
                    run,
                    {wave3d::desktop::RunTerminalState::Completed,
                     QString(),
                     product}));
        },
        "completed product accepted a changed component file");
    expect(
        changed_product.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
            changed_product.write("segy-fixture-vx") == 15,
        "cannot restore result fixture");
    changed_product.close();
    const auto result_path = wave3d::desktop::ProjectWorkspace::publish_run_result(
        run,
        {wave3d::desktop::RunTerminalState::Completed, QString(), product});
    const auto result = read_object(result_path);
    const auto result_product =
        result.value(QStringLiteral("product")).toObject();
    const auto result_files =
        result_product.value(QStringLiteral("files")).toArray();
    expect(
        result.value(QStringLiteral("schema")).toString() ==
                QString::fromUtf8(wave3d::desktop::kDesktopRunResultSchema) &&
            result.value(QStringLiteral("state")).toString() ==
                QStringLiteral("completed") &&
            result_product.value(QStringLiteral("kind")).toString() ==
                QStringLiteral("segy_rev1_three_component_files") &&
            result_files.size() == 3 &&
            result_files.at(0)
                    .toObject()
                    .value(QStringLiteral("sha256"))
                    .toString() == product.files.front().sha256,
        "terminal run result did not preserve product identity");
    expect_rejected(
        [&] {
            static_cast<void>(
                wave3d::desktop::ProjectWorkspace::publish_run_result(
                    run,
                    {wave3d::desktop::RunTerminalState::Completed,
                     QString(),
                     product}));
        },
        "terminal run result must never be overwritten");

    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ProjectWorkspace::prepare_run(
                root,
                loaded,
                QStringLiteral("forward_001"),
                QStringLiteral("shot-002"),
                QByteArray("different")));
        },
        "an existing run ID must never be overwritten");
    retained.close();
    expect(retained.open(QIODevice::ReadOnly), "retained config disappeared");
    expect(
        retained.readAll() == configuration,
        "rejected run preparation changed immutable configuration bytes");

    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ProjectWorkspace::prepare_run(
                root,
                loaded,
                QStringLiteral("../escape"),
                QStringLiteral("shot-001"),
                configuration));
        },
        "unsafe run identity must be rejected");
    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ProjectWorkspace::prepare_run(
                root,
                loaded,
                QStringLiteral("unknown_shot"),
                QStringLiteral("shot-999"),
                configuration));
        },
        "run with unknown shot identity must be rejected");
    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ProjectWorkspace::prepare_run(
                root,
                loaded,
                QStringLiteral("empty_config"),
                QStringLiteral("shot-001"),
                QByteArray{}));
        },
        "empty resolved configuration must be rejected");

    const auto cancelled = wave3d::desktop::ProjectWorkspace::prepare_run(
        root,
        loaded,
        QStringLiteral("cancelled_001"),
        QStringLiteral("shot-001"),
        configuration);
    const auto cancelled_path =
        wave3d::desktop::ProjectWorkspace::publish_run_result(
            cancelled,
            {wave3d::desktop::RunTerminalState::Cancelled,
             QStringLiteral("用户停止"),
             std::nullopt});
    expect(
        read_object(cancelled_path).value(QStringLiteral("state")).toString() ==
            QStringLiteral("cancelled"),
        "cancelled run result state is incorrect");

    const auto failed = wave3d::desktop::ProjectWorkspace::prepare_run(
        root,
        loaded,
        QStringLiteral("failed_001"),
        QStringLiteral("shot-001"),
        configuration);
    expect_rejected(
        [&] {
            static_cast<void>(
                wave3d::desktop::ProjectWorkspace::publish_run_result(
                    failed,
                    {wave3d::desktop::RunTerminalState::Failed,
                     QString(),
                     std::nullopt}));
        },
        "failed run result without a diagnostic must be rejected");
    expect(
        !QFileInfo::exists(directory.filePath(QStringLiteral("escape"))) &&
            !QFileInfo::exists(directory.filePath(QStringLiteral("runs/unknown_shot"))) &&
            !QFileInfo::exists(directory.filePath(QStringLiteral("runs/empty_config"))),
        "rejected run preparation must not leave a run directory");
}

void test_validation_and_workspace_rejection() {
    wave3d::desktop::ProjectDocument project;
    project.project_id = QStringLiteral("project-test");
    project.name = QStringLiteral("Test");
    project.created_utc = QStringLiteral("2026-09-15T08:00:00.000Z");
    project.shots.push_back({QStringLiteral("shot-001"), QStringLiteral("炮 1")});

    auto invalid = project;
    invalid.model_reference = QStringLiteral("../outside.h5");
    expect_rejected(
        [&] { wave3d::desktop::ProjectWorkspace::validate(invalid); },
        "parent traversal model reference must be rejected");
    invalid = project;
    invalid.model_reference = QStringLiteral("/absolute/model.h5");
    expect_rejected(
        [&] { wave3d::desktop::ProjectWorkspace::validate(invalid); },
        "absolute model reference must be rejected");
    invalid = project;
    invalid.model_reference = QStringLiteral(".");
    expect_rejected(
        [&] { wave3d::desktop::ProjectWorkspace::validate(invalid); },
        "project root cannot be used as a model file reference");
    invalid = project;
    invalid.display_field = QStringLiteral("pressure");
    expect_rejected(
        [&] { wave3d::desktop::ProjectWorkspace::validate(invalid); },
        "unknown display field must be rejected");
    invalid = project;
    invalid.shots.push_back(invalid.shots.front());
    expect_rejected(
        [&] { wave3d::desktop::ProjectWorkspace::validate(invalid); },
        "duplicate shot identity must be rejected");

    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary directory creation failed");
    const auto occupied = QDir(temporary.path()).filePath(QStringLiteral("occupied"));
    expect(QDir().mkpath(occupied), "cannot create occupied test directory");
    QFile marker(QDir(occupied).filePath(QStringLiteral("keep.txt")));
    expect(marker.open(QIODevice::WriteOnly), "cannot create marker file");
    marker.write("keep");
    marker.close();
    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ProjectWorkspace::create(
                occupied, QStringLiteral("Must fail")));
        },
        "new project must not overwrite an occupied directory");
    expect(QFileInfo::exists(marker.fileName()), "occupied directory was modified");

    const auto corrupt = QDir(temporary.path()).filePath(QStringLiteral("corrupt"));
    static_cast<void>(wave3d::desktop::ProjectWorkspace::create(
        corrupt, QStringLiteral("Corrupt test")));
    const auto project_path =
        QDir(corrupt).filePath(QString::fromUtf8(wave3d::desktop::kDesktopProjectFile));
    auto object = read_object(project_path);
    object.insert(QStringLiteral("schema"), QStringLiteral("wave3d.desktop.project.v0"));
    write_object(project_path, object);
    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ProjectWorkspace::load(corrupt));
        },
        "unknown project schema must be rejected");
    object.insert(
        QStringLiteral("schema"),
        QString::fromUtf8(wave3d::desktop::kDesktopProjectSchema));
    object.insert(QStringLiteral("queue_enabled"), QStringLiteral("false"));
    write_object(project_path, object);
    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ProjectWorkspace::load(corrupt));
        },
        "project values with the wrong JSON type must be rejected");

    const auto incomplete =
        QDir(temporary.path()).filePath(QStringLiteral("incomplete"));
    static_cast<void>(wave3d::desktop::ProjectWorkspace::create(
        incomplete, QStringLiteral("Incomplete test")));
    expect(
        QDir(incomplete).rmdir(QStringLiteral("models")),
        "cannot remove test workspace directory");
    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ProjectWorkspace::load(incomplete));
        },
        "missing standard workspace directory must be rejected");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        test_project_round_trip_and_run_contract();
        test_validation_and_workspace_rejection();
        std::cout << "desktop project workspace tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "desktop project workspace test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
