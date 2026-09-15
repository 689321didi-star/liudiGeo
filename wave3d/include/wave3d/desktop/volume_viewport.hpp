#pragma once

#include "wave3d/desktop/static_model_scene.hpp"

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QString>

#include <array>
#include <memory>
#include <optional>
#include <vector>

class QMouseEvent;
class QOpenGLShaderProgram;
class QWheelEvent;

namespace wave3d::desktop {

class VolumeViewport final : public QOpenGLWidget,
                             protected QOpenGLFunctions_3_3_Core {
public:
    explicit VolumeViewport(QWidget* parent = nullptr);
    ~VolumeViewport() override;

    void set_volume(VolumeTextureData data, QString property_name);
    void set_crop_bounds(const std::array<float, 6>& bounds);
    void set_source_position(
        const std::optional<std::array<float, 3>>& normalized_position);
    void set_receiver_positions(
        std::vector<std::array<float, 3>> normalized_positions);
    void clear_volume();
    void set_opacity(float opacity);
    void set_lower_threshold(float threshold);
    void set_camera(float yaw, float pitch, float distance);
    void reset_camera();

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void upload_pending_volume();
    void update_camera_diagnostics();
    void release_gl_resources();

    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::optional<VolumeTextureData> pending_volume_;
    std::array<float, 3> physical_aspect_{1.0F, 1.0F, 1.0F};
    std::array<float, 6> crop_bounds_{0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 1.0F};
    std::optional<std::array<float, 3>> source_position_;
    std::vector<std::array<float, 3>> receiver_positions_;
    QString property_name_;
    QString failure_message_;
    QPoint last_mouse_position_;
    unsigned int texture_{0};
    unsigned int vertex_array_{0};
    float yaw_{0.65F};
    float pitch_{0.42F};
    float distance_{1.75F};
    float opacity_{0.55F};
    float lower_threshold_{0.08F};
};

} // namespace wave3d::desktop
