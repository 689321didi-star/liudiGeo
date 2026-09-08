#include "wave3d/acquisition/moment_source_injector.hpp"
#include "wave3d/acquisition/receiver_sampler.hpp"
#include "wave3d/cuda/cuda_error.hpp"
#include "wave3d/cuda/elastic_propagator.hpp"
#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/model/physical_model.hpp"
#include "wave3d/physics/cpu_elastic_step.hpp"
#include "wave3d/physics/cpu_elastic_update.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr double dt_s = 0.0005;
constexpr std::size_t sample_count = 8;
constexpr double error_limit = 2.0e-5;
int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Exception, typename Function>
void expect_throws(Function&& function, const std::string& message) {
    bool threw = false;
    try {
        function();
    } catch (const Exception&) {
        threw = true;
    }
    expect(threw, message);
}

[[nodiscard]] wave3d::Grid3D validation_grid() {
    return {
        15,
        15,
        15,
        10.0F,
        10.0F,
        10.0F,
        wave3d::staggered_fd_radius,
        {},
        {},
        {}};
}

[[nodiscard]] wave3d::ElasticCoefficients validation_coefficients(
    const wave3d::Grid3D& grid) {
    return wave3d::prepare_elastic_coefficients(
        wave3d::make_homogeneous_model(grid, {3200.0F, 2200.0F, 2500.0F}));
}

[[nodiscard]] double normalized_max_error(
    const std::vector<float>& actual,
    const std::vector<float>& reference) {
    if (actual.size() != reference.size()) {
        throw std::invalid_argument("comparison arrays must have equal sizes");
    }
    double maximum_error = 0.0;
    double maximum_reference = 0.0;
    for (std::size_t index = 0; index < actual.size(); ++index) {
        if (!std::isfinite(actual[index]) || !std::isfinite(reference[index])) {
            return std::numeric_limits<double>::infinity();
        }
        maximum_error = std::max(
            maximum_error,
            std::abs(
                static_cast<double>(actual[index]) -
                static_cast<double>(reference[index])));
        maximum_reference = std::max(
            maximum_reference,
            std::abs(static_cast<double>(reference[index])));
    }
    return maximum_error / std::max(maximum_reference, 1.0e-20);
}

void compare_field(
    const char* name,
    const std::vector<float>& actual,
    const std::vector<float>& reference) {
    const double error = normalized_max_error(actual, reference);
    std::cout << "CUDA comparison " << name
              << " normalized_max_error=" << std::setprecision(10) << error
              << '\n';
    expect(error <= error_limit, std::string(name) + " exceeds CUDA error limit");
}

void compare_wavefields(
    const wave3d::ElasticWavefield& actual,
    const wave3d::ElasticWavefield& reference) {
    compare_field("vx", actual.vx_m_s, reference.vx_m_s);
    compare_field("vy", actual.vy_m_s, reference.vy_m_s);
    compare_field("vz", actual.vz_m_s, reference.vz_m_s);
    compare_field("sxx", actual.sxx_pa, reference.sxx_pa);
    compare_field("syy", actual.syy_pa, reference.syy_pa);
    compare_field("szz", actual.szz_pa, reference.szz_pa);
    compare_field("sxy", actual.sxy_pa, reference.sxy_pa);
    compare_field("sxz", actual.sxz_pa, reference.sxz_pa);
    compare_field("syz", actual.syz_pa, reference.syz_pa);
}

void fill_manufactured_state(wave3d::ElasticWavefield& wavefield) {
    const auto fill = [](std::vector<float>& field, double scale, double phase) {
        for (std::size_t index = 0; index < field.size(); ++index) {
            const double coordinate = static_cast<double>(index);
            field[index] = static_cast<float>(
                scale * (std::sin(0.013 * coordinate + phase) +
                         0.31 * std::cos(0.007 * coordinate - phase)));
        }
    };
    fill(wavefield.vx_m_s, 1.0e-3, 0.1);
    fill(wavefield.vy_m_s, 1.3e-3, 0.3);
    fill(wavefield.vz_m_s, 0.8e-3, 0.5);
    fill(wavefield.sxx_pa, 1.0e5, 0.2);
    fill(wavefield.syy_pa, 1.2e5, 0.4);
    fill(wavefield.szz_pa, 0.9e5, 0.6);
    fill(wavefield.sxy_pa, 0.7e5, 0.8);
    fill(wavefield.sxz_pa, 0.6e5, 1.0);
    fill(wavefield.syz_pa, 0.5e5, 1.2);
}

