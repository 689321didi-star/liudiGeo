#pragma once

#include "wave3d/desktop/forward_run_worker.hpp"

#include <QImage>
#include <QString>

#include <cstddef>

namespace wave3d::desktop {

[[nodiscard]] cuda::VisualizationField visualization_field_from_key(
    const QString& key);
[[nodiscard]] QString visualization_field_key(cuda::VisualizationField field);
[[nodiscard]] QString visualization_field_name(cuda::VisualizationField field);
[[nodiscard]] QString visualization_field_unit(cuda::VisualizationField field);

[[nodiscard]] QImage composite_live_wavefield_slice(
    QImage base,
    const LiveWavefieldFrame& frame,
    int orientation,
    std::size_t fixed_index,
    float opacity,
    float amplitude_threshold);

} // namespace wave3d::desktop
