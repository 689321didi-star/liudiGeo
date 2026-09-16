#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

namespace wave3d::desktop {

inline constexpr auto kDesktopProjectSchema = "wave3d.desktop.project.v1";
inline constexpr auto kDesktopRunSchema = "wave3d.desktop.run.v1";
inline constexpr auto kDesktopRunResultSchema = "wave3d.desktop.run_result.v1";
inline constexpr auto kDesktopProjectFile = "project.wave3d.json";

struct ProjectShot {
    QString id;
    QString name;
};

struct ProjectDocument {
    QString project_id;
    QString name;
    QString created_utc;
    QString model_reference;
    bool queue_enabled{false};
    QString display_field{QStringLiteral("speed")};
    QVector<ProjectShot> shots;
};

struct PreparedRun {
    QString directory;
    QString configuration_path;
    QString manifest_path;
    QString configuration_sha256;
};

enum class RunTerminalState { Completed, Cancelled, Failed };

struct RunProduct {
    QString relative_path;
    QString sha256;
    qint64 byte_count{0};
    quint64 receiver_count{0};
    quint64 sample_count{0};
    QString device_name;
    double input_load_ms{0.0};
    double setup_ms{0.0};
    double propagation_ms{0.0};
    double trace_download_ms{0.0};
    double segy_write_ms{0.0};
};

struct RunTerminalResult {
    RunTerminalState state{RunTerminalState::Cancelled};
    QString diagnostic;
    std::optional<RunProduct> product;
};

class ProjectWorkspace final {
public:
    [[nodiscard]] static ProjectDocument create(
        const QString& root_directory,
        const QString& project_name);

    [[nodiscard]] static ProjectDocument load(const QString& root_directory);

    static void save(
        const QString& root_directory,
        const ProjectDocument& project);

    static void validate(const ProjectDocument& project);

    [[nodiscard]] static PreparedRun prepare_run(
        const QString& root_directory,
        const ProjectDocument& project,
        const QString& run_id,
        const QString& shot_id,
        const QByteArray& resolved_yaml);

    [[nodiscard]] static QString publish_run_result(
        const PreparedRun& run,
        const RunTerminalResult& result);
};

} // namespace wave3d::desktop
