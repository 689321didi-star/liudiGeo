#include "wave3d/desktop/project_workspace.hpp"

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

QString project_file_path(const QString& root_directory) {
    return QDir(root_directory).filePath(QString::fromUtf8(kDesktopProjectFile));
}

bool valid_identifier(const QString& value) {
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$"));
    return expression.match(value).hasMatch();
}

bool valid_utc_timestamp(const QString& value) {
    const auto timestamp = QDateTime::fromString(value, Qt::ISODateWithMs);
    return timestamp.isValid() && timestamp.timeSpec() == Qt::UTC;
}

bool safe_relative_path(const QString& value) {
    if (value.isEmpty()) {
        return true;
    }
    if (QDir::isAbsolutePath(value) || value.contains(QLatin1Char('\\'))) {
        return false;
    }
    const auto clean = QDir::cleanPath(value);
    return clean == value && clean != QStringLiteral(".") &&
           clean != QStringLiteral("..") &&
           !clean.startsWith(QStringLiteral("../"));
}

void require_workspace_tree(const QString& root_directory) {
    const QDir root(root_directory);
    for (const auto* relative : {
             "source", "models", "figures/model", "runs", "manifests"}) {
        if (!QFileInfo(root.filePath(QString::fromUtf8(relative))).isDir()) {
            fail(QStringLiteral("project workspace directory is missing: %1")
                     .arg(QString::fromUtf8(relative)));
        }
    }
}

void write_atomically(const QString& path, const QByteArray& contents) {
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("cannot open project output: %1").arg(path));
    }
    if (output.write(contents) != contents.size() || !output.commit()) {
        fail(QStringLiteral("cannot atomically publish project output: %1")
                 .arg(path));
    }
}

QJsonObject project_json(const ProjectDocument& project) {
    QJsonArray shots;
    for (const auto& shot : project.shots) {
        shots.append(QJsonObject{
            {QStringLiteral("id"), shot.id},
            {QStringLiteral("name"), shot.name}});
    }
    return QJsonObject{
        {QStringLiteral("schema"), QString::fromUtf8(kDesktopProjectSchema)},
        {QStringLiteral("project_id"), project.project_id},
        {QStringLiteral("name"), project.name},
        {QStringLiteral("created_utc"), project.created_utc},
        {QStringLiteral("model_reference"), project.model_reference},
        {QStringLiteral("queue_enabled"), project.queue_enabled},
        {QStringLiteral("display_field"), project.display_field},
        {QStringLiteral("shots"), shots}};
}

QString current_utc() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString terminal_state_name(RunTerminalState state) {
    switch (state) {
    case RunTerminalState::Completed:
        return QStringLiteral("completed");
    case RunTerminalState::Cancelled:
        return QStringLiteral("cancelled");
    case RunTerminalState::Failed:
        return QStringLiteral("failed");
    }
    fail(QStringLiteral("unknown terminal run state"));
}

void validate_run_product(const RunProduct& product) {
    static const QRegularExpression digest(QStringLiteral("^[0-9a-f]{64}$"));
    if (!safe_relative_path(product.relative_path) ||
        product.relative_path.isEmpty() ||
        !digest.match(product.sha256).hasMatch() || product.byte_count <= 0 ||
        product.receiver_count == 0 || product.sample_count == 0 ||
        product.device_name.trimmed().isEmpty()) {
        fail(QStringLiteral("completed run product metadata is invalid"));
    }
    for (const auto value : {
             product.input_load_ms,
             product.setup_ms,
             product.propagation_ms,
             product.trace_download_ms,
             product.segy_write_ms}) {
        if (!std::isfinite(value) || value < 0.0) {
            fail(QStringLiteral("run timing metadata is invalid"));
        }
    }
}

} // namespace

void ProjectWorkspace::validate(const ProjectDocument& project) {
    if (!valid_identifier(project.project_id)) {
        fail(QStringLiteral("project_id must be a safe stable identifier"));
    }
    if (project.name.trimmed().isEmpty() || project.name != project.name.trimmed() ||
        project.name.size() > 120) {
        fail(QStringLiteral("project name must contain 1 to 120 trimmed characters"));
    }
    if (!valid_utc_timestamp(project.created_utc)) {
        fail(QStringLiteral("created_utc must be an ISO-8601 UTC timestamp"));
    }
    if (!safe_relative_path(project.model_reference)) {
        fail(QStringLiteral("model_reference must be a safe relative path"));
    }
    static const std::array<QString, 6> fields{
        QStringLiteral("speed"),
        QStringLiteral("vx"),
        QStringLiteral("vy"),
        QStringLiteral("vz"),
        QStringLiteral("divergence"),
        QStringLiteral("curl_magnitude")};
    if (std::find(fields.begin(), fields.end(), project.display_field) ==
        fields.end()) {
        fail(QStringLiteral("unsupported project display field"));
    }
    if (project.shots.isEmpty()) {
        fail(QStringLiteral("a project must contain at least one shot"));
    }
    QVector<QString> identifiers;
    identifiers.reserve(project.shots.size());
    for (const auto& shot : project.shots) {
        if (!valid_identifier(shot.id) || shot.name.trimmed().isEmpty() ||
            shot.name != shot.name.trimmed() || shot.name.size() > 120) {
            fail(QStringLiteral("project shot identity is invalid"));
        }
        if (identifiers.contains(shot.id)) {
            fail(QStringLiteral("project shot identifiers must be unique"));
        }
        identifiers.push_back(shot.id);
    }
}

