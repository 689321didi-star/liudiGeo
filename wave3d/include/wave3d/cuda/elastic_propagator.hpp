#pragma once

#include "wave3d/acquisition/moment_source_injector.hpp"
#include "wave3d/acquisition/receiver_sampler.hpp"
#include "wave3d/core/checked_size.hpp"
#include "wave3d/cuda/device_buffer.hpp"
#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/wave/elastic_wavefield.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace wave3d::cuda {

class DeviceSpongeProfile;
class DeviceCpmlProfile;
class DeviceCpmlState;

class DeviceElasticWavefield {
public:
    explicit DeviceElasticWavefield(const Grid3D& grid)
        : grid_(grid),
          vx_m_s_(validated_cell_count(grid)),
          vy_m_s_(vx_m_s_.size()),
          vz_m_s_(vx_m_s_.size()),
          sxx_pa_(vx_m_s_.size()),
          syy_pa_(vx_m_s_.size()),
          szz_pa_(vx_m_s_.size()),
          sxy_pa_(vx_m_s_.size()),
          sxz_pa_(vx_m_s_.size()),
          syz_pa_(vx_m_s_.size()) {
        zero();
    }

    DeviceElasticWavefield(const DeviceElasticWavefield&) = delete;
    DeviceElasticWavefield& operator=(const DeviceElasticWavefield&) = delete;
    DeviceElasticWavefield(DeviceElasticWavefield&&) noexcept = default;
    DeviceElasticWavefield& operator=(DeviceElasticWavefield&&) noexcept =
        default;

    void zero() {
        vx_m_s_.zero();
        vy_m_s_.zero();
        vz_m_s_.zero();
        sxx_pa_.zero();
        syy_pa_.zero();
        szz_pa_.zero();
        sxy_pa_.zero();
        sxz_pa_.zero();
        syz_pa_.zero();
    }

    void upload(const ElasticWavefield& host) {
        require_valid_elastic_wavefield_layout(host);
        require_same_grid(host.grid);
        copy_from_host(host.vx_m_s, vx_m_s_);
        copy_from_host(host.vy_m_s, vy_m_s_);
        copy_from_host(host.vz_m_s, vz_m_s_);
        copy_from_host(host.sxx_pa, sxx_pa_);
        copy_from_host(host.syy_pa, syy_pa_);
        copy_from_host(host.szz_pa, szz_pa_);
        copy_from_host(host.sxy_pa, sxy_pa_);
        copy_from_host(host.sxz_pa, sxz_pa_);
        copy_from_host(host.syz_pa, syz_pa_);
    }

    void download(ElasticWavefield& host) const {
        require_valid_elastic_wavefield_layout(host);
        require_same_grid(host.grid);
        copy_to_host(vx_m_s_, host.vx_m_s);
        copy_to_host(vy_m_s_, host.vy_m_s);
        copy_to_host(vz_m_s_, host.vz_m_s);
        copy_to_host(sxx_pa_, host.sxx_pa);
        copy_to_host(syy_pa_, host.syy_pa);
        copy_to_host(szz_pa_, host.szz_pa);
        copy_to_host(sxy_pa_, host.sxy_pa);
        copy_to_host(sxz_pa_, host.sxz_pa);
        copy_to_host(syz_pa_, host.syz_pa);
    }

    [[nodiscard]] const Grid3D& grid() const noexcept { return grid_; }
    [[nodiscard]] std::size_t cell_count() const noexcept {
        return vx_m_s_.size();
    }
    [[nodiscard]] std::size_t bytes() const {
        return detail::checked_size_product(
            9, vx_m_s_.bytes(), "CUDA elastic wavefield bytes overflow");
    }

private:
    friend void update_elastic_stresses(
        DeviceElasticWavefield&, const class DeviceElasticCoefficients&, double);
    friend void inject_moment_tensor_source(
        DeviceElasticWavefield&,
        const class DeviceMomentTensorSource&,
        std::size_t,
        double);
    friend void update_elastic_velocities(
        DeviceElasticWavefield&, const class DeviceElasticCoefficients&, double);
    friend void sample_receivers_after_velocity_step(
        const DeviceElasticWavefield&,
        const class DeviceReceiverSet&,
        class DeviceReceiverTraces&,
        std::size_t);
    friend void apply_sponge_to_stresses(
        DeviceElasticWavefield&, const DeviceSpongeProfile&);
    friend void apply_sponge_to_velocities(
        DeviceElasticWavefield&, const DeviceSpongeProfile&);
    friend void update_elastic_stresses_cpml(
        DeviceElasticWavefield&,
        const class DeviceElasticCoefficients&,
        const DeviceCpmlProfile&,
        DeviceCpmlState&);
    friend void update_elastic_velocities_cpml(
        DeviceElasticWavefield&,
        const class DeviceElasticCoefficients&,
        const DeviceCpmlProfile&,
        DeviceCpmlState&);

