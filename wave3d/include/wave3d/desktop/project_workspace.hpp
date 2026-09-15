#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace wave3d::desktop {

inline constexpr auto kDesktopProjectSchema = "wave3d.desktop.project.v1";
inline constexpr auto kDesktopRunSchema = "wave3d.desktop.run.v1";
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
};

} // namespace wave3d::desktop
