#pragma once

#include "wave3d/boundary/cpml.hpp"
#include "wave3d/cuda/device_buffer.hpp"
#include "wave3d/cuda/elastic_propagator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace wave3d::cuda {

struct DeviceCpmlAxisView {
    const float* a_integer{nullptr};
    const float* b_integer{nullptr};
    const float* inverse_kappa_integer{nullptr};
    const float* a_half{nullptr};
    const float* b_half{nullptr};
    const float* inverse_kappa_half{nullptr};
};

class DeviceCpmlProfile {
public:
    explicit DeviceCpmlProfile(const CpmlProfile& host)
        : grid_(host.grid),
          dt_s_(host.parameters.dt_s),
          sides_(host.parameters.sides),
          coefficients_(validated_element_count(host)) {
        std::vector<float> flattened(coefficients_.size());
        std::size_t offset = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto& source = host.axes[axis];
            const std::array<const std::vector<float>*, 6> arrays{{
                &source.a_integer,
                &source.b_integer,
                &source.inverse_kappa_integer,
                &source.a_half,
                &source.b_half,
                &source.inverse_kappa_half}};
            for (std::size_t component = 0; component < arrays.size();
                 ++component) {
                offsets_[axis * 6 + component] = offset;
                std::copy(
                    arrays[component]->begin(),
                    arrays[component]->end(),
                    flattened.begin() + static_cast<std::ptrdiff_t>(offset));
                offset += arrays[component]->size();
            }
        }
        coefficients_.copy_from_host(flattened.data(), flattened.size());
    }

    DeviceCpmlProfile(const DeviceCpmlProfile&) = delete;
    DeviceCpmlProfile& operator=(const DeviceCpmlProfile&) = delete;
    DeviceCpmlProfile(DeviceCpmlProfile&&) noexcept = default;
    DeviceCpmlProfile& operator=(DeviceCpmlProfile&&) noexcept = default;

    [[nodiscard]] const Grid3D& grid() const noexcept { return grid_; }
    [[nodiscard]] double dt_s() const noexcept { return dt_s_; }
    [[nodiscard]] const CpmlSides& sides() const noexcept { return sides_; }
    [[nodiscard]] std::size_t bytes() const { return coefficients_.bytes(); }
    [[nodiscard]] DeviceCpmlAxisView axis(std::size_t axis_number) const {
        if (axis_number >= 3) {
            throw std::out_of_range("CUDA CPML axis is outside x/y/z");
        }
        const float* base = coefficients_.get();
        const auto start = axis_number * 6;
        return {
            base + offsets_[start],
            base + offsets_[start + 1],
            base + offsets_[start + 2],
            base + offsets_[start + 3],
            base + offsets_[start + 4],
            base + offsets_[start + 5]};
    }

private:
    [[nodiscard]] static std::size_t validated_element_count(
        const CpmlProfile& host) {
        require_valid_cpml_profile(host);
        std::size_t count = 0;
        for (const auto& axis : host.axes) {
            count = detail::checked_size_add(
                count,
                axis.bytes() / sizeof(float),
                "CUDA CPML coefficient count overflow");
        }
        return count;
    }

    Grid3D grid_{};
    double dt_s_{0.0};
    CpmlSides sides_{};
    std::array<std::size_t, 18> offsets_{};
    DeviceBuffer<float> coefficients_;
};

class DeviceCpmlState {
public:
    explicit DeviceCpmlState(const Grid3D& grid) : grid_(grid) {
        require_valid_grid_geometry(grid_);
        const auto cells = grid_.allocated_cell_count();
        for (auto& field : fields_) {
            field.allocate(cells);
            field.zero();
        }
    }

    DeviceCpmlState(const DeviceCpmlState&) = delete;
    DeviceCpmlState& operator=(const DeviceCpmlState&) = delete;
    DeviceCpmlState(DeviceCpmlState&&) noexcept = default;
    DeviceCpmlState& operator=(DeviceCpmlState&&) noexcept = default;

    void zero() {
        for (auto& field : fields_) {
            field.zero();
        }
    }

    void download(CpmlState& host) const {
        if (!same_grid_geometry(grid_, host.grid)) {
            throw std::invalid_argument(
                "host and device CPML state grids must match");
        }
        for (std::size_t field = 0; field < fields_.size(); ++field) {
            if (host.fields[field].size() != fields_[field].size()) {
                throw std::invalid_argument(
                    "host CPML state field has invalid size");
            }
            fields_[field].copy_to_host(
                host.fields[field].data(), host.fields[field].size());
        }
    }

    [[nodiscard]] const Grid3D& grid() const noexcept { return grid_; }
    [[nodiscard]] std::size_t bytes() const {
        return detail::checked_size_product(
            cpml_memory_field_count,
            fields_[0].bytes(),
            "CUDA CPML state bytes overflow");
    }

private:
    friend void update_elastic_stresses_cpml(
        DeviceElasticWavefield&,
        const DeviceElasticCoefficients&,
        const DeviceCpmlProfile&,
        DeviceCpmlState&);
    friend void update_elastic_velocities_cpml(
        DeviceElasticWavefield&,
        const DeviceElasticCoefficients&,
        const DeviceCpmlProfile&,
        DeviceCpmlState&);

    Grid3D grid_{};
    std::array<DeviceBuffer<float>, cpml_memory_field_count> fields_;
};

void update_elastic_stresses_cpml(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceCpmlProfile& profile,
    DeviceCpmlState& state);

void update_elastic_velocities_cpml(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceCpmlProfile& profile,
    DeviceCpmlState& state);

void advance_elastic_cpml_step(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceMomentTensorSource& source,
    const DeviceReceiverSet& receivers,
    DeviceReceiverTraces& traces,
    const DeviceCpmlProfile& profile,
    DeviceCpmlState& state,
    std::size_t step_index_n);

} // namespace wave3d::cuda
