#include "wave3d/boundary/cpml.hpp"
#include "wave3d/boundary/free_surface.hpp"
#include "wave3d/cuda/cpml.hpp"
#include "wave3d/cuda/cuda_error.hpp"
#include "wave3d/cuda/free_surface.hpp"
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
    const std::vector<float>& reference) {
    double error = 0.0;
    double scale = 0.0;
    for (std::size_t index = 0; index < actual.size(); ++index) {
        error = std::max(
            error,
            std::abs(static_cast<double>(actual[index]) - reference[index]));
        scale = std::max(scale, std::abs(static_cast<double>(reference[index])));
    }
    return error / std::max(scale, 1.0e-20);
}

void fill(wave3d::ElasticWavefield& wavefield) {
    double phase = 0.1;
    for (auto* field : {
             &wavefield.vx_m_s, &wavefield.vy_m_s, &wavefield.vz_m_s,
             &wavefield.sxx_pa, &wavefield.syy_pa, &wavefield.szz_pa,
             &wavefield.sxy_pa, &wavefield.sxz_pa, &wavefield.syz_pa}) {
        for (std::size_t index = 0; index < field->size(); ++index) {
            (*field)[index] = static_cast<float>(
                std::sin(0.007 * static_cast<double>(index) + phase));
        }
        phase += 0.2;
    }
}

void test_cpu_cuda_projection() {
    const wave3d::Grid3D grid{
        7, 7, 7,
        10.0F, 10.0F, 10.0F,
        6,
        {4, 4}, {4, 4}, {0, 4}};
    const auto surface = wave3d::prepare_traction_free_surface(grid);
    wave3d::ElasticWavefield cpu(grid);
    wave3d::ElasticWavefield initial(grid);
    fill(cpu);
    fill(initial);
    wave3d::apply_traction_free_stresses(cpu, surface);
    wave3d::apply_traction_free_velocities(cpu, surface);

    wave3d::cuda::DeviceElasticWavefield device(grid);
    wave3d::cuda::DeviceTractionFreeSurface device_surface(surface);
    device.upload(initial);
    wave3d::cuda::apply_traction_free_stresses(device, device_surface);
    wave3d::cuda::apply_traction_free_velocities(device, device_surface);
    wave3d::cuda::synchronize();
    wave3d::ElasticWavefield gpu(grid);
    device.download(gpu);

    const std::vector<float>* actual[] = {
        &gpu.vx_m_s, &gpu.vy_m_s, &gpu.vz_m_s,
        &gpu.sxx_pa, &gpu.syy_pa, &gpu.szz_pa,
        &gpu.sxy_pa, &gpu.sxz_pa, &gpu.syz_pa};
    const std::vector<float>* reference[] = {
        &cpu.vx_m_s, &cpu.vy_m_s, &cpu.vz_m_s,
        &cpu.sxx_pa, &cpu.syy_pa, &cpu.szz_pa,
        &cpu.sxy_pa, &cpu.sxz_pa, &cpu.syz_pa};
    double maximum = 0.0;
    for (std::size_t component = 0; component < 9; ++component) {
        maximum = std::max(
            maximum,
            normalized_max_error(*actual[component], *reference[component]));
    }
    std::cout << "free-surface CPU/CUDA error=" << maximum << '\n';
    expect(maximum == 0.0, "CPU and CUDA free-surface projection must match");
}

struct RunResult {
    std::vector<float> vz;
    bool finite{true};
    double maximum_traction{0.0};
};

[[nodiscard]] wave3d::CpmlParameters parameters(bool top_cpml) {
    return {
        dt_s,
        3200.0,
        30.0,
        1.0e-3,
        2.0,
        1.0,
        {true, true, true, true, top_cpml, true}};
}

