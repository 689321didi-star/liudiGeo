#include "wave3d/desktop/volume_viewport.hpp"

#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QVariantList>
#include <QVector3D>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace wave3d::desktop {
namespace {

constexpr auto vertex_shader = R"glsl(
#version 330 core
out vec2 screen_position;
void main() {
    vec2 points[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    screen_position = points[gl_VertexID] * 0.5 + 0.5;
    gl_Position = vec4(points[gl_VertexID], 0.0, 1.0);
}
)glsl";

constexpr auto fragment_shader = R"glsl(
#version 330 core
in vec2 screen_position;
out vec4 fragment_colour;
uniform sampler3D volume_texture;
uniform vec3 camera_position;
uniform vec3 camera_forward;
uniform vec3 camera_right;
uniform vec3 camera_up;
uniform vec3 volume_aspect;
uniform vec3 crop_minimum;
uniform vec3 crop_maximum;
uniform float viewport_aspect;
uniform float opacity;
uniform float lower_threshold;

vec3 colour_map(float value) {
    vec3 low = vec3(8.0, 29.0, 55.0) / 255.0;
    vec3 middle = vec3(23.0, 132.0, 160.0) / 255.0;
    vec3 high = vec3(250.0, 221.0, 90.0) / 255.0;
    return value <= 0.5 ? mix(low, middle, value * 2.0)
                        : mix(middle, high, (value - 0.5) * 2.0);
}

bool intersect_box(vec3 origin, vec3 direction, out float near_t, out float far_t) {
    vec3 half_box = volume_aspect * 0.5;
    vec3 inverse_direction = 1.0 / direction;
    vec3 first = (-half_box - origin) * inverse_direction;
    vec3 second = (half_box - origin) * inverse_direction;
    vec3 lower = min(first, second);
    vec3 upper = max(first, second);
    near_t = max(max(lower.x, lower.y), lower.z);
    far_t = min(min(upper.x, upper.y), upper.z);
    return far_t > max(near_t, 0.0);
}

void main() {
    vec2 point = screen_position * 2.0 - 1.0;
    point.x *= viewport_aspect;
    vec3 direction = normalize(
        camera_forward + 0.58 * point.x * camera_right + 0.58 * point.y * camera_up);
    float near_t;
    float far_t;
    if (!intersect_box(camera_position, direction, near_t, far_t)) {
        fragment_colour = vec4(0.035, 0.055, 0.075, 1.0);
        return;
    }
    near_t = max(near_t, 0.0);
    float step_length = (far_t - near_t) / 320.0;
    vec4 accumulated = vec4(0.0);
    for (int index = 0; index < 320; ++index) {
        vec3 position = camera_position + direction * (near_t + (index + 0.5) * step_length);
        vec3 texture_coordinate = position / volume_aspect + 0.5;
        // Model z grows downward. Keep the surface at the top of the rendered box.
        texture_coordinate.z = 1.0 - texture_coordinate.z;
        if (all(greaterThanEqual(texture_coordinate, crop_minimum)) &&
            all(lessThanEqual(texture_coordinate, crop_maximum))) {
            float value = texture(volume_texture, texture_coordinate).r;
            float density = smoothstep(lower_threshold, 1.0, value);
            float alpha = 1.0 - exp(-density * opacity * 0.075);
            vec3 colour = colour_map(value);
            accumulated.rgb += (1.0 - accumulated.a) * alpha * colour;
            accumulated.a += (1.0 - accumulated.a) * alpha;
            if (accumulated.a > 0.985) {
                break;
            }
        }
    }
    vec3 background = vec3(0.035, 0.055, 0.075);
    fragment_colour = vec4(
        accumulated.rgb + (1.0 - accumulated.a) * background, 1.0);
}
)glsl";

} // namespace

VolumeViewport::VolumeViewport(QWidget* parent) : QOpenGLWidget(parent) {
    setObjectName(QStringLiteral("volumeViewport"));
    setMinimumSize(320, 240);
    setMouseTracking(true);
    setProperty("openGlReady", false);
    setProperty("volumeShaderReady", false);
    setProperty("volumeTextureReady", false);
    setProperty("volumeFrameReady", false);
    setProperty("volumeOpacity", opacity_);
    setProperty("volumeLowerThreshold", lower_threshold_);
    update_camera_diagnostics();
}

VolumeViewport::~VolumeViewport() {
    if (property("openGlReady").toBool() && context() != nullptr &&
        context()->isValid()) {
        makeCurrent();
        if (QOpenGLContext::currentContext() == context()) {
            release_gl_resources();
        }
        doneCurrent();
    }
}

