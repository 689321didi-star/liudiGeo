#pragma once

#include "wave3d/boundary/free_surface.hpp"
#include "wave3d/cuda/cpml.hpp"
#include "wave3d/cuda/elastic_propagator.hpp"

#include <cstddef>

namespace wave3d::cuda {

class DeviceTractionFreeSurface {
public:
    explicit DeviceTractionFreeSurface(const TractionFreeSurface& host)
        : grid_(host.grid),
          surface_storage_z_(host.surface_storage_z),
          ghost_depth_(host.ghost_depth) {
        require_valid_traction_free_surface(host);
    }

    [[nodiscard]] const Grid3D& grid() const noexcept { return grid_; }
    [[nodiscard]] std::size_t surface_storage_z() const noexcept {
        return surface_storage_z_;
    }
    [[nodiscard]] std::size_t ghost_depth() const noexcept {
        return ghost_depth_;
    }

private:
    Grid3D grid_{};
    std::size_t surface_storage_z_{0};
    std::size_t ghost_depth_{0};
};

void apply_traction_free_stresses(
    DeviceElasticWavefield& wavefield,
    const DeviceTractionFreeSurface& surface);

void apply_traction_free_velocities(
    DeviceElasticWavefield& wavefield,
    const DeviceTractionFreeSurface& surface);

void advance_elastic_free_surface_step(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceMomentTensorSource& source,
    const DeviceReceiverSet& receivers,
    DeviceReceiverTraces& traces,
    const DeviceCpmlProfile& profile,
    DeviceCpmlState& state,
    const DeviceTractionFreeSurface& surface,
    std::size_t step_index_n);

} // namespace wave3d::cuda