    [[nodiscard]] static std::size_t validated_cell_count(const Grid3D& grid) {
        require_valid_grid_geometry(grid);
        return grid.allocated_cell_count();
    }

    void require_same_grid(const Grid3D& other) const {
        if (!same_grid_geometry(grid_, other)) {
            throw std::invalid_argument(
                "host and device elastic wavefield grids must match");
        }
    }

    static void copy_from_host(
        const std::vector<float>& host,
        DeviceBuffer<float>& device) {
        device.copy_from_host(host.data(), host.size());
    }

    static void copy_to_host(
        const DeviceBuffer<float>& device,
        std::vector<float>& host) {
        device.copy_to_host(host.data(), host.size());
    }

    Grid3D grid_{};
    DeviceBuffer<float> vx_m_s_;
    DeviceBuffer<float> vy_m_s_;
    DeviceBuffer<float> vz_m_s_;
    DeviceBuffer<float> sxx_pa_;
    DeviceBuffer<float> syy_pa_;
    DeviceBuffer<float> szz_pa_;
    DeviceBuffer<float> sxy_pa_;
    DeviceBuffer<float> sxz_pa_;
    DeviceBuffer<float> syz_pa_;
};

class DeviceElasticCoefficients {
public:
    explicit DeviceElasticCoefficients(const ElasticCoefficients& host)
        : grid_(host.grid),
          lambda_pa_(validated_cell_count(host)),
          shear_modulus_pa_(lambda_pa_.size()),
          bulk_modulus_pa_(lambda_pa_.size()),
          buoyancy_x_m3_kg_(lambda_pa_.size()),
          buoyancy_y_m3_kg_(lambda_pa_.size()),
          buoyancy_z_m3_kg_(lambda_pa_.size()),
          shear_modulus_xy_pa_(lambda_pa_.size()),
          shear_modulus_xz_pa_(lambda_pa_.size()),
          shear_modulus_yz_pa_(lambda_pa_.size()) {
        copy(host.lambda_pa, lambda_pa_);
        copy(host.shear_modulus_pa, shear_modulus_pa_);
        copy(host.bulk_modulus_pa, bulk_modulus_pa_);
        copy(host.buoyancy_x_m3_kg, buoyancy_x_m3_kg_);
        copy(host.buoyancy_y_m3_kg, buoyancy_y_m3_kg_);
        copy(host.buoyancy_z_m3_kg, buoyancy_z_m3_kg_);
        copy(host.shear_modulus_xy_pa, shear_modulus_xy_pa_);
        copy(host.shear_modulus_xz_pa, shear_modulus_xz_pa_);
        copy(host.shear_modulus_yz_pa, shear_modulus_yz_pa_);
    }

    DeviceElasticCoefficients(const DeviceElasticCoefficients&) = delete;
    DeviceElasticCoefficients& operator=(const DeviceElasticCoefficients&) =
        delete;
    DeviceElasticCoefficients(DeviceElasticCoefficients&&) noexcept = default;
    DeviceElasticCoefficients& operator=(DeviceElasticCoefficients&&) noexcept =
        default;

    [[nodiscard]] const Grid3D& grid() const noexcept { return grid_; }
    [[nodiscard]] std::size_t bytes() const {
        return detail::checked_size_product(
            9, lambda_pa_.bytes(), "CUDA elastic coefficient bytes overflow");
    }

private:
    friend void update_elastic_stresses(
        DeviceElasticWavefield&, const DeviceElasticCoefficients&, double);
    friend void update_elastic_velocities(
        DeviceElasticWavefield&, const DeviceElasticCoefficients&, double);
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

    [[nodiscard]] static std::size_t validated_cell_count(
        const ElasticCoefficients& host) {
        require_valid_elastic_coefficient_layout(host);
        return host.grid.allocated_cell_count();
    }

    static void copy(
        const std::vector<float>& host,
        DeviceBuffer<float>& device) {
        device.copy_from_host(host.data(), host.size());
    }

