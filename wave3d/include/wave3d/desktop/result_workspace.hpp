#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

#include <cstddef>
#include <memory>

namespace wave3d::desktop {

class ResultWorkspace final : public QWidget {
public:
    explicit ResultWorkspace(QWidget* parent = nullptr);
    ~ResultWorkspace() override;

    void set_project_root(const QString& project_root);
    void set_segy_available(bool available);

    [[nodiscard]] int completed_run_count() const noexcept;
    [[nodiscard]] QString selected_run_id() const;
    [[nodiscard]] QImage current_gather_image() const;
    [[nodiscard]] QString current_header_report() const;

    [[nodiscard]] bool select_run(
        const QString& run_id,
        QString* error_message = nullptr);
    [[nodiscard]] bool set_component(
        const QString& component,
        QString* error_message = nullptr);
    [[nodiscard]] bool set_receiver_window(
        std::size_t first_receiver,
        std::size_t receiver_count,
        QString* error_message = nullptr);
    [[nodiscard]] bool export_current_png(
        const QString& path,
        QString* error_message = nullptr) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wave3d::desktop
