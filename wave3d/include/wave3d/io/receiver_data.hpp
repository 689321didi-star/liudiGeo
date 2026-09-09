#pragma once

#include "wave3d/io/data.hpp"

#include <cstddef>
#include <stdexcept>

namespace wave3d::io {

enum class VelocityComponent {
    X,
    Y,
    Z,
};

class IReceiverData {
public:
    virtual ~IReceiverData() = default;

    [[nodiscard]] virtual std::size_t receiver_count() const noexcept = 0;
    [[nodiscard]] virtual std::size_t sample_count() const noexcept = 0;
    [[nodiscard]] virtual double dt_s() const noexcept = 0;
    [[nodiscard]] virtual const PhysicalPoint3D& receiver_coordinate_m(
        std::size_t receiver) const = 0;
    [[nodiscard]] virtual const MomentTensorSource& source() const noexcept = 0;
    [[nodiscard]] virtual float sample(
        VelocityComponent component,
        std::size_t receiver,
        std::size_t sample_index) const = 0;
};

class ThreeComponentReceiverDataReader final : public IReceiverData {
public:
    explicit ThreeComponentReceiverDataReader(
        const ThreeComponentTraces& traces)
        : traces_(&traces) {
        require_valid_traces(traces);
    }

    [[nodiscard]] std::size_t receiver_count() const noexcept override {
        return traces_->receiver_count;
    }

    [[nodiscard]] std::size_t sample_count() const noexcept override {
        return traces_->sample_count;
    }

    [[nodiscard]] double dt_s() const noexcept override {
        return traces_->dt_s;
    }

    [[nodiscard]] const PhysicalPoint3D& receiver_coordinate_m(
        std::size_t receiver) const override {
        if (receiver >= traces_->receiver_count) {
            throw std::out_of_range("receiver data coordinate is out of range");
        }
        return traces_->receiver_coordinates_m[receiver];
    }

    [[nodiscard]] const MomentTensorSource& source() const noexcept override {
        return traces_->source;
    }

    [[nodiscard]] float sample(
        VelocityComponent component,
        std::size_t receiver,
        std::size_t sample_index) const override {
        const auto index = traces_->linear_index(receiver, sample_index);
        switch (component) {
        case VelocityComponent::X:
            return traces_->vx_m_s[index];
        case VelocityComponent::Y:
            return traces_->vy_m_s[index];
        case VelocityComponent::Z:
            return traces_->vz_m_s[index];
        }
        throw std::invalid_argument("unknown receiver velocity component");
    }

private:
    const ThreeComponentTraces* traces_{nullptr};
};

} // namespace wave3d::io
