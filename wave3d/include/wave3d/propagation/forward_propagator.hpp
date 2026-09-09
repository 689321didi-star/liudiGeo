#pragma once

#include "wave3d/acquisition/moment_source_injector.hpp"
#include "wave3d/acquisition/receiver_sampler.hpp"
#include "wave3d/core/checked_size.hpp"
#include "wave3d/diagnostics/forward_observer.hpp"
#include "wave3d/io/data.hpp"
#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/physics/cpu_elastic_step.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace wave3d {

class IForwardPropagator {
public:
    virtual ~IForwardPropagator() = default;

    [[nodiscard]] virtual const Grid3D& grid() const noexcept = 0;
    [[nodiscard]] virtual std::size_t completed_steps() const noexcept = 0;
    [[nodiscard]] virtual std::size_t total_steps() const noexcept = 0;
    [[nodiscard]] virtual ElasticWavefieldConstView wavefield() const = 0;
    [[nodiscard]] virtual const io::ThreeComponentTraces& receiver_traces()
        const noexcept = 0;
    virtual void attach_observer(IForwardObserver& observer) = 0;
    virtual void advance_one() = 0;

    void run_to_completion() {
        while (completed_steps() < total_steps()) {
            advance_one();
        }
    }
};

enum class ForwardPropagatorBackend {
    CpuReferenceInterior,
};

struct CpuReferencePropagatorInputs {
    ElasticCoefficients coefficients;
    PreparedMomentTensorSource source;
    PreparedReceiverSet receivers;
    std::size_t sample_count{0};
    double dt_s{0.0};
};

namespace detail {

[[nodiscard]] inline Grid3D validate_cpu_propagator_inputs(
    const CpuReferencePropagatorInputs& inputs) {
    require_valid_elastic_coefficient_layout(inputs.coefficients);
    require_valid_prepared_moment_tensor_source(inputs.source);
    require_valid_prepared_receiver_set(inputs.receivers);
    if (!same_grid_geometry(
            inputs.coefficients.grid, inputs.source.grid) ||
        !same_grid_geometry(
            inputs.coefficients.grid, inputs.receivers.grid)) {
        throw std::invalid_argument(
            "CPU propagator inputs must use one exact grid");
    }
    if (inputs.sample_count == 0 || !std::isfinite(inputs.dt_s) ||
        !(inputs.dt_s > 0.0)) {
        throw std::invalid_argument(
            "CPU propagator needs positive samples and finite dt");
    }
    const double final_time =
        static_cast<double>(inputs.sample_count) * inputs.dt_s;
    if (!std::isfinite(final_time)) {
        throw std::overflow_error("CPU propagator final time is not finite");
    }
    return inputs.coefficients.grid;
}

} // namespace detail

class CpuReferenceInteriorPropagator final : public IForwardPropagator {
public:
    explicit CpuReferenceInteriorPropagator(
        CpuReferencePropagatorInputs inputs)
        : grid_(detail::validate_cpu_propagator_inputs(inputs)),
          coefficients_(std::move(inputs.coefficients)),
          source_(std::move(inputs.source)),
          receivers_(std::move(inputs.receivers)),
          sample_count_(inputs.sample_count),
          dt_s_(inputs.dt_s),
          wavefield_(grid_),
          receiver_frame_(receivers_.receivers.size()) {
        traces_.receiver_count = receivers_.receivers.size();
        traces_.sample_count = sample_count_;
        traces_.dt_s = dt_s_;
        traces_.source = source_.source;
        traces_.receiver_coordinates_m.reserve(traces_.receiver_count);
        for (const auto& receiver : receivers_.receivers) {
            traces_.receiver_coordinates_m.push_back(
                receiver.receiver.physical_location);
        }
        const auto values = detail::checked_size_product(
            traces_.receiver_count,
            traces_.sample_count,
            "CPU propagator trace size overflows size_t");
        traces_.vx_m_s.resize(values);
        traces_.vy_m_s.resize(values);
        traces_.vz_m_s.resize(values);
        io::require_valid_traces(traces_);
    }

    CpuReferenceInteriorPropagator(
        const CpuReferenceInteriorPropagator&) = delete;
    CpuReferenceInteriorPropagator& operator=(
        const CpuReferenceInteriorPropagator&) = delete;

    [[nodiscard]] const Grid3D& grid() const noexcept override {
        return grid_;
    }

    [[nodiscard]] std::size_t completed_steps() const noexcept override {
        return next_step_;
    }

    [[nodiscard]] std::size_t total_steps() const noexcept override {
        return sample_count_;
    }

    [[nodiscard]] ElasticWavefieldConstView wavefield() const override {
        return elastic_wavefield_view(wavefield_);
    }

    [[nodiscard]] const io::ThreeComponentTraces& receiver_traces()
        const noexcept override {
        return traces_;
    }

    void attach_observer(IForwardObserver& observer) override {
        if (next_step_ != 0) {
            throw std::logic_error(
                "forward observers must be attached before propagation");
        }
        if (std::find(observers_.begin(), observers_.end(), &observer) !=
            observers_.end()) {
            throw std::invalid_argument(
                "forward observer is already attached");
        }
        observers_.push_back(&observer);
    }

    void advance_one() override {
        if (next_step_ >= sample_count_) {
            throw std::out_of_range("forward propagator has already completed");
        }
        const auto step = next_step_;
        cpu_advance_elastic_interior_step(
            wavefield_,
            coefficients_,
            source_,
            receivers_,
            step,
            dt_s_,
            receiver_frame_);
        for (std::size_t receiver = 0;
             receiver < receiver_frame_.size();
             ++receiver) {
            const auto trace_index = receiver * sample_count_ + step;
            traces_.vx_m_s[trace_index] = receiver_frame_[receiver].vx_m_s;
            traces_.vy_m_s[trace_index] = receiver_frame_[receiver].vy_m_s;
            traces_.vz_m_s[trace_index] = receiver_frame_[receiver].vz_m_s;
        }
        ++next_step_;
        const ForwardStepMetadata metadata{
            step,
            next_step_,
            static_cast<double>(next_step_) * dt_s_,
            (static_cast<double>(step) + 0.5) * dt_s_};
        require_valid_forward_step_metadata(metadata);
        const auto view = elastic_wavefield_view(wavefield_);
        for (auto* observer : observers_) {
            observer->after_step(metadata, view);
        }
    }

private:
    Grid3D grid_{};
    ElasticCoefficients coefficients_;
    PreparedMomentTensorSource source_;
    PreparedReceiverSet receivers_;
    std::size_t sample_count_{0};
    double dt_s_{0.0};
    ElasticWavefield wavefield_;
    std::vector<ReceiverVelocitySample> receiver_frame_;
    io::ThreeComponentTraces traces_;
    std::vector<IForwardObserver*> observers_;
    std::size_t next_step_{0};
};

[[nodiscard]] inline std::unique_ptr<IForwardPropagator>
make_forward_propagator(
    ForwardPropagatorBackend backend,
    CpuReferencePropagatorInputs inputs) {
    switch (backend) {
    case ForwardPropagatorBackend::CpuReferenceInterior:
        return std::make_unique<CpuReferenceInteriorPropagator>(
            std::move(inputs));
    }
    throw std::invalid_argument("unsupported forward propagator backend");
}

} // namespace wave3d
