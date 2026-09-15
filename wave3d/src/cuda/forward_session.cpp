#include "wave3d/cuda/forward_session.hpp"

#include "wave3d/core/checked_size.hpp"
#include "wave3d/cuda/cuda_error.hpp"

#include <algorithm>
#include <stdexcept>

namespace wave3d::cuda {
namespace {

[[nodiscard]] Grid3D validated_session_grid(
    const ElasticCoefficients& coefficients,
    const PreparedMomentTensorSource& source,
    const PreparedReceiverSet& receivers,
    const CpmlProfile& cpml_profile,
    const std::optional<TractionFreeSurface>& free_surface,
    std::size_t total_steps) {
    require_valid_elastic_coefficient_layout(coefficients);
    require_valid_prepared_moment_tensor_source(source);
    require_valid_prepared_receiver_set(receivers);
    require_valid_cpml_profile(cpml_profile);
    if (total_steps == 0) {
        throw std::invalid_argument(
            "CUDA forward session requires at least one time step");
    }
    const auto& grid = coefficients.grid;
    if (!same_grid_geometry(grid, source.grid) ||
        !same_grid_geometry(grid, receivers.grid) ||
        !same_grid_geometry(grid, cpml_profile.grid)) {
        throw std::invalid_argument(
            "CUDA forward session inputs must use one exact grid");
    }
    if (free_surface.has_value()) {
        require_valid_traction_free_surface(*free_surface);
        if (!same_grid_geometry(grid, free_surface->grid)) {
            throw std::invalid_argument(
                "CUDA forward session free surface must use the session grid");
        }
        if (cpml_profile.parameters.sides.z_min) {
            throw std::invalid_argument(
                "CUDA forward session free surface requires top CPML disabled");
        }
    } else if (!cpml_profile.parameters.sides.z_min) {
        throw std::invalid_argument(
            "CUDA forward session without a free surface requires top CPML");
    }
    return grid;
}

[[nodiscard]] std::optional<DeviceTractionFreeSurface> make_device_surface(
    const std::optional<TractionFreeSurface>& surface) {
    if (!surface.has_value()) {
        return std::nullopt;
    }
    return std::optional<DeviceTractionFreeSurface>{
        std::in_place, *surface};
}

} // namespace

CudaForwardSession::CudaForwardSession(
    const ElasticCoefficients& coefficients,
    const PreparedMomentTensorSource& source,
    const PreparedReceiverSet& receivers,
    const CpmlProfile& cpml_profile,
    std::optional<TractionFreeSurface> free_surface,
    std::size_t total_steps)
    : grid_(validated_session_grid(
          coefficients,
          source,
          receivers,
          cpml_profile,
          free_surface,
          total_steps)),
      total_steps_(total_steps),
      dt_s_(cpml_profile.parameters.dt_s),
      device_coefficients_(coefficients),
      wavefield_(grid_),
      device_source_(source),
      device_receivers_(receivers),
      device_traces_(receivers.receivers.size(), total_steps_, dt_s_),
      device_cpml_profile_(cpml_profile),
      device_cpml_state_(grid_),
      device_free_surface_(make_device_surface(free_surface)) {
    synchronize();
}

std::size_t CudaForwardSession::owned_device_bytes() const {
    require_usable();
    std::size_t result = 0;
    for (const auto bytes : {
             device_coefficients_.bytes(),
             wavefield_.bytes(),
             device_source_.bytes(),
             device_receivers_.bytes(),
             device_traces_.bytes(),
             device_cpml_profile_.bytes(),
             device_cpml_state_.bytes()}) {
        result = detail::checked_size_add(
            result, bytes, "CUDA forward session byte count overflow");
    }
    return result;
}

std::optional<ForwardStepMetadata>
CudaForwardSession::last_completed_step() const {
    require_usable();
    if (completed_steps_ == 0) {
        return std::nullopt;
    }
    const auto step = completed_steps_ - 1;
    ForwardStepMetadata metadata{
        step,
        completed_steps_,
        static_cast<double>(completed_steps_) * dt_s_,
        (static_cast<double>(step) + 0.5) * dt_s_};
    require_valid_forward_step_metadata(metadata);
    return metadata;
}

ForwardStepMetadata CudaForwardSession::advance(
    std::size_t requested_steps) {
    require_usable();
    if (requested_steps == 0) {
        throw std::invalid_argument(
            "CUDA forward session advance count must be positive");
    }
    if (finished()) {
        throw std::out_of_range(
            "CUDA forward session has already completed");
    }
    const auto batch_steps = std::min(requested_steps, remaining_steps());
    const auto target = completed_steps_ + batch_steps;
    try {
        for (auto step = completed_steps_; step < target; ++step) {
            if (device_free_surface_.has_value()) {
                advance_elastic_free_surface_step(
                    wavefield_,
                    device_coefficients_,
                    device_source_,
                    device_receivers_,
                    device_traces_,
                    device_cpml_profile_,
                    device_cpml_state_,
                    *device_free_surface_,
                    step);
            } else {
                advance_elastic_cpml_step(
                    wavefield_,
                    device_coefficients_,
                    device_source_,
                    device_receivers_,
                    device_traces_,
                    device_cpml_profile_,
                    device_cpml_state_,
                    step);
            }
        }
        synchronize();
        completed_steps_ = target;
    } catch (...) {
        failed_ = true;
        throw;
    }
    return *last_completed_step();
}

ForwardStepMetadata CudaForwardSession::advance_remaining() {
    require_usable();
    if (finished()) {
        throw std::out_of_range(
            "CUDA forward session has already completed");
    }
    return advance(remaining_steps());
}

DeviceElasticWavefieldConstView
CudaForwardSession::device_wavefield_view() const {
    require_usable();
    const auto view = wavefield_.const_view();
    require_valid_device_elastic_wavefield_view(view);
    return view;
}

void CudaForwardSession::download_wavefield(
    ElasticWavefield& destination) const {
    require_usable();
    wavefield_.download(destination);
}

void CudaForwardSession::download_receiver_traces(
    std::vector<float>& vx_m_s,
    std::vector<float>& vy_m_s,
    std::vector<float>& vz_m_s) const {
    require_usable();
    if (!finished()) {
        throw std::logic_error(
            "CUDA forward session traces require a completed run");
    }
    device_traces_.download(vx_m_s, vy_m_s, vz_m_s);
}

void CudaForwardSession::require_usable() const {
    if (failed_) {
        throw std::logic_error(
            "CUDA forward session cannot be reused after a CUDA failure");
    }
}

} // namespace wave3d::cuda