ProjectDocument ProjectWorkspace::create(
    const QString& root_directory,
    const QString& project_name) {
    ProjectDocument project;
    project.project_id = QStringLiteral("project-") +
                         QUuid::createUuid().toString(QUuid::WithoutBraces);
    project.name = project_name.trimmed();
    project.created_utc = current_utc();
    project.shots.push_back(
        ProjectShot{QStringLiteral("shot-001"), QStringLiteral("炮 1")});
    validate(project);

    const QFileInfo root_info(root_directory);
    if (root_info.exists() && !root_info.isDir()) {
        fail(QStringLiteral("project root exists and is not a directory"));
    }

    QDir root(root_directory);
    if (root.exists() &&
        !root.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
        fail(QStringLiteral("new project directory must be empty"));
    }
    if (!QDir().mkpath(root_directory)) {
        fail(QStringLiteral("cannot create project root"));
    }
    root = QDir(root_directory);
    for (const auto* relative : {
             "source", "models", "figures/model", "runs", "manifests"}) {
        if (!root.mkpath(QString::fromUtf8(relative))) {
            fail(QStringLiteral("cannot create project workspace directory"));
        }
    }

    save(root_directory, project);
    return project;
}

ProjectDocument ProjectWorkspace::load(const QString& root_directory) {
    QFile input(project_file_path(root_directory));
    if (!input.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("cannot open project document"));
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("project document is not valid JSON"));
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("schema")).toString() !=
        QString::fromUtf8(kDesktopProjectSchema)) {
        fail(QStringLiteral("unsupported desktop project schema"));
    }
    for (const auto* key : {
             "project_id",
             "name",
             "created_utc",
             "model_reference",
             "display_field"}) {
        if (!root.value(QString::fromUtf8(key)).isString()) {
            fail(QStringLiteral("project document has invalid value types"));
        }
    }
    if (!root.value(QStringLiteral("queue_enabled")).isBool() ||
        !root.value(QStringLiteral("shots")).isArray()) {
        fail(QStringLiteral("project document has invalid value types"));
    }

    ProjectDocument project;
    project.project_id = root.value(QStringLiteral("project_id")).toString();
    project.name = root.value(QStringLiteral("name")).toString();
    project.created_utc = root.value(QStringLiteral("created_utc")).toString();
    project.model_reference =
        root.value(QStringLiteral("model_reference")).toString();
    project.queue_enabled = root.value(QStringLiteral("queue_enabled")).toBool();
    project.display_field = root.value(QStringLiteral("display_field")).toString();
    for (const auto& value : root.value(QStringLiteral("shots")).toArray()) {
        if (!value.isObject()) {
            fail(QStringLiteral("project shot must be an object"));
        }
        const auto shot = value.toObject();
        if (!shot.value(QStringLiteral("id")).isString() ||
            !shot.value(QStringLiteral("name")).isString()) {
            fail(QStringLiteral("project shot has invalid value types"));
        }
        project.shots.push_back(ProjectShot{
            shot.value(QStringLiteral("id")).toString(),
            shot.value(QStringLiteral("name")).toString()});
    }
    validate(project);
    require_workspace_tree(root_directory);
    return project;
}

void ProjectWorkspace::save(
    const QString& root_directory,
    const ProjectDocument& project) {
    validate(project);
    require_workspace_tree(root_directory);
    write_atomically(
        project_file_path(root_directory),
        QJsonDocument(project_json(project)).toJson(QJsonDocument::Indented));
}

