#pragma once

#include "wave3d/acquisition/moment_source_injector.hpp"
#include "wave3d/acquisition/receiver_sampler.hpp"
#include "wave3d/boundary/cpml.hpp"
#include "wave3d/boundary/free_surface.hpp"
#include "wave3d/cuda/cpml.hpp"
#include "wave3d/cuda/elastic_propagator.hpp"
#include "wave3d/cuda/free_surface.hpp"
#include "wave3d/diagnostics/forward_observer.hpp"
#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/wave/elastic_wavefield.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace wave3d::cuda {

class CudaForwardSession {
public:
    CudaForwardSession(
        const ElasticCoefficients& coefficients,
        const PreparedMomentTensorSource& source,
        const PreparedReceiverSet& receivers,
        const CpmlProfile& cpml_profile,
        std::optional<TractionFreeSurface> free_surface,
        std::size_t total_steps);

    CudaForwardSession(const CudaForwardSession&) = delete;
    CudaForwardSession& operator=(const CudaForwardSession&) = delete;
    CudaForwardSession(CudaForwardSession&&) = delete;
    CudaForwardSession& operator=(CudaForwardSession&&) = delete;

    [[nodiscard]] std::size_t total_steps() const noexcept {
        return total_steps_;
    }
    [[nodiscard]] std::size_t completed_steps() const noexcept {
        return completed_steps_;
    }
    [[nodiscard]] std::size_t remaining_steps() const noexcept {
        return total_steps_ - completed_steps_;
    }
    [[nodiscard]] bool finished() const noexcept {
        return completed_steps_ == total_steps_;
    }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] double dt_s() const noexcept { return dt_s_; }
    [[nodiscard]] const Grid3D& grid() const noexcept { return grid_; }
    [[nodiscard]] std::size_t receiver_count() const noexcept {
        return device_receivers_.receiver_count();
    }
    [[nodiscard]] std::size_t owned_device_bytes() const;

    [[nodiscard]] std::optional<ForwardStepMetadata>
    last_completed_step() const;

    [[nodiscard]] ForwardStepMetadata advance(std::size_t requested_steps);
    [[nodiscard]] ForwardStepMetadata advance_remaining();

    [[nodiscard]] DeviceElasticWavefieldConstView device_wavefield_view() const;
    void download_wavefield(ElasticWavefield& destination) const;
    void download_receiver_traces(
        std::vector<float>& vx_m_s,
        std::vector<float>& vy_m_s,
        std::vector<float>& vz_m_s) const;

private:
    void require_usable() const;

    Grid3D grid_{};
    std::size_t total_steps_{0};
    std::size_t completed_steps_{0};
    double dt_s_{0.0};
    bool failed_{false};
    DeviceElasticCoefficients device_coefficients_;
    DeviceElasticWavefield wavefield_;
    DeviceMomentTensorSource device_source_;
    DeviceReceiverSet device_receivers_;
    DeviceReceiverTraces device_traces_;
    DeviceCpmlProfile device_cpml_profile_;
    DeviceCpmlState device_cpml_state_;
    std::optional<DeviceTractionFreeSurface> device_free_surface_;
};

} // namespace wave3d::cuda