void test_move_ownership() {
    static_assert(
        !std::is_copy_constructible<wave3d::cuda::DeviceElasticWavefield>::value,
        "device elastic wavefield must be move-only");
    static_assert(
        std::is_nothrow_move_constructible<
            wave3d::cuda::DeviceElasticWavefield>::value,
        "device elastic wavefield move must be noexcept");
    static_assert(
        !std::is_copy_constructible<
            wave3d::cuda::DeviceElasticCoefficients>::value,
        "device coefficients must be move-only");
    static_assert(
        !std::is_copy_constructible<
            wave3d::cuda::DeviceMomentTensorSource>::value,
        "device source must be move-only");
    static_assert(
        !std::is_copy_constructible<wave3d::cuda::DeviceReceiverSet>::value,
        "device receiver set must be move-only");
    static_assert(
        !std::is_copy_constructible<wave3d::cuda::DeviceReceiverTraces>::value,
        "device traces must be move-only");

    wave3d::cuda::DeviceElasticWavefield original(validation_grid());
    const auto cells = original.cell_count();
    wave3d::cuda::DeviceElasticWavefield moved(std::move(original));
    expect(moved.cell_count() == cells, "device wavefield move must retain storage");
}

void test_manufactured_full_field_comparison() {
    const auto grid = validation_grid();
    const auto coefficients = validation_coefficients(grid);
    wave3d::ElasticWavefield cpu(grid);
    wave3d::ElasticWavefield initial(grid);
    fill_manufactured_state(cpu);
    fill_manufactured_state(initial);

    wave3d::cuda::DeviceElasticCoefficients device_coefficients(coefficients);
    wave3d::cuda::DeviceElasticWavefield device_wavefield(grid);
    device_wavefield.upload(initial);

    wave3d::cpu_update_elastic_stresses(cpu, coefficients, dt_s);
    wave3d::cuda::update_elastic_stresses(
        device_wavefield, device_coefficients, dt_s);
    wave3d::cuda::synchronize();
    wave3d::ElasticWavefield gpu_after_stress(grid);
    device_wavefield.download(gpu_after_stress);
    compare_field("manufactured sxx", gpu_after_stress.sxx_pa, cpu.sxx_pa);
    compare_field("manufactured syy", gpu_after_stress.syy_pa, cpu.syy_pa);
    compare_field("manufactured szz", gpu_after_stress.szz_pa, cpu.szz_pa);
    compare_field("manufactured sxy", gpu_after_stress.sxy_pa, cpu.sxy_pa);
    compare_field("manufactured sxz", gpu_after_stress.sxz_pa, cpu.sxz_pa);
    compare_field("manufactured syz", gpu_after_stress.syz_pa, cpu.syz_pa);

    wave3d::cpu_update_elastic_velocities(cpu, coefficients, dt_s);
    wave3d::cuda::update_elastic_velocities(
        device_wavefield, device_coefficients, dt_s);
    wave3d::cuda::synchronize();
    wave3d::ElasticWavefield gpu(grid);
    device_wavefield.download(gpu);
    compare_wavefields(gpu, cpu);
}

struct ValidationInputs {
    wave3d::Grid3D grid;
    wave3d::ElasticCoefficients coefficients;
    wave3d::PreparedMomentTensorSource source;
    wave3d::PreparedReceiverSet receivers;
};

[[nodiscard]] ValidationInputs make_validation_inputs() {
    const auto grid = validation_grid();
    auto coefficients = validation_coefficients(grid);
    const auto raw_source = wave3d::prepare_moment_tensor_source(
        grid,
        {70.0, 70.0, 70.0},
        0.0,
        wave3d::isotropic_explosion(1.0e12),
        {40.0, 0.0, 1.0});
    auto source =
        wave3d::prepare_moment_tensor_source_stencils(grid, raw_source);
    const auto receiver_set = wave3d::prepare_receiver_set(
        grid,
        std::vector<wave3d::PhysicalPoint3D>{
            {45.0, 55.0, 65.0},
            {85.0, 65.0, 55.0},
            {65.0, 85.0, 75.0},
            {75.0, 45.0, 85.0}});
    auto receivers = wave3d::prepare_receiver_stencils(grid, receiver_set);
    return {grid, std::move(coefficients), std::move(source), std::move(receivers)};
}