void VolumeViewport::set_volume(
    VolumeTextureData data,
    QString property_name) {
    pending_volume_ = std::move(data);
    physical_aspect_ = pending_volume_->physical_aspect;
    property_name_ = std::move(property_name);
    failure_message_.clear();
    setProperty("volumeTextureReady", false);
    setProperty("volumeFrameReady", false);
    setProperty(
        "volumeTextureDimensions",
        QStringLiteral("%1x%2x%3")
            .arg(pending_volume_->nx)
            .arg(pending_volume_->ny)
            .arg(pending_volume_->nz));
    setProperty("uploadedModelProperty", property_name_);
    update();
}

void VolumeViewport::set_crop_bounds(const std::array<float, 6>& bounds) {
    crop_bounds_ = bounds;
    QVariantList diagnostic_bounds;
    for (const auto bound : bounds) {
        diagnostic_bounds.push_back(bound);
    }
    setProperty("volumeCropBounds", diagnostic_bounds);
    update();
}

void VolumeViewport::clear_volume() {
    pending_volume_.reset();
    property_name_.clear();
    failure_message_.clear();
    if (property("openGlReady").toBool() && context() != nullptr &&
        context()->isValid()) {
        makeCurrent();
        if (QOpenGLContext::currentContext() == context() && texture_ != 0) {
            glDeleteTextures(1, &texture_);
            texture_ = 0;
        }
        doneCurrent();
    }
    setProperty("volumeTextureReady", false);
    setProperty("volumeFrameReady", false);
    setProperty("volumeTextureDimensions", QString());
    setProperty("uploadedModelProperty", QString());
    update();
}

void VolumeViewport::set_opacity(float opacity) {
    opacity_ = std::clamp(opacity, 0.0F, 1.0F);
    setProperty("volumeOpacity", opacity_);
    update();
}

void VolumeViewport::set_lower_threshold(float threshold) {
    lower_threshold_ = std::clamp(threshold, 0.0F, 0.95F);
    setProperty("volumeLowerThreshold", lower_threshold_);
    update();
}

void VolumeViewport::set_camera(float yaw, float pitch, float distance) {
    if (!std::isfinite(yaw) || !std::isfinite(pitch) ||
        !std::isfinite(distance)) {
        throw std::invalid_argument("volume camera values must be finite");
    }
    yaw_ = std::remainder(yaw, 6.2831853071795864769F);
    pitch_ = std::clamp(pitch, -1.35F, 1.35F);
    distance_ = std::clamp(distance, 1.15F, 5.0F);
    update_camera_diagnostics();
    update();
}

void VolumeViewport::reset_camera() {
    set_camera(0.65F, 0.42F, 1.75F);
}

void VolumeViewport::initializeGL() {
    initializeOpenGLFunctions();
    setProperty("openGlReady", context() != nullptr && context()->isValid());
    try {
        program_ = std::make_unique<QOpenGLShaderProgram>();
        if (!program_->addShaderFromSourceCode(
                QOpenGLShader::Vertex, vertex_shader) ||
            !program_->addShaderFromSourceCode(
                QOpenGLShader::Fragment, fragment_shader) ||
            !program_->link()) {
            throw std::runtime_error(program_->log().toStdString());
        }
        glGenVertexArrays(1, &vertex_array_);
        setProperty("volumeShaderReady", true);
        failure_message_.clear();
    } catch (const std::exception& error) {
        failure_message_ = QString::fromUtf8(error.what());
        setProperty("volumeShaderReady", false);
    }
}

void VolumeViewport::upload_pending_volume() {
    if (!pending_volume_) {
        return;
    }
    const auto& data = *pending_volume_;
    GLint maximum_size = 0;
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &maximum_size);
    if (data.nx > static_cast<std::size_t>(maximum_size) ||
        data.ny > static_cast<std::size_t>(maximum_size) ||
        data.nz > static_cast<std::size_t>(maximum_size)) {
        throw std::runtime_error("model exceeds OpenGL 3-D texture limits");
    }
    if (texture_ == 0) {
        glGenTextures(1, &texture_);
    }
    glBindTexture(GL_TEXTURE_3D, texture_);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(
        GL_TEXTURE_3D,
        0,
        GL_R32F,
        static_cast<GLsizei>(data.nx),
        static_cast<GLsizei>(data.ny),
        static_cast<GLsizei>(data.nz),
        0,
        GL_RED,
        GL_FLOAT,
        data.normalized_values.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_3D, 0);
    pending_volume_.reset();
    const auto upload_error = glGetError();
    if (upload_error != GL_NO_ERROR) {
        throw std::runtime_error(
            "OpenGL 3-D texture upload failed with error " +
            std::to_string(upload_error));
    }
    failure_message_.clear();
    setProperty("volumeTextureReady", true);
}