[[nodiscard]] RunResult run_surface(std::size_t samples) {
    const wave3d::Grid3D grid{
        31, 31, 31,
        10.0F, 10.0F, 10.0F,
        6,
        {20, 20}, {20, 20}, {0, 20}};
    const auto coefficients = wave3d::prepare_elastic_coefficients(
        wave3d::make_homogeneous_model(
            grid, {3200.0F, 2200.0F, 2500.0F}));
    const auto source = wave3d::prepare_moment_tensor_source_stencils(
        grid,
        wave3d::prepare_moment_tensor_source(
            grid,
            {150.0, 150.0, 100.0},
            0.0,
            wave3d::isotropic_explosion(1.0e12),
            {30.0, 0.04, 1.0}));
    const auto receivers = wave3d::prepare_receiver_stencils(
        grid,
        wave3d::prepare_receiver_set(
            grid,
            std::vector<wave3d::PhysicalPoint3D>{{150.0, 150.0, 0.0}}));
    const auto profile = wave3d::prepare_cpml_profile(grid, parameters(false));
    const auto surface = wave3d::prepare_traction_free_surface(grid);

    wave3d::cuda::DeviceElasticCoefficients device_coefficients(coefficients);
    wave3d::cuda::DeviceElasticWavefield wavefield(grid);
    wave3d::cuda::DeviceMomentTensorSource device_source(source);
    wave3d::cuda::DeviceReceiverSet device_receivers(receivers);
    wave3d::cuda::DeviceReceiverTraces traces(1, samples, dt_s);
    wave3d::cuda::DeviceCpmlProfile device_profile(profile);
    wave3d::cuda::DeviceCpmlState state(grid);
    wave3d::cuda::DeviceTractionFreeSurface device_surface(surface);
    for (std::size_t step = 0; step < samples; ++step) {
        wave3d::cuda::advance_elastic_free_surface_step(
            wavefield,
            device_coefficients,
            device_source,
            device_receivers,
            traces,
            device_profile,
            state,
            device_surface,
            step);
    }
    wave3d::cuda::synchronize();
    std::vector<float> vx(samples);
    std::vector<float> vy(samples);
    RunResult result{std::vector<float>(samples), true, 0.0};
    traces.download(vx, vy, result.vz);
    wave3d::ElasticWavefield host(grid);
    wavefield.download(host);
    wave3d::CpmlState host_state(grid);
    state.download(host_state);
    const auto k0 = grid.physical_origin_z();
    for (std::size_t y = 0; y < grid.allocated_ny(); ++y) {
        for (std::size_t x = 0; x < grid.allocated_nx(); ++x) {
            const auto integer = grid.linear_index(x, y, k0);
            const auto upper_half = grid.linear_index(x, y, k0 - 1);
            result.maximum_traction = std::max({
                result.maximum_traction,
                std::abs(static_cast<double>(host.szz_pa[integer])),
                std::abs(0.5 * static_cast<double>(
                    host.sxz_pa[upper_half] + host.sxz_pa[integer])),
                std::abs(0.5 * static_cast<double>(
                    host.syz_pa[upper_half] + host.syz_pa[integer]))});
        }
    }
    for (const auto* field : {
             &host.vx_m_s, &host.vy_m_s, &host.vz_m_s,
             &host.sxx_pa, &host.syy_pa, &host.szz_pa,
             &host.sxy_pa, &host.sxz_pa, &host.syz_pa}) {
        result.finite = result.finite &&
            std::all_of(field->begin(), field->end(), [](float value) {
                return std::isfinite(value);
            });
    }
    for (const auto& field : host_state.fields) {
        result.finite = result.finite &&
            std::all_of(field.begin(), field.end(), [](float value) {
                return std::isfinite(value);
            });
    }
    result.finite = result.finite &&
        std::all_of(result.vz.begin(), result.vz.end(), [](float value) {
            return std::isfinite(value);
        });
    return result;
}