    Grid3D grid_{};
    DeviceBuffer<float> lambda_pa_;
    DeviceBuffer<float> shear_modulus_pa_;
    DeviceBuffer<float> bulk_modulus_pa_;
    DeviceBuffer<float> buoyancy_x_m3_kg_;
    DeviceBuffer<float> buoyancy_y_m3_kg_;
    DeviceBuffer<float> buoyancy_z_m3_kg_;
    DeviceBuffer<float> shear_modulus_xy_pa_;
    DeviceBuffer<float> shear_modulus_xz_pa_;
    DeviceBuffer<float> shear_modulus_yz_pa_;
};

class DeviceMomentTensorSource {
public:
    explicit DeviceMomentTensorSource(const PreparedMomentTensorSource& host)
        : grid_(host.grid),
          source_(host.source),
          linear_indices_(48),
          weights_(48) {
        require_valid_prepared_moment_tensor_source(host);
        std::array<std::size_t, 48> indices{};
        std::array<double, 48> weights{};
        const std::array<const TrilinearStencil*, 6> stencils{{
            &host.sxx, &host.syy, &host.szz, &host.sxy, &host.sxz, &host.syz}};
        for (std::size_t component = 0; component < stencils.size(); ++component) {
            for (std::size_t node = 0; node < 8; ++node) {
                const auto flat = component * 8 + node;
                indices[flat] = stencils[component]->nodes[node].linear_index;
                weights[flat] = stencils[component]->nodes[node].weight;
            }
        }
        linear_indices_.copy_from_host(indices.data(), indices.size());
        weights_.copy_from_host(weights.data(), weights.size());
    }

    DeviceMomentTensorSource(const DeviceMomentTensorSource&) = delete;
    DeviceMomentTensorSource& operator=(const DeviceMomentTensorSource&) = delete;
    DeviceMomentTensorSource(DeviceMomentTensorSource&&) noexcept = default;
    DeviceMomentTensorSource& operator=(DeviceMomentTensorSource&&) noexcept =
        default;

    [[nodiscard]] const Grid3D& grid() const noexcept { return grid_; }
    [[nodiscard]] std::size_t bytes() const {
        return linear_indices_.bytes() + weights_.bytes();
    }

private:
    friend void inject_moment_tensor_source(
        DeviceElasticWavefield&,
        const DeviceMomentTensorSource&,
        std::size_t,
        double);

    Grid3D grid_{};
    MomentTensorSource source_{};
    DeviceBuffer<std::size_t> linear_indices_;
    DeviceBuffer<double> weights_;
};

class DeviceReceiverSet {
public:
    explicit DeviceReceiverSet(const PreparedReceiverSet& host)
        : grid_(host.grid),
          receiver_count_(validated_receiver_count(host)),
          linear_indices_(detail::checked_size_product(
              receiver_count_, 24, "CUDA receiver stencil size overflow")),
          weights_(linear_indices_.size()) {
        std::vector<std::size_t> indices(linear_indices_.size());
        std::vector<double> weights(weights_.size());
        for (std::size_t receiver = 0; receiver < receiver_count_; ++receiver) {
            const std::array<const TrilinearStencil*, 3> stencils{{
                &host.receivers[receiver].vx,
                &host.receivers[receiver].vy,
                &host.receivers[receiver].vz}};
            for (std::size_t component = 0; component < stencils.size();
                 ++component) {
                for (std::size_t node = 0; node < 8; ++node) {
                    const auto flat = (receiver * 3 + component) * 8 + node;
                    indices[flat] =
                        stencils[component]->nodes[node].linear_index;
                    weights[flat] = stencils[component]->nodes[node].weight;
                }
            }
        }
        linear_indices_.copy_from_host(indices.data(), indices.size());
        weights_.copy_from_host(weights.data(), weights.size());
    }

    DeviceReceiverSet(const DeviceReceiverSet&) = delete;
    DeviceReceiverSet& operator=(const DeviceReceiverSet&) = delete;
    DeviceReceiverSet(DeviceReceiverSet&&) noexcept = default;
    DeviceReceiverSet& operator=(DeviceReceiverSet&&) noexcept = default;

    [[nodiscard]] const Grid3D& grid() const noexcept { return grid_; }
    [[nodiscard]] std::size_t receiver_count() const noexcept {
        return receiver_count_;
    }
    [[nodiscard]] std::size_t bytes() const {
        return linear_indices_.bytes() + weights_.bytes();
    }

private:
    friend void sample_receivers_after_velocity_step(
        const DeviceElasticWavefield&,
        const DeviceReceiverSet&,
        class DeviceReceiverTraces&,
        std::size_t);

