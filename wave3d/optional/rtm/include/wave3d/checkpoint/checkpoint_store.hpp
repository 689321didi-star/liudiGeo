#pragma once

#include "wave3d/diagnostics/forward_observer.hpp"
#include "wave3d/wave/elastic_wavefield.hpp"

#include <cstddef>
#include <stdexcept>

namespace wave3d::checkpoint {

struct CheckpointMetadata {
    Grid3D grid{};
    ForwardStepMetadata time_level{};
};

[[nodiscard]] inline CheckpointMetadata checkpoint_metadata(
    const ForwardStepMetadata& time_level,
    const ElasticWavefieldConstView& wavefield) {
    require_valid_forward_step_metadata(time_level);
    require_valid_elastic_wavefield_view(wavefield);
    return {wavefield.grid, time_level};
}

[[nodiscard]] inline bool same_checkpoint_metadata(
    const CheckpointMetadata& first,
    const CheckpointMetadata& second) noexcept {
    return same_grid_geometry(first.grid, second.grid) &&
           first.time_level.step_index_n == second.time_level.step_index_n &&
           first.time_level.completed_steps ==
               second.time_level.completed_steps &&
           first.time_level.velocity_time_s ==
               second.time_level.velocity_time_s &&
           first.time_level.stress_time_s == second.time_level.stress_time_s;
}

class ICheckpointStore {
public:
    virtual ~ICheckpointStore() = default;

    virtual void save(
        const CheckpointMetadata& metadata,
        const ElasticWavefieldConstView& wavefield) = 0;
    [[nodiscard]] virtual bool contains(
        std::size_t completed_steps) const noexcept = 0;
    [[nodiscard]] virtual CheckpointMetadata metadata(
        std::size_t completed_steps) const = 0;
    virtual void restore(
        std::size_t completed_steps,
        ElasticWavefield& destination) const = 0;
};

class CheckpointObserver final : public IForwardObserver {
public:
    CheckpointObserver(ICheckpointStore& store, std::size_t interval_steps)
        : store_(&store), interval_steps_(interval_steps) {
        if (interval_steps_ == 0) {
            throw std::invalid_argument(
                "checkpoint observer interval must be positive");
        }
    }

    void after_step(
        const ForwardStepMetadata& metadata,
        const ElasticWavefieldConstView& wavefield) override {
        require_valid_forward_step_metadata(metadata);
        require_valid_elastic_wavefield_view(wavefield);
        if (metadata.completed_steps % interval_steps_ == 0) {
            store_->save(checkpoint_metadata(metadata, wavefield), wavefield);
        }
    }

private:
    ICheckpointStore* store_{nullptr};
    std::size_t interval_steps_{0};
};

} // namespace wave3d::checkpoint