[[nodiscard]] std::vector<float> run_reference(std::size_t samples) {
    const wave3d::Grid3D grid{
        31, 31, 61,
        10.0F, 10.0F, 10.0F,
        6,
        {20, 20}, {20, 20}, {20, 20}};
    const auto coefficients = wave3d::prepare_elastic_coefficients(
        wave3d::make_homogeneous_model(
            grid, {3200.0F, 2200.0F, 2500.0F}));
    const auto source = wave3d::prepare_moment_tensor_source_stencils(
        grid,
        wave3d::prepare_moment_tensor_source(
            grid,
            {150.0, 150.0, 300.0},
            0.0,
            wave3d::isotropic_explosion(1.0e12),
            {30.0, 0.04, 1.0}));
    const auto receivers = wave3d::prepare_receiver_stencils(
        grid,
        wave3d::prepare_receiver_set(
            grid,
            std::vector<wave3d::PhysicalPoint3D>{{150.0, 150.0, 200.0}}));
    const auto profile = wave3d::prepare_cpml_profile(grid, parameters(true));
    wave3d::cuda::DeviceElasticCoefficients device_coefficients(coefficients);
    wave3d::cuda::DeviceElasticWavefield wavefield(grid);
    wave3d::cuda::DeviceMomentTensorSource device_source(source);
    wave3d::cuda::DeviceReceiverSet device_receivers(receivers);
    wave3d::cuda::DeviceReceiverTraces traces(1, samples, dt_s);
    wave3d::cuda::DeviceCpmlProfile device_profile(profile);
    wave3d::cuda::DeviceCpmlState state(grid);
    for (std::size_t step = 0; step < samples; ++step) {
        wave3d::cuda::advance_elastic_cpml_step(
            wavefield,
            device_coefficients,
            device_source,
            device_receivers,
            traces,
            device_profile,
            state,
            step);
    }
    wave3d::cuda::synchronize();
    std::vector<float> vx(samples);
    std::vector<float> vy(samples);
    std::vector<float> vz(samples);
    traces.download(vx, vy, vz);
    return vz;
}

struct Peak {
    std::size_t sample{0};
    float value{0.0F};
};

[[nodiscard]] Peak peak_in_window(
    const std::vector<float>& trace,
    std::size_t begin,
    std::size_t end) {
    Peak peak{begin, trace[begin]};
    for (std::size_t sample = begin + 1; sample < end; ++sample) {
        if (std::abs(trace[sample]) > std::abs(peak.value)) {
            peak = {sample, trace[sample]};
        }
    }
    return peak;
}

[[nodiscard]] double correlation(
    const std::vector<float>& first,
    const std::vector<float>& second,
    std::size_t begin,
    std::size_t end) {
    double dot = 0.0;
    double first_norm = 0.0;
    double second_norm = 0.0;
    for (std::size_t sample = begin; sample < end; ++sample) {
        dot += static_cast<double>(first[sample]) * second[sample];
        first_norm += static_cast<double>(first[sample]) * first[sample];
        second_norm += static_cast<double>(second[sample]) * second[sample];
    }
    return dot / std::sqrt(first_norm * second_norm);
}

void test_surface_physics_and_stability() {
    constexpr std::size_t samples = 600;
    const auto surface = run_surface(samples);
    const auto reference = run_reference(samples);
    const auto surface_peak = peak_in_window(surface.vz, 110, 180);
    const auto reference_peak = peak_in_window(reference, 110, 180);
    const double amplitude_ratio =
        std::abs(static_cast<double>(surface_peak.value) /
                 reference_peak.value);
    const double waveform_correlation =
        correlation(surface.vz, reference, 110, 180);
    const auto sample_difference = surface_peak.sample > reference_peak.sample
                                       ? surface_peak.sample - reference_peak.sample
                                       : reference_peak.sample - surface_peak.sample;
    std::cout << std::setprecision(10)
              << "free_surface_peak_sample=" << surface_peak.sample
              << " reference_peak_sample=" << reference_peak.sample
              << " amplitude_ratio=" << amplitude_ratio
              << " correlation=" << waveform_correlation
              << " surface_peak=" << surface_peak.value
              << " reference_peak=" << reference_peak.value
              << " max_traction=" << surface.maximum_traction << '\n';
    expect(sample_difference <= 1, "free-surface arrival time changed");
    expect(
        surface_peak.value < 0.0F && reference_peak.value < 0.0F,
        "normal incident/free-surface velocity must have upward polarity");
    expect(waveform_correlation >= 0.98, "free-surface waveform correlation is low");
    expect(
        amplitude_ratio >= 1.6 && amplitude_ratio <= 2.4,
        "free-surface velocity amplitude does not bracket doubling");
    expect(surface.maximum_traction == 0.0, "surface traction must be exact zero");
    expect(surface.finite, "600-step free-surface run must remain finite");
}

} // namespace

int main() {
    try {
        test_cpu_cuda_projection();
        if (std::getenv("WAVE3D_SANITIZER_FAST") == nullptr) {
            test_surface_physics_and_stability();
        }
    } catch (const std::exception& error) {
        std::cerr << "CUDA free-surface test failure: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " CUDA free-surface test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D CUDA free-surface tests passed\n";
    return 0;
}