void VolumeViewport::paintGL() {
    setProperty("volumeFrameReady", false);
    glClearColor(0.035F, 0.055F, 0.075F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (program_ && property("volumeShaderReady").toBool()) {
        try {
            upload_pending_volume();
        } catch (const std::exception& error) {
            failure_message_ = QString::fromUtf8(error.what());
            setProperty("volumeTextureReady", false);
        }
    }
    if (program_ && texture_ != 0 && property("volumeTextureReady").toBool()) {
        const QVector3D camera(
            distance_ * std::cos(pitch_) * std::cos(yaw_),
            distance_ * std::cos(pitch_) * std::sin(yaw_),
            distance_ * std::sin(pitch_));
        const auto forward = -camera.normalized();
        const auto right = QVector3D::crossProduct(
                               forward, QVector3D(0.0F, 0.0F, 1.0F))
                               .normalized();
        const auto up = QVector3D::crossProduct(right, forward).normalized();
        program_->bind();
        program_->setUniformValue("camera_position", camera);
        program_->setUniformValue("camera_forward", forward);
        program_->setUniformValue("camera_right", right);
        program_->setUniformValue("camera_up", up);
        program_->setUniformValue(
            "volume_aspect",
            QVector3D(
                physical_aspect_[0], physical_aspect_[1], physical_aspect_[2]));
        program_->setUniformValue(
            "crop_minimum",
            QVector3D(crop_bounds_[0], crop_bounds_[2], crop_bounds_[4]));
        program_->setUniformValue(
            "crop_maximum",
            QVector3D(crop_bounds_[1], crop_bounds_[3], crop_bounds_[5]));
        program_->setUniformValue(
            "viewport_aspect",
            height() == 0 ? 1.0F : static_cast<float>(width()) / height());
        program_->setUniformValue("opacity", opacity_);
        program_->setUniformValue("lower_threshold", lower_threshold_);
        program_->setUniformValue("volume_texture", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, texture_);
        glBindVertexArray(vertex_array_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_3D, 0);
        program_->release();
        const auto frame_error = glGetError();
        if (frame_error == GL_NO_ERROR) {
            failure_message_.clear();
            setProperty("volumeFrameReady", true);
        } else {
            failure_message_ = QStringLiteral("OpenGL 绘制错误 %1")
                                   .arg(static_cast<unsigned int>(frame_error));
        }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(18, 46, 62, 225));
    painter.drawRoundedRect(QRect(14, 14, 132, 30), 8, 8);
    painter.setPen(QColor(171, 220, 239));
    painter.drawText(
        QRect(14, 14, 132, 30), Qt::AlignCenter, QStringLiteral("三维体渲染"));
    painter.setPen(QColor(171, 194, 207));
    const auto message = failure_message_.isEmpty()
                             ? texture_ == 0
                                   ? QStringLiteral("等待模型数据")
                                   : QStringLiteral("拖动旋转 · 滚轮缩放 · %1")
                                         .arg(property_name_)
                             : QStringLiteral("渲染失败：%1").arg(failure_message_);
    painter.drawText(
        rect().adjusted(16, 0, -16, -14),
        Qt::AlignHCenter | Qt::AlignBottom,
        message);
}

void VolumeViewport::mousePressEvent(QMouseEvent* event) {
    last_mouse_position_ = event->position().toPoint();
    QOpenGLWidget::mousePressEvent(event);
}

void VolumeViewport::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons().testFlag(Qt::LeftButton)) {
        const auto current = event->position().toPoint();
        const auto delta = current - last_mouse_position_;
        yaw_ += static_cast<float>(delta.x()) * 0.008F;
        pitch_ = std::clamp(
            pitch_ - static_cast<float>(delta.y()) * 0.008F, -1.35F, 1.35F);
        last_mouse_position_ = current;
        update_camera_diagnostics();
        update();
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void VolumeViewport::wheelEvent(QWheelEvent* event) {
    distance_ = std::clamp(
        distance_ * std::exp(-static_cast<float>(event->angleDelta().y()) / 1200.0F),
        1.15F,
        5.0F);
    update_camera_diagnostics();
    update();
    event->accept();
}

void VolumeViewport::update_camera_diagnostics() {
    setProperty("volumeCameraYaw", yaw_);
    setProperty("volumeCameraPitch", pitch_);
    setProperty("volumeCameraDistance", distance_);
}

void VolumeViewport::release_gl_resources() {
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    if (vertex_array_ != 0) {
        glDeleteVertexArrays(1, &vertex_array_);
        vertex_array_ = 0;
    }
    program_.reset();
}

} // namespace wave3d::desktop
