#pragma once

#include "wave3d/core/grid.hpp"

#include <QString>

namespace wave3d::desktop {

inline constexpr auto kSegyModelConversionSchema =
    "wave3d.desktop.segy_model_conversion.v1";

struct SegyModelConversionRequest {
    QString vp_path;
    QString vs_path;
    QString density_path;
    QString output_stem;
    Grid3D grid;
};

struct SegyModelConversionArtifact {
    QString model_reference;
    QString manifest_reference;
    QString output_sha256;
};

[[nodiscard]] SegyModelConversionArtifact convert_segy_model_artifact(
    const QString& project_root,
    const SegyModelConversionRequest& request);

} // namespace wave3d::desktop
