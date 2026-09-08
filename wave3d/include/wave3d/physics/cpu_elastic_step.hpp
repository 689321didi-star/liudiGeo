#pragma once

#include "wave3d/acquisition/moment_source_injector.hpp"
#include "wave3d/acquisition/receiver_sampler.hpp"
#include "wave3d/physics/cpu_elastic_update.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace wave3d {

inline void require_valid_cpu_elastic_interior_step_inputs(
    const ElasticWavefield& wavefield,
    const ElasticCoefficients& coefficients,
    const PreparedMomentTensorSource& source,
    const PreparedReceiverSet& receivers,
    std::size_t step_index_n,
    double dt_s,
    const std::vector<ReceiverVelocitySample>& receiver_frame) {
    require_valid_elastic_wavefield_layout(wavefield);
    require_valid_elastic_coefficient_layout(coefficients);
    require_valid_prepared_moment_tensor_source(source);
    require_valid_prepared_receiver_set(receivers);
    if (!same_grid_geometry(wavefield.grid, coefficients.grid) ||
        !same_grid_geometry(wavefield.grid, source.grid) ||
        !same_grid_geometry(wavefield.grid, receivers.grid)) {
        throw std::invalid_argument(
            "CPU elastic step inputs must use one exact grid geometry");
    }
    if (!std::isfinite(dt_s) || !(dt_s > 0.0)) {
        throw std::invalid_argument(
            "CPU elastic step time step must be finite and positive");
    }
    if (step_index_n == std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("CPU elastic step index overflows size_t");
    }
    if (receiver_frame.size() != receivers.receivers.size()) {
        throw std::invalid_argument(
            "CPU elastic receiver frame must be preallocated exactly");
    }

    const double stress_time_s =
        static_cast<double>(step_index_n) * dt_s;
    const double receiver_time_s =
        static_cast<double>(step_index_n + 1) * dt_s;
    if (!std::isfinite(stress_time_s) || !std::isfinite(receiver_time_s)) {
        throw std::overflow_error("CPU elastic step time is not finite");
    }
}

inline void cpu_advance_elastic_interior_step(
    ElasticWavefield& wavefield,
    const ElasticCoefficients& coefficients,
    const PreparedMomentTensorSource& source,
    const PreparedReceiverSet& receivers,
    std::size_t step_index_n,
    double dt_s,
    std::vector<ReceiverVelocitySample>& receiver_frame) {
    require_valid_cpu_elastic_interior_step_inputs(
        wavefield,
        coefficients,
        source,
        receivers,
        step_index_n,
        dt_s,
        receiver_frame);

    cpu_update_elastic_stresses(wavefield, coefficients, dt_s);
    inject_moment_tensor_source(
        wavefield,
        source,
        step_index_n,
        dt_s);

    // Increment 4e deliberately has no accepted stress-boundary operation.

    cpu_update_elastic_velocities(wavefield, coefficients, dt_s);

    // Increment 4e deliberately has no accepted velocity-boundary operation.

    sample_receivers_after_velocity_step(
        wavefield,
        receivers,
        step_index_n,
        dt_s,
        receiver_frame);
}

} // namespace wave3d
