#include "wave3d/acquisition/moment_source_injector.hpp"
#include "wave3d/acquisition/receiver_sampler.hpp"
#include "wave3d/boundary/cpml.hpp"
#include "wave3d/cuda/cpml.hpp"
#include "wave3d/cuda/cuda_error.hpp"
#include "wave3d/cuda/elastic_propagator.hpp"
#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/model/physical_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
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
int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] double normalized_max_error(
    const std::vector<float>& actual,
    const std::vector<float>& reference,
    std::size_t begin = 0,
    std::size_t end = std::numeric_limits<std::size_t>::max()) {
    end = std::min({end, actual.size(), reference.size()});
    double maximum_error = 0.0;
    double maximum_reference = 0.0;
    for (std::size_t index = begin; index < end; ++index) {
        if (!std::isfinite(actual[index]) || !std::isfinite(reference[index])) {
            return std::numeric_limits<double>::infinity();
        }
        maximum_error = std::max(
            maximum_error,
            std::abs(static_cast<double>(actual[index]) - reference[index]));
        maximum_reference = std::max(
            maximum_reference,
            std::abs(static_cast<double>(reference[index])));
    }
    return maximum_error / std::max(maximum_reference, 1.0e-20);
}

[[nodiscard]] wave3d::CpmlParameters cpml_parameters(
    wave3d::CpmlSides sides = {}) {
    return {dt_s, 3200.0, 30.0, 1.0e-3, 2.0, 1.0, sides};
}

void test_small_cpu_gpu_comparison() {
    static_assert(
        !std::is_copy_constructible<wave3d::cuda::DeviceCpmlProfile>::value,
        "device CPML profile must be move-only");
    static_assert(
        !std::is_copy_constructible<wave3d::cuda::DeviceCpmlState>::value,
        "device CPML state must be move-only");
    const wave3d::Grid3D grid{
        9, 9, 9,
        10.0F, 10.0F, 10.0F,
        6,
        {4, 4}, {4, 4}, {4, 4}};
    const auto coefficients = wave3d::prepare_elastic_coefficients(
        wave3d::make_homogeneous_model(
            grid, {3200.0F, 2200.0F, 2500.0F}));
    const auto profile =
        wave3d::prepare_cpml_profile(grid, cpml_parameters());
    const auto raw_source = wave3d::prepare_moment_tensor_source(
        grid,
        {40.0, 40.0, 40.0},
        0.0,
        {1.0e12, -0.8e12, 0.6e12, 0.3e12, -0.2e12, 0.1e12},
        {30.0, 0.0, 1.0});
    const auto source =
        wave3d::prepare_moment_tensor_source_stencils(grid, raw_source);
    const auto receivers = wave3d::prepare_receiver_stencils(
        grid,
        wave3d::prepare_receiver_set(
            grid,
            std::vector<wave3d::PhysicalPoint3D>{
                {25.0, 35.0, 45.0}, {55.0, 45.0, 35.0}}));
    constexpr std::size_t samples = 8;

    wave3d::ElasticWavefield cpu_wavefield(grid);
    wave3d::CpmlState cpu_state(grid);
    std::vector<wave3d::ReceiverVelocitySample> frame(2);
    std::vector<float> cpu_vx(2 * samples);
    std::vector<float> cpu_vy(2 * samples);
    std::vector<float> cpu_vz(2 * samples);
    for (std::size_t step = 0; step < samples; ++step) {
        wave3d::cpu_update_elastic_stresses_cpml(
            cpu_wavefield, coefficients, profile, cpu_state);
        wave3d::inject_moment_tensor_source(
            cpu_wavefield, source, step, dt_s);
        wave3d::cpu_update_elastic_velocities_cpml(
            cpu_wavefield, coefficients, profile, cpu_state);
        wave3d::sample_receivers_after_velocity_step(
            cpu_wavefield, receivers, step, dt_s, frame);
        for (std::size_t receiver = 0; receiver < 2; ++receiver) {
            const auto index = receiver * samples + step;
            cpu_vx[index] = frame[receiver].vx_m_s;
            cpu_vy[index] = frame[receiver].vy_m_s;
            cpu_vz[index] = frame[receiver].vz_m_s;
        }
    }

    wave3d::cuda::DeviceElasticCoefficients device_coefficients(coefficients);
    wave3d::cuda::DeviceElasticWavefield device_wavefield(grid);
    wave3d::cuda::DeviceMomentTensorSource device_source(source);
    wave3d::cuda::DeviceReceiverSet device_receivers(receivers);
    wave3d::cuda::DeviceReceiverTraces device_traces(2, samples, dt_s);
    wave3d::cuda::DeviceCpmlProfile device_profile(profile);
    wave3d::cuda::DeviceCpmlState device_state(grid);
    for (std::size_t step = 0; step < samples; ++step) {
        wave3d::cuda::advance_elastic_cpml_step(
            device_wavefield,
            device_coefficients,
            device_source,
            device_receivers,
            device_traces,
            device_profile,
            device_state,
            step);
    }
    wave3d::cuda::synchronize();
    wave3d::ElasticWavefield gpu_wavefield(grid);
    wave3d::CpmlState gpu_state(grid);
    device_wavefield.download(gpu_wavefield);
    device_state.download(gpu_state);
    std::vector<float> gpu_vx(2 * samples);
    std::vector<float> gpu_vy(2 * samples);
    std::vector<float> gpu_vz(2 * samples);
    device_traces.download(gpu_vx, gpu_vy, gpu_vz);

    const std::vector<float>* gpu_fields[] = {
        &gpu_wavefield.vx_m_s, &gpu_wavefield.vy_m_s,
        &gpu_wavefield.vz_m_s, &gpu_wavefield.sxx_pa,
        &gpu_wavefield.syy_pa, &gpu_wavefield.szz_pa,
        &gpu_wavefield.sxy_pa, &gpu_wavefield.sxz_pa,
        &gpu_wavefield.syz_pa};
    const std::vector<float>* cpu_fields[] = {
        &cpu_wavefield.vx_m_s, &cpu_wavefield.vy_m_s,
        &cpu_wavefield.vz_m_s, &cpu_wavefield.sxx_pa,
        &cpu_wavefield.syy_pa, &cpu_wavefield.szz_pa,
        &cpu_wavefield.sxy_pa, &cpu_wavefield.sxz_pa,
        &cpu_wavefield.syz_pa};
    double field_error = 0.0;
    for (std::size_t component = 0; component < 9; ++component) {
        field_error = std::max(
            field_error,
            normalized_max_error(*gpu_fields[component], *cpu_fields[component]));
    }
    double state_error = 0.0;
    for (std::size_t field = 0; field < wave3d::cpml_memory_field_count;
         ++field) {
        state_error = std::max(
            state_error,
            normalized_max_error(gpu_state.fields[field], cpu_state.fields[field]));
    }
    const double trace_error = std::max({
        normalized_max_error(gpu_vx, cpu_vx),
        normalized_max_error(gpu_vy, cpu_vy),
        normalized_max_error(gpu_vz, cpu_vz)});
    std::cout << std::setprecision(10)
              << "CPML CPU/GPU field_error=" << field_error
              << " state_error=" << state_error
              << " trace_error=" << trace_error
              << " device_cpml_bytes="
              << device_profile.bytes() + device_state.bytes() << '\n';
    expect(field_error <= 2.0e-5, "CPML CUDA fields exceed tolerance");
    expect(state_error <= 2.0e-5, "CPML CUDA state exceeds tolerance");
    expect(trace_error <= 2.0e-5, "CPML CUDA traces exceed tolerance");
}

