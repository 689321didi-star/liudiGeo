#pragma once

#include "wave3d/desktop/project_workspace.hpp"

#include <QMainWindow>
#include <QString>

#include <optional>

class QCloseEvent;

namespace wave3d::desktop {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

    [[nodiscard]] bool create_project(
        const QString& root_directory,
        const QString& project_name,
        QString* error_message = nullptr);

    [[nodiscard]] bool open_project(
        const QString& root_directory,
        QString* error_message = nullptr);

    [[nodiscard]] const ProjectDocument* current_project() const noexcept;
    [[nodiscard]] const QString& current_project_root() const noexcept;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void activate_project(QString root_directory, ProjectDocument project);
    void save_window_settings();

    QString project_root_;
    std::optional<ProjectDocument> project_;
};

} // namespace wave3d::desktop