PreparedRun ProjectWorkspace::prepare_run(
    const QString& root_directory,
    const ProjectDocument& project,
    const QString& run_id,
    const QString& shot_id,
    const QByteArray& resolved_yaml) {
    validate(project);
    const auto stored = load(root_directory);
    if (stored.project_id != project.project_id) {
        fail(QStringLiteral("project identity does not match workspace"));
    }
    if (!valid_identifier(run_id)) {
        fail(QStringLiteral("run_id must be a safe stable identifier"));
    }
    bool known_shot = false;
    for (const auto& shot : stored.shots) {
        known_shot = known_shot || shot.id == shot_id;
    }
    if (!known_shot) {
        fail(QStringLiteral("run references an unknown shot"));
    }
    if (resolved_yaml.isEmpty()) {
        fail(QStringLiteral("resolved run configuration must not be empty"));
    }

    const QDir root(root_directory);
    const auto directory = root.filePath(QStringLiteral("runs/") + run_id);
    if (QFileInfo::exists(directory) || !QDir().mkpath(directory)) {
        fail(QStringLiteral("run directory already exists or cannot be created"));
    }
    QDir run(directory);
    try {
        for (const auto* relative : {"output", "figures", "logs", "reports"}) {
            if (!run.mkpath(QString::fromUtf8(relative))) {
                fail(QStringLiteral("cannot create run workspace directory"));
            }
        }
        const auto configuration_path = run.filePath(QStringLiteral("config.yaml"));
        write_atomically(configuration_path, resolved_yaml);
        const auto digest = QCryptographicHash::hash(
                                resolved_yaml, QCryptographicHash::Sha256)
                                .toHex();
        const auto created_utc = current_utc();
        const QJsonObject manifest{
            {QStringLiteral("schema"), QString::fromUtf8(kDesktopRunSchema)},
            {QStringLiteral("run_id"), run_id},
            {QStringLiteral("project_id"), project.project_id},
            {QStringLiteral("shot_id"), shot_id},
            {QStringLiteral("created_utc"), created_utc},
            {QStringLiteral("configuration"), QStringLiteral("config.yaml")},
            {QStringLiteral("configuration_sha256"),
             QString::fromLatin1(digest)}};
        const auto manifest_path = run.filePath(QStringLiteral("manifest.json"));
        write_atomically(
            manifest_path,
            QJsonDocument(manifest).toJson(QJsonDocument::Indented));
        return PreparedRun{
            directory,
            configuration_path,
            manifest_path,
            QString::fromLatin1(digest)};
    } catch (...) {
        run.removeRecursively();
        throw;
    }
}

QString ProjectWorkspace::publish_run_result(
    const PreparedRun& run,
    const RunTerminalResult& result) {
    const QDir directory(run.directory);
    if (!directory.exists() ||
        QFileInfo(run.configuration_path).absolutePath() != directory.absolutePath() ||
        QFileInfo(run.manifest_path).absolutePath() != directory.absolutePath() ||
        !QFileInfo(run.configuration_path).isFile() ||
        !QFileInfo(run.manifest_path).isFile()) {
        fail(QStringLiteral("prepared run paths are incomplete or inconsistent"));
    }
    if (result.state == RunTerminalState::Completed) {
        if (!result.product || !result.diagnostic.isEmpty()) {
            fail(QStringLiteral("completed run requires one product and no diagnostic"));
        }
        validate_run_product(*result.product);
        const auto product_path = directory.filePath(result.product->relative_path);
        if (!QFileInfo(product_path).isFile() ||
            QFileInfo(product_path).size() != result.product->byte_count) {
            fail(QStringLiteral("completed run product file is missing or changed"));
        }
        QFile product_file(product_path);
        if (!product_file.open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("completed run product cannot be read"));
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&product_file) ||
            QString::fromLatin1(hash.result().toHex()) != result.product->sha256) {
            fail(QStringLiteral("completed run product checksum does not match"));
        }
    } else if (result.product) {
        fail(QStringLiteral("cancelled or failed run cannot claim a product"));
    } else if (result.state == RunTerminalState::Failed &&
               result.diagnostic.trimmed().isEmpty()) {
        fail(QStringLiteral("failed run requires a diagnostic"));
    }

    QJsonObject root{
        {QStringLiteral("schema"), QString::fromUtf8(kDesktopRunResultSchema)},
        {QStringLiteral("state"), terminal_state_name(result.state)},
        {QStringLiteral("completed_utc"), current_utc()}};
    if (!result.diagnostic.isEmpty()) {
        root.insert(QStringLiteral("diagnostic"), result.diagnostic);
    }
    if (result.product) {
        const auto& product = *result.product;
        root.insert(
            QStringLiteral("product"),
            QJsonObject{
                {QStringLiteral("kind"), QStringLiteral("segy_rev1_3c")},
                {QStringLiteral("path"), product.relative_path},
                {QStringLiteral("sha256"), product.sha256},
                {QStringLiteral("bytes"), product.byte_count},
                {QStringLiteral("receiver_count"),
                 static_cast<qint64>(product.receiver_count)},
                {QStringLiteral("sample_count"),
                 static_cast<qint64>(product.sample_count)},
                {QStringLiteral("device_name"), product.device_name},
                {QStringLiteral("timings_ms"),
                 QJsonObject{
                     {QStringLiteral("input_load"), product.input_load_ms},
                     {QStringLiteral("setup"), product.setup_ms},
                     {QStringLiteral("propagation"), product.propagation_ms},
                     {QStringLiteral("trace_download"), product.trace_download_ms},
                     {QStringLiteral("segy_write"), product.segy_write_ms}}}});
    }

    const auto path = directory.filePath(QStringLiteral("result.json"));
    if (QFileInfo::exists(path)) {
        fail(QStringLiteral("terminal run result already exists"));
    }
    write_atomically(path, QJsonDocument(root).toJson(QJsonDocument::Indented));
    return path;
}

} // namespace wave3d::desktop
