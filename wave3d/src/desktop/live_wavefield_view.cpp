#include "wave3d/desktop/live_wavefield_view.hpp"

#include <QColor>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wave3d::desktop {
namespace {

int image_dimension(std::size_t value) {
    if (value == 0 ||
        value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("live wavefield dimension exceeds QImage");
    }
    return static_cast<int>(value);
}

QColor interpolate(QColor first, QColor second, float fraction) {
    const auto mix = [fraction](int left, int right) {
        return static_cast<int>(std::lround(
            left + fraction * static_cast<float>(right - left)));
    };
    return {
        mix(first.red(), second.red()),
        mix(first.green(), second.green()),
        mix(first.blue(), second.blue())};
}

QColor live_colour(float normalized, bool signed_scale) {
    normalized = std::clamp(normalized, 0.0F, 1.0F);
    if (signed_scale) {
        const QColor negative(43, 108, 255);
        const QColor zero(235, 244, 248);
        const QColor positive(255, 72, 88);
        return normalized <= 0.5F
                   ? interpolate(negative, zero, normalized * 2.0F)
                   : interpolate(zero, positive, (normalized - 0.5F) * 2.0F);
    }
    const QColor low(25, 185, 220);
    const QColor middle(255, 221, 72);
    const QColor high(255, 77, 66);
    return normalized <= 0.5F
               ? interpolate(low, middle, normalized * 2.0F)
               : interpolate(middle, high, (normalized - 0.5F) * 2.0F);
}

float normalized_amplitude(float value, bool signed_scale) {
    return signed_scale ? std::abs(2.0F * value - 1.0F) : value;
}

} // namespace

cuda::VisualizationField visualization_field_from_key(const QString& key) {
    if (key == QStringLiteral("vx")) {
        return cuda::VisualizationField::Vx;
    }
    if (key == QStringLiteral("vy")) {
        return cuda::VisualizationField::Vy;
    }
    if (key == QStringLiteral("vz")) {
        return cuda::VisualizationField::Vz;
    }
    if (key == QStringLiteral("speed")) {
        return cuda::VisualizationField::Speed;
    }
    if (key == QStringLiteral("divergence")) {
        return cuda::VisualizationField::Divergence;
    }
    if (key == QStringLiteral("curl_magnitude")) {
        return cuda::VisualizationField::CurlMagnitude;
    }
    throw std::invalid_argument("unknown live wavefield field key");
}

QString visualization_field_key(cuda::VisualizationField field) {
    switch (field) {
    case cuda::VisualizationField::Vx:
        return QStringLiteral("vx");
    case cuda::VisualizationField::Vy:
        return QStringLiteral("vy");
    case cuda::VisualizationField::Vz:
        return QStringLiteral("vz");
    case cuda::VisualizationField::Speed:
        return QStringLiteral("speed");
    case cuda::VisualizationField::Divergence:
        return QStringLiteral("divergence");
    case cuda::VisualizationField::CurlMagnitude:
        return QStringLiteral("curl_magnitude");
    }
    throw std::invalid_argument("unknown live wavefield field");
}

QString visualization_field_name(cuda::VisualizationField field) {
    switch (field) {
    case cuda::VisualizationField::Vx:
        return QStringLiteral("Vx");
    case cuda::VisualizationField::Vy:
        return QStringLiteral("Vy");
    case cuda::VisualizationField::Vz:
        return QStringLiteral("Vz");
    case cuda::VisualizationField::Speed:
        return QStringLiteral("速度模");
    case cuda::VisualizationField::Divergence:
        return QStringLiteral("散度");
    case cuda::VisualizationField::CurlMagnitude:
        return QStringLiteral("旋度模");
    }
    throw std::invalid_argument("unknown live wavefield field");
}

QString visualization_field_unit(cuda::VisualizationField field) {
    return field == cuda::VisualizationField::Divergence ||
                   field == cuda::VisualizationField::CurlMagnitude
               ? QStringLiteral("s⁻¹")
               : QStringLiteral("m/s");
}

QImage composite_live_wavefield_slice(
    QImage base,
    const LiveWavefieldFrame& frame,
    int orientation,
    std::size_t fixed_index,
    float opacity,
    float amplitude_threshold) {
    require_valid_grid_geometry(frame.grid);
    if (!frame.normalized_values ||
        frame.value_count != frame.grid.physical_cell_count()) {
        throw std::invalid_argument("live wavefield frame storage is invalid");
    }
    if (orientation < 0 || orientation > 2) {
        throw std::invalid_argument("live wavefield slice orientation is invalid");
    }
    const auto limit = orientation == 0 ? frame.grid.nz
                                       : orientation == 1 ? frame.grid.ny
                                                          : frame.grid.nx;
    if (fixed_index >= limit) {
        throw std::out_of_range("live wavefield slice index is outside the grid");
    }
    const auto width = image_dimension(
        orientation == 2 ? frame.grid.ny : frame.grid.nx);
    const auto height = image_dimension(
        orientation == 0 ? frame.grid.ny : frame.grid.nz);
    if (base.size() != QSize(width, height)) {
        throw std::invalid_argument("live wavefield and base slice dimensions differ");
    }
    opacity = std::clamp(opacity, 0.0F, 1.0F);
    amplitude_threshold = std::clamp(amplitude_threshold, 0.0F, 0.95F);
    QImage overlay(width, height, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    const auto* values = frame.normalized_values.get();
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            std::size_t x = orientation == 2
                                ? fixed_index
                                : static_cast<std::size_t>(column);
            std::size_t y = orientation == 1
                                ? fixed_index
                                : orientation == 0
                                      ? frame.grid.ny - 1 -
                                            static_cast<std::size_t>(row)
                                      : static_cast<std::size_t>(column);
            std::size_t z = orientation == 0
                                ? fixed_index
                                : static_cast<std::size_t>(row);
            const auto value = values[frame.grid.physical_linear_index(x, y, z)];
            const auto amplitude = normalized_amplitude(value, frame.signed_scale);
            if (amplitude <= amplitude_threshold) {
                continue;
            }
            const auto scaled = (amplitude - amplitude_threshold) /
                                (1.0F - amplitude_threshold);
            auto colour = live_colour(value, frame.signed_scale);
            colour.setAlphaF(opacity * std::clamp(scaled, 0.0F, 1.0F));
            overlay.setPixelColor(column, row, colour);
        }
    }
    QPainter painter(&base);
    painter.drawImage(0, 0, overlay);
    return base;
}

} // namespace wave3d::desktop