struct RunDefinition {
    wave3d::Grid3D grid;
    wave3d::PhysicalPoint3D source;
    wave3d::PhysicalPoint3D receiver;
    std::size_t sample_count{0};
    bool cpml{true};
};

struct RunResult {
    std::vector<float> vx;
    std::vector<float> vy;
    std::vector<float> vz;
    bool finite{true};
};

[[nodiscard]] RunResult run(const RunDefinition& definition) {
    const auto coefficients = wave3d::prepare_elastic_coefficients(
        wave3d::make_homogeneous_model(
            definition.grid, {3200.0F, 2200.0F, 2500.0F}));
    const auto source = wave3d::prepare_moment_tensor_source_stencils(
        definition.grid,
        wave3d::prepare_moment_tensor_source(
            definition.grid,
            definition.source,
            0.0,
            wave3d::isotropic_explosion(1.0e12),
            {30.0, 0.04, 1.0}));
    const auto receivers = wave3d::prepare_receiver_stencils(
        definition.grid,
        wave3d::prepare_receiver_set(
            definition.grid,
            std::vector<wave3d::PhysicalPoint3D>{definition.receiver}));
    const auto profile = wave3d::prepare_cpml_profile(
        definition.grid, cpml_parameters());

    wave3d::cuda::DeviceElasticCoefficients device_coefficients(coefficients);
    wave3d::cuda::DeviceElasticWavefield wavefield(definition.grid);
    wave3d::cuda::DeviceMomentTensorSource device_source(source);
    wave3d::cuda::DeviceReceiverSet device_receivers(receivers);
    wave3d::cuda::DeviceReceiverTraces traces(
        1, definition.sample_count, dt_s);
    wave3d::cuda::DeviceCpmlProfile device_profile(profile);
    wave3d::cuda::DeviceCpmlState state(definition.grid);
    for (std::size_t step = 0; step < definition.sample_count; ++step) {
        if (definition.cpml) {
            wave3d::cuda::advance_elastic_cpml_step(
                wavefield,
                device_coefficients,
                device_source,
                device_receivers,
                traces,
                device_profile,
                state,
                step);
        } else {
            wave3d::cuda::advance_elastic_interior_step(
                wavefield,
                device_coefficients,
                device_source,
                device_receivers,
                traces,
                step);
        }
    }
    wave3d::cuda::synchronize();
    RunResult result{
        std::vector<float>(definition.sample_count),
        std::vector<float>(definition.sample_count),
        std::vector<float>(definition.sample_count),
        true};
    traces.download(result.vx, result.vy, result.vz);
    wave3d::ElasticWavefield final_wavefield(definition.grid);
    wavefield.download(final_wavefield);
    wave3d::CpmlState final_state(definition.grid);
    state.download(final_state);
    for (const auto* field : {
             &final_wavefield.vx_m_s, &final_wavefield.vy_m_s,
             &final_wavefield.vz_m_s, &final_wavefield.sxx_pa,
             &final_wavefield.syy_pa, &final_wavefield.szz_pa,
             &final_wavefield.sxy_pa, &final_wavefield.sxz_pa,
             &final_wavefield.syz_pa}) {
        result.finite = result.finite &&
            std::all_of(field->begin(), field->end(), [](float value) {
                return std::isfinite(value);
            });
    }
    for (const auto& field : final_state.fields) {
        result.finite = result.finite &&
            std::all_of(field.begin(), field.end(), [](float value) {
                return std::isfinite(value);
            });
    }
    for (const auto* trace : {&result.vx, &result.vy, &result.vz}) {
        result.finite = result.finite &&
            std::all_of(trace->begin(), trace->end(), [](float value) {
                return std::isfinite(value);
            });
    }
    return result;
}