void test_multistep_fields_and_traces() {
    auto inputs = make_validation_inputs();
    const auto receiver_count = inputs.receivers.receivers.size();
    const auto trace_count = receiver_count * sample_count;
    wave3d::ElasticWavefield cpu(inputs.grid);
    std::vector<wave3d::ReceiverVelocitySample> frame(receiver_count);
    std::vector<float> cpu_vx(trace_count);
    std::vector<float> cpu_vy(trace_count);
    std::vector<float> cpu_vz(trace_count);
    for (std::size_t step = 0; step < sample_count; ++step) {
        wave3d::cpu_advance_elastic_interior_step(
            cpu,
            inputs.coefficients,
            inputs.source,
            inputs.receivers,
            step,
            dt_s,
            frame);
        for (std::size_t receiver = 0; receiver < receiver_count; ++receiver) {
            const auto index = receiver * sample_count + step;
            cpu_vx[index] = frame[receiver].vx_m_s;
            cpu_vy[index] = frame[receiver].vy_m_s;
            cpu_vz[index] = frame[receiver].vz_m_s;
            expect(
                frame[receiver].time_s == static_cast<double>(step + 1) * dt_s,
                "CPU receiver time label changed");
        }
    }

    wave3d::cuda::DeviceElasticCoefficients device_coefficients(
        inputs.coefficients);
    wave3d::cuda::DeviceElasticWavefield device_wavefield(inputs.grid);
    wave3d::cuda::DeviceMomentTensorSource device_source(inputs.source);
    wave3d::cuda::DeviceReceiverSet device_receivers(inputs.receivers);
    wave3d::cuda::DeviceReceiverTraces device_traces(
        receiver_count, sample_count, dt_s);
    const auto owned_bytes = device_coefficients.bytes() +
                             device_wavefield.bytes() + device_source.bytes() +
                             device_receivers.bytes() + device_traces.bytes();

    wave3d::cuda::synchronize();
    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t step = 0; step < sample_count; ++step) {
        wave3d::cuda::advance_elastic_interior_step(
            device_wavefield,
            device_coefficients,
            device_source,
            device_receivers,
            device_traces,
            step);
    }
    wave3d::cuda::synchronize();
    const auto end = std::chrono::steady_clock::now();

    wave3d::ElasticWavefield gpu(inputs.grid);
    device_wavefield.download(gpu);
    std::vector<float> gpu_vx(trace_count);
    std::vector<float> gpu_vy(trace_count);
    std::vector<float> gpu_vz(trace_count);
    device_traces.download(gpu_vx, gpu_vy, gpu_vz);

    compare_wavefields(gpu, cpu);
    compare_field("trace vx", gpu_vx, cpu_vx);
    compare_field("trace vy", gpu_vy, cpu_vy);
    compare_field("trace vz", gpu_vz, cpu_vz);
    const auto corner = inputs.grid.linear_index(0, 0, 0);
    for (const auto* field : {
             &gpu.vx_m_s, &gpu.vy_m_s, &gpu.vz_m_s,
             &gpu.sxx_pa, &gpu.syy_pa, &gpu.szz_pa,
             &gpu.sxy_pa, &gpu.sxz_pa, &gpu.syz_pa}) {
        expect((*field)[corner] == 0.0F, "incomplete-stencil corner must stay zero");
    }
    expect(device_traces.dt_s() == dt_s, "device trace dt metadata changed");
    expect(
        device_traces.sample_count() == sample_count,
        "device trace sample count changed");

    const auto elapsed_us =
        std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
    std::cout << "CUDA eight-step elapsed_us=" << elapsed_us
              << " owned_device_bytes=" << owned_bytes << '\n';
}

void test_precondition_rejection() {
    auto inputs = make_validation_inputs();
    wave3d::cuda::DeviceElasticWavefield wavefield(inputs.grid);
    wave3d::cuda::DeviceElasticCoefficients coefficients(inputs.coefficients);
    wave3d::cuda::DeviceReceiverSet receivers(inputs.receivers);
    wave3d::cuda::DeviceReceiverTraces traces(
        receivers.receiver_count(), sample_count, dt_s);

    expect_throws<std::invalid_argument>(
        [&] { wave3d::cuda::update_elastic_stresses(wavefield, coefficients, 0.0); },
        "CUDA stress update must reject zero dt");
    expect_throws<std::out_of_range>(
        [&] {
            wave3d::cuda::sample_receivers_after_velocity_step(
                wavefield, receivers, traces, sample_count);
        },
        "CUDA sampling must reject an out-of-range sample");
    std::vector<float> wrong(1);
    std::vector<float> valid(receivers.receiver_count() * sample_count);
    expect_throws<std::invalid_argument>(
        [&] { traces.download(wrong, valid, valid); },
        "CUDA trace download must reject incorrect host storage");

    auto other_grid = inputs.grid;
    other_grid.dx_m = 11.0F;
    auto other_coefficients = validation_coefficients(other_grid);
    wave3d::cuda::DeviceElasticCoefficients other_device(other_coefficients);
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::cuda::update_elastic_velocities(
                wavefield, other_device, dt_s);
        },
        "CUDA update must reject a grid mismatch before launch");
}

} // namespace

int main() {
    try {
        test_move_ownership();
        test_manufactured_full_field_comparison();
        test_multistep_fields_and_traces();
        test_precondition_rejection();
    } catch (const std::exception& error) {
        std::cerr << "CUDA elastic test failure: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " CUDA elastic test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D CUDA elastic tests passed\n";
    return 0;
}
