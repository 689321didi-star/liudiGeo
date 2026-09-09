#pragma once

#include "wave3d/boundary/sponge.hpp"
#include "wave3d/cuda/device_buffer.hpp"
#include "wave3d/cuda/elastic_propagator.hpp"

#include <cstddef>

namespace wave3d::cuda {

class DeviceSpongeProfile {
public:
    explicit DeviceSpongeProfile(const SpongeProfile& host)
        : grid_(host.grid), damping_(validated_size(host)) {
        damping_.copy_from_host(host.damping.data(), host.damping.size());
    }

    DeviceSpongeProfile(const DeviceSpongeProfile&) = delete;
    DeviceSpongeProfile& operator=(const DeviceSpongeProfile&) = delete;
    DeviceSpongeProfile(DeviceSpongeProfile&&) noexcept = default;
    DeviceSpongeProfile& operator=(DeviceSpongeProfile&&) noexcept = default;

    [[nodiscard]] const Grid3D& grid() const noexcept { return grid_; }
    [[nodiscard]] std::size_t bytes() const { return damping_.bytes(); }

private:
    friend void apply_sponge_to_stresses(
        DeviceElasticWavefield&, const DeviceSpongeProfile&);
    friend void apply_sponge_to_velocities(
        DeviceElasticWavefield&, const DeviceSpongeProfile&);

    [[nodiscard]] static std::size_t validated_size(
        const SpongeProfile& host) {
        require_valid_grid_geometry(host.grid);
        if (host.damping.size() != host.grid.allocated_cell_count()) {
            throw std::invalid_argument(
                "host sponge profile size does not match allocated grid");
        }
        if (!std::isfinite(host.outer_damping_per_application) ||
            !(host.outer_damping_per_application > 0.0F) ||
            host.outer_damping_per_application > 1.0F) {
            throw std::invalid_argument(
                "host sponge outer damping must be finite and in (0, 1]");
        }
        for (const float factor : host.damping) {
            if (!std::isfinite(factor) || !(factor > 0.0F) || factor > 1.0F) {
                throw std::invalid_argument(
                    "host sponge factors must be finite and in (0, 1]");
            }
        }
        return host.damping.size();
    }

    Grid3D grid_{};
    DeviceBuffer<float> damping_;
};

void apply_sponge_to_stresses(
    DeviceElasticWavefield& wavefield,
    const DeviceSpongeProfile& profile);

void apply_sponge_to_velocities(
    DeviceElasticWavefield& wavefield,
    const DeviceSpongeProfile& profile);

void advance_elastic_sponge_step(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceMomentTensorSource& source,
    const DeviceReceiverSet& receivers,
    DeviceReceiverTraces& traces,
    const DeviceSpongeProfile& profile,
    std::size_t step_index_n);

} // namespace wave3d::cuda
