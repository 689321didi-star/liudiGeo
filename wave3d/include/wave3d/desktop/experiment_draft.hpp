#pragma once

#include "wave3d/acquisition/source.hpp"
#include "wave3d/model/physical_model.hpp"
#include "wave3d/numerics/elastic_validation.hpp"

#include <QString>

namespace wave3d::desktop {

inline constexpr auto kExperimentDraftSchema =
    "wave3d.desktop.experiment.v1";

enum class DraftSourceMode {
    IsotropicExplosion,
    MomentTensor,
};

struct ExperimentDraft {
    QString shot_id;
    QString model_reference;
    double dt_s{0.001};
    double total_time_s{3.0};
    double cfl_safety_factor{0.85};
    double design_frequency_hz{9.0};
    PhysicalPoint3D source_location_m{};
    double source_origin_time_s{0.0};
    DraftSourceMode source_mode{DraftSourceMode::IsotropicExplosion};
    double explosion_moment_nm{1.0e12};
    SymmetricMomentTensor moment_tensor_nm{
        1.0e12, 1.0e12, 1.0e12, 0.0, 0.0, 0.0};
    RickerWavelet wavelet{3.0, 1.0 / 3.0, 1.0};
};

struct ResolvedExperimentDraft {
    SimulationConfig simulation{};
    MomentTensorSource source{};
    ElasticNumericalReport numerical{};
};

class ExperimentDraftStore final {
public:
    [[nodiscard]] static ExperimentDraft defaults(
        const QString& shot_id,
        const QString& model_reference,
        const Grid3D& grid,
        const PhysicalModelExtrema& extrema);

    static void validate(const ExperimentDraft& draft);

    [[nodiscard]] static ResolvedExperimentDraft resolve(
        const ExperimentDraft& draft,
        const Grid3D& grid,
        const PhysicalModelExtrema& extrema);

    [[nodiscard]] static QString relative_path(const QString& shot_id);
    [[nodiscard]] static bool exists(
        const QString& project_root,
        const QString& shot_id);
    [[nodiscard]] static ExperimentDraft load(
        const QString& project_root,
        const QString& shot_id);
    static void save(
        const QString& project_root,
        const ExperimentDraft& draft);
};

} // namespace wave3d::desktop
