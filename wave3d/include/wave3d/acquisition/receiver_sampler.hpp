#pragma once

#include "wave3d/acquisition/receiver.hpp"
#include "wave3d/acquisition/trilinear_stencil.hpp"
#include "wave3d/wave/elastic_wavefield.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace wave3d {

struct PreparedReceiverStencils {
    Receiver receiver{};
    TrilinearStencil vx;
    TrilinearStencil vy;
    TrilinearStencil vz;
};

struct PreparedReceiverSet {
    Grid3D grid{};
    std::vector<PreparedReceiverStencils> receivers;
};

struct ReceiverVelocitySample {
    double time_s{0.0};
    float vx_m_s{0.0F};
    float vy_m_s{0.0F};
    float vz_m_s{0.0F};
};

namespace detail {

inline void require_receiver_storage_location_matches_grid(
    const Grid3D& grid,
    const Receiver& receiver) {
    const auto expected =
        physical_to_storage_coordinate(grid, receiver.physical_location);
    if (receiver.storage_location.x != expected.x ||
        receiver.storage_location.y != expected.y ||
        receiver.storage_location.z != expected.z) {
        throw std::invalid_argument(
            "receiver storage coordinate does not match its preparation grid");
    }
}

inline void require_receiver_stencil_lattice(
    const TrilinearStencil& stencil,
    ElasticLattice expected) {
    if (stencil.lattice != expected) {
        throw std::invalid_argument(
            "receiver stencil uses the wrong staggered lattice");
    }
}

[[nodiscard]] inline float interpolate_receiver_component(
    const std::vector<float>& field,
    const TrilinearStencil& stencil) {
    double value = 0.0;
    for (const auto& node : stencil.nodes) {
        const double sample = static_cast<double>(field[node.linear_index]);
        if (!std::isfinite(sample)) {
            throw std::invalid_argument(
                "receiver cannot sample a non-finite wavefield value");
        }
        value += node.weight * sample;
    }
    if (!std::isfinite(value) ||
        value > static_cast<double>(std::numeric_limits<float>::max()) ||
        value < -static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::overflow_error(
            "receiver sample is not representable as finite float32");
    }
    return static_cast<float>(value);
}

} // namespace detail

inline void require_valid_prepared_receiver_set(
    const PreparedReceiverSet& prepared) {
    require_valid_grid_geometry(prepared.grid);
    if (prepared.receivers.empty()) {
        throw std::invalid_argument(
            "prepared receiver set must not be empty");
    }
    for (const auto& receiver : prepared.receivers) {
        detail::require_receiver_storage_location_matches_grid(
            prepared.grid,
            receiver.receiver);
        detail::require_receiver_stencil_lattice(
            receiver.vx,
            ElasticLattice::XHalf);
        detail::require_receiver_stencil_lattice(
            receiver.vy,
            ElasticLattice::YHalf);
        detail::require_receiver_stencil_lattice(
            receiver.vz,
            ElasticLattice::ZHalf);
        require_valid_trilinear_stencil(prepared.grid, receiver.vx);
        require_valid_trilinear_stencil(prepared.grid, receiver.vy);
        require_valid_trilinear_stencil(prepared.grid, receiver.vz);
    }
}

[[nodiscard]] inline PreparedReceiverSet prepare_receiver_stencils(
    const Grid3D& grid,
    const ReceiverSet& receiver_set) {
    if (receiver_set.receivers.empty()) {
        throw std::invalid_argument("receiver set must not be empty");
    }

    PreparedReceiverSet result{};
    result.grid = grid;
    result.receivers.reserve(receiver_set.receivers.size());
    for (const auto& receiver : receiver_set.receivers) {
        detail::require_receiver_storage_location_matches_grid(grid, receiver);
        result.receivers.push_back({
            receiver,
            prepare_trilinear_stencil(
                grid,
                receiver.storage_location,
                ElasticLattice::XHalf),
            prepare_trilinear_stencil(
                grid,
                receiver.storage_location,
                ElasticLattice::YHalf),
            prepare_trilinear_stencil(
                grid,
                receiver.storage_location,
                ElasticLattice::ZHalf)});
    }
    require_valid_prepared_receiver_set(result);
    return result;
}

inline void sample_receivers_after_velocity_step(
    const ElasticWavefield& wavefield,
    const PreparedReceiverSet& prepared,
    std::size_t step_index_n,
    double dt_s,
    std::vector<ReceiverVelocitySample>& output) {
    require_valid_elastic_wavefield_layout(wavefield);
    require_valid_prepared_receiver_set(prepared);
    if (!same_grid_geometry(wavefield.grid, prepared.grid)) {
        throw std::invalid_argument(
            "receiver and wavefield grids must match exactly");
    }
    if (!std::isfinite(dt_s) || !(dt_s > 0.0)) {
        throw std::invalid_argument(
            "receiver time step must be finite and positive");
    }
    if (step_index_n == std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("receiver sample step index overflows size_t");
    }
    if (output.size() != prepared.receivers.size()) {
        throw std::invalid_argument(
            "receiver output must be preallocated to the receiver count");
    }

    const double sample_time_s =
        static_cast<double>(step_index_n + 1) * dt_s;
    if (!std::isfinite(sample_time_s)) {
        throw std::overflow_error("receiver sample time is not finite");
    }

    for (std::size_t receiver_number = 0;
         receiver_number < prepared.receivers.size();
         ++receiver_number) {
        const auto& receiver = prepared.receivers[receiver_number];
        output[receiver_number] = {
            sample_time_s,
            detail::interpolate_receiver_component(
                wavefield.vx_m_s,
                receiver.vx),
            detail::interpolate_receiver_component(
                wavefield.vy_m_s,
                receiver.vy),
            detail::interpolate_receiver_component(
                wavefield.vz_m_s,
                receiver.vz)};
    }
}

} // namespace wave3d