[[nodiscard]] double peak_abs(
    const std::vector<float>& trace,
    std::size_t begin,
    std::size_t end) {
    double result = 0.0;
    for (std::size_t sample = begin; sample < std::min(end, trace.size());
         ++sample) {
        result = std::max(
            result, std::abs(static_cast<double>(trace[sample])));
    }
    return result;
}

void test_normal_reflection_and_long_stability() {
    const wave3d::Grid3D grid{
        31, 31, 31,
        10.0F, 10.0F, 10.0F,
        6,
        {20, 20}, {20, 20}, {20, 20}};
    const auto undamped = run({
        grid, {150.0, 150.0, 150.0}, {200.0, 150.0, 150.0}, 800, false});
    const auto cpml = run({
        grid, {150.0, 150.0, 150.0}, {200.0, 150.0, 150.0}, 800, true});
    const double undamped_late = peak_abs(undamped.vx, 350, 520);
    const double cpml_late = peak_abs(cpml.vx, 350, 520);
    const double ratio = cpml_late / undamped_late;
    std::cout << std::setprecision(10)
              << "CPML normal_reflection_ratio=" << ratio
              << " undamped_peak=" << undamped_late
              << " cpml_peak=" << cpml_late << '\n';
    expect(ratio <= 0.005, "normal-incidence CPML reflection exceeds limit");
    expect(cpml.finite, "800-step CPML run must remain finite");
}

void test_grazing_reflection() {
    const wave3d::Grid3D small{
        51, 51, 31,
        10.0F, 10.0F, 10.0F,
        6,
        {20, 20}, {20, 20}, {20, 20}};
    const wave3d::Grid3D reference{
        91, 51, 31,
        10.0F, 10.0F, 10.0F,
        6,
        {20, 20}, {20, 20}, {20, 20}};
    constexpr std::size_t samples = 420;
    const auto grazing = run({
        small, {40.0, 150.0, 150.0}, {40.0, 350.0, 150.0}, samples, true});
    const auto translated_reference = run({
        reference,
        {440.0, 150.0, 150.0},
        {440.0, 350.0, 150.0},
        samples,
        true});
    double residual_peak = 0.0;
    for (std::size_t sample = 150; sample < 360; ++sample) {
        residual_peak = std::max(
            residual_peak,
            std::abs(
                static_cast<double>(grazing.vy[sample]) -
                translated_reference.vy[sample]));
    }
    const double direct_peak = peak_abs(translated_reference.vy, 150, 280);
    const double ratio = residual_peak / direct_peak;
    std::cout << "CPML grazing_residual_ratio=" << ratio
              << " residual_peak=" << residual_peak
              << " direct_peak=" << direct_peak << '\n';
    expect(ratio <= 0.02, "grazing-incidence CPML residual exceeds limit");
}

} // namespace

int main() {
    try {
        test_small_cpu_gpu_comparison();
        if (std::getenv("WAVE3D_SANITIZER_FAST") == nullptr) {
            test_normal_reflection_and_long_stability();
            test_grazing_reflection();
        }
    } catch (const std::exception& error) {
        std::cerr << "CUDA CPML test failure: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " CUDA CPML test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D CUDA CPML tests passed\n";
    return 0;
}
