#pragma once

#include "wave3d/wave/elastic_wavefield_view.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace wave3d {

struct ForwardStepMetadata {
    std::size_t step_index_n{0};
    std::size_t completed_steps{0};
    double velocity_time_s{0.0};
    double stress_time_s{0.0};
};

inline void require_valid_forward_step_metadata(
    const ForwardStepMetadata& metadata) {
    if (metadata.completed_steps == 0 ||
        metadata.step_index_n != metadata.completed_steps - 1 ||
        !std::isfinite(metadata.velocity_time_s) ||
        !std::isfinite(metadata.stress_time_s) ||
        !(metadata.velocity_time_s > 0.0) ||
        !(metadata.stress_time_s >= 0.0) ||
        !(metadata.stress_time_s < metadata.velocity_time_s)) {
        throw std::invalid_argument("forward step metadata is inconsistent");
    }
}

class IForwardObserver {
public:
    virtual ~IForwardObserver() = default;

    virtual void after_step(
        const ForwardStepMetadata& metadata,
        const ElasticWavefieldConstView& wavefield) = 0;
};

} // namespace wave3d
