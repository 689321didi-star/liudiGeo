#pragma once

#include "wave3d/acquisition/source.hpp"
#include "wave3d/model/physical_model.hpp"
#include "wave3d/numerics/elastic_validation.hpp"

#include <QString>

#include <cstddef>
#include <optional>
#include <vector>

namespace wave3d::desktop {

inline constexpr auto kExperimentDraftSchema =
    "wave3d.desktop.experiment.v3";
inline constexpr auto kLegacyExperimentDraftSchema =
    "wave3d.desktop.experiment.v1";
inline constexpr auto kLegacyExperimentDraftSchemaV2 =
    "wave3d.desktop.experiment.v2";

enum class DraftSourceMode {
    IsotropicExplosion,
    MomentTensor,
    DoubleCouple,
};

struct DoubleCoupleParameters {
    double scalar_moment_nm{1.0e12};
    double strike_deg{0.0};
    double dip_deg{90.0};
    double rake_deg{0.0};
};

struct RectangularReceiverGrid {
    std::size_t count_x{101};
    std::size_t count_y{101};
    double minimum_x_m{0.0};
    double maximum_x_m{0.0};
    double minimum_y_m{0.0};
    double maximum_y_m{0.0};
    double depth_m{0.0};
};

struct AcquisitionEstimate {
    std::size_t receiver_count{0};
    std::size_t sample_count{0};
    std::size_t trace_value_count{0};
    std::size_t raw_trace_bytes{0};
    std::size_t segy_bytes{0};
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
    DoubleCoupleParameters double_couple{};
    RickerWavelet wavelet{3.0, 1.0 / 3.0, 1.0};
    std::optional<RectangularReceiverGrid> receiver_grid;
};

struct ResolvedExperimentDraft {
    SimulationConfig simulation{};
    MomentTensorSource source{};
    ElasticNumericalReport numerical{};
    std::vector<PhysicalPoint3D> receivers;
    AcquisitionEstimate acquisition{};
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

    [[nodiscard]] static RectangularReceiverGrid default_receiver_grid(
        const Grid3D& grid);
    [[nodiscard]] static std::vector<PhysicalPoint3D> generate_receivers(
        const RectangularReceiverGrid& receiver_grid,
        const Grid3D& grid);
    [[nodiscard]] static AcquisitionEstimate acquisition_estimate(
        std::size_t receiver_count,
        std::size_t sample_count);

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