    [[nodiscard]] static std::size_t validated_receiver_count(
        const PreparedReceiverSet& host) {
        require_valid_prepared_receiver_set(host);
        return host.receivers.size();
    }

    Grid3D grid_{};
    std::size_t receiver_count_{0};
    DeviceBuffer<std::size_t> linear_indices_;
    DeviceBuffer<double> weights_;
};

class DeviceReceiverTraces {
public:
    DeviceReceiverTraces(
        std::size_t receiver_count,
        std::size_t sample_count,
        double dt_s)
        : receiver_count_(receiver_count),
          sample_count_(sample_count),
          dt_s_(dt_s),
          vx_m_s_(validated_trace_count(receiver_count, sample_count, dt_s)),
          vy_m_s_(vx_m_s_.size()),
          vz_m_s_(vx_m_s_.size()) {
        vx_m_s_.zero();
        vy_m_s_.zero();
        vz_m_s_.zero();
    }

    DeviceReceiverTraces(const DeviceReceiverTraces&) = delete;
    DeviceReceiverTraces& operator=(const DeviceReceiverTraces&) = delete;
    DeviceReceiverTraces(DeviceReceiverTraces&&) noexcept = default;
    DeviceReceiverTraces& operator=(DeviceReceiverTraces&&) noexcept = default;

    void download(
        std::vector<float>& vx_m_s,
        std::vector<float>& vy_m_s,
        std::vector<float>& vz_m_s) const {
        require_host_trace_size(vx_m_s);
        require_host_trace_size(vy_m_s);
        require_host_trace_size(vz_m_s);
        vx_m_s_.copy_to_host(vx_m_s.data(), vx_m_s.size());
        vy_m_s_.copy_to_host(vy_m_s.data(), vy_m_s.size());
        vz_m_s_.copy_to_host(vz_m_s.data(), vz_m_s.size());
    }

    [[nodiscard]] std::size_t receiver_count() const noexcept {
        return receiver_count_;
    }
    [[nodiscard]] std::size_t sample_count() const noexcept {
        return sample_count_;
    }
    [[nodiscard]] double dt_s() const noexcept { return dt_s_; }
    [[nodiscard]] std::size_t bytes() const {
        return detail::checked_size_product(
            3, vx_m_s_.bytes(), "CUDA receiver trace bytes overflow");
    }

private:
    friend void sample_receivers_after_velocity_step(
        const DeviceElasticWavefield&,
        const DeviceReceiverSet&,
        DeviceReceiverTraces&,
        std::size_t);

    [[nodiscard]] static std::size_t validated_trace_count(
        std::size_t receiver_count,
        std::size_t sample_count,
        double dt_s) {
        if (receiver_count == 0 || sample_count == 0) {
            throw std::invalid_argument(
                "CUDA receiver trace dimensions must be positive");
        }
        if (!std::isfinite(dt_s) || !(dt_s > 0.0)) {
            throw std::invalid_argument(
                "CUDA receiver trace dt must be finite and positive");
        }
        return detail::checked_size_product(
            receiver_count,
            sample_count,
            "CUDA receiver trace size overflow");
    }

    void require_host_trace_size(const std::vector<float>& trace) const {
        if (trace.size() != vx_m_s_.size()) {
            throw std::invalid_argument(
                "host receiver trace size does not match device storage");
        }
    }

    std::size_t receiver_count_{0};
    std::size_t sample_count_{0};
    double dt_s_{0.0};
    DeviceBuffer<float> vx_m_s_;
    DeviceBuffer<float> vy_m_s_;
    DeviceBuffer<float> vz_m_s_;
};

void update_elastic_stresses(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    double dt_s);

void inject_moment_tensor_source(
    DeviceElasticWavefield& wavefield,
    const DeviceMomentTensorSource& source,
    std::size_t step_index_n,
    double dt_s);

void update_elastic_velocities(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    double dt_s);

void sample_receivers_after_velocity_step(
    const DeviceElasticWavefield& wavefield,
    const DeviceReceiverSet& receivers,
    DeviceReceiverTraces& traces,
    std::size_t step_index_n);

void advance_elastic_interior_step(
    DeviceElasticWavefield& wavefield,
    const DeviceElasticCoefficients& coefficients,
    const DeviceMomentTensorSource& source,
    const DeviceReceiverSet& receivers,
    DeviceReceiverTraces& traces,
    std::size_t step_index_n);

} // namespace wave3d::cuda
