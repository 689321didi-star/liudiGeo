#pragma once

#include "wave3d/desktop/static_model_scene.hpp"

#include <QString>

namespace wave3d::desktop {

inline constexpr auto kModelDerivationSchema =
    "wave3d.desktop.model_derivation.v1";

struct DerivedModelArtifact {
    QString model_reference;
    QString manifest_reference;
    QString source_sha256;
    QString output_sha256;
};

[[nodiscard]] DerivedModelArtifact create_cropped_model_artifact(
    const QString& project_root,
    const QString& source_model_reference,
    const StaticModelScene& source_scene,
    const ModelCropBounds& bounds,
    const QString& output_stem);

} // namespace wave3d::desktop
