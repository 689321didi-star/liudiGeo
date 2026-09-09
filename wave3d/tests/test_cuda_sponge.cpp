#include "wave3d/boundary/sponge.hpp"
#include "wave3d/cuda/cuda_error.hpp"
#include "wave3d/cuda/elastic_propagator.hpp"
#include "wave3d/cuda/sponge.hpp"
#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/model/physical_model.hpp"

#include <algorithm>
#include <chrono>
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
        maximum_error = std::max(
            maximum_error,
            std::abs(
                static_cast<double>(actual[index]) - reference[index]));
        maximum_reference = std::max(
            maximum_reference,
            std::abs(static_cast<double>(reference[index])));
    }
    return maximum_error / std::max(maximum_reference, 1.0e-20);
}

void fill_fields(wave3d::ElasticWavefield& wavefield) {
    double phase = 0.1;
    for (auto* field : {
             &wavefield.vx_m_s, &wavefield.vy_m_s, &wavefield.vz_m_s,
             &wavefield.sxx_pa, &wavefield.syy_pa, &wavefield.szz_pa,
             &wavefield.sxy_pa, &wavefield.sxz_pa, &wavefield.syz_pa}) {
        for (std::size_t index = 0; index < field->size(); ++index) {
            (*field)[index] = static_cast<float>(
                std::sin(0.01 * static_cast<double>(index) + phase));
        }
        phase += 0.2;
    }
}

void test_cpu_cuda_application() {
    static_assert(
        !std::is_copy_constructible<wave3d::cuda::DeviceSpongeProfile>::value,
        "device sponge profile must be move-only");
    const wave3d::Grid3D grid{
        5, 5, 5,
        10.0F, 10.0F, 10.0F,
        6,
        {4, 3}, {2, 5}, {3, 4}};
    const auto profile = wave3d::prepare_sponge_profile(grid, 0.75);
    wave3d::ElasticWavefield initial(grid);
    wave3d::ElasticWavefield cpu(grid);
    fill_fields(initial);
    fill_fields(cpu);
    wave3d::apply_sponge_to_stresses(cpu, profile);
    wave3d::apply_sponge_to_velocities(cpu, profile);

    wave3d::cuda::DeviceElasticWavefield device_wavefield(grid);
    wave3d::cuda::DeviceSpongeProfile device_profile(profile);
    device_wavefield.upload(initial);
    wave3d::cuda::apply_sponge_to_stresses(device_wavefield, device_profile);
    wave3d::cuda::apply_sponge_to_velocities(device_wavefield, device_profile);
    wave3d::cuda::synchronize();
    wave3d::ElasticWavefield gpu(grid);
    device_wavefield.download(gpu);

    double maximum = 0.0;
    const std::vector<float>* gpu_fields[] = {
        &gpu.vx_m_s, &gpu.vy_m_s, &gpu.vz_m_s,
        &gpu.sxx_pa, &gpu.syy_pa, &gpu.szz_pa,
        &gpu.sxy_pa, &gpu.sxz_pa, &gpu.syz_pa};
    const std::vector<float>* cpu_fields[] = {
        &cpu.vx_m_s, &cpu.vy_m_s, &cpu.vz_m_s,
        &cpu.sxx_pa, &cpu.syy_pa, &cpu.szz_pa,
        &cpu.sxy_pa, &cpu.sxz_pa, &cpu.syz_pa};
    for (std::size_t component = 0; component < 9; ++component) {
        maximum = std::max(
            maximum,
            normalized_max_error(*gpu_fields[component], *cpu_fields[component]));
    }
    std::cout << "sponge CPU/CUDA normalized_max_error=" << maximum << '\n';
    expect(maximum <= 2.0e-6, "CUDA sponge application differs from CPU");
}

struct TraceResult {
    std::vector<float> vx;
    std::vector<float> vy;
    std::vector<float> vz;
    double elapsed_ms{0.0};
    bool fields_finite{false};
};

[[nodiscard]] TraceResult run_case(bool damped, std::size_t sample_count) {
    const wave3d::Grid3D grid{
        31, 31, 31,
        10.0F, 10.0F, 10.0F,
        6,
        {20, 20}, {20, 20}, {20, 20}};
    const auto coefficients = wave3d::prepare_elastic_coefficients(
        wave3d::make_homogeneous_model(
            grid, {3200.0F, 2200.0F, 2500.0F}));
    const auto raw_source = wave3d::prepare_moment_tensor_source(
        grid,
        {150.0, 150.0, 150.0},
        0.0,
        wave3d::isotropic_explosion(1.0e12),
        {30.0, 0.04, 1.0});
    const auto source =
        wave3d::prepare_moment_tensor_source_stencils(grid, raw_source);
    const auto receivers = wave3d::prepare_receiver_stencils(
        grid,
        wave3d::prepare_receiver_set(
            grid,
            std::vector<wave3d::PhysicalPoint3D>{{200.0, 150.0, 150.0}}));
    const auto sponge = wave3d::prepare_sponge_profile(grid, 0.75);

    wave3d::cuda::DeviceElasticCoefficients device_coefficients(coefficients);
    wave3d::cuda::DeviceElasticWavefield device_wavefield(grid);
    wave3d::cuda::DeviceMomentTensorSource device_source(source);
    wave3d::cuda::DeviceReceiverSet device_receivers(receivers);
    wave3d::cuda::DeviceReceiverTraces device_traces(1, sample_count, dt_s);
    wave3d::cuda::DeviceSpongeProfile device_sponge(sponge);

    wave3d::cuda::synchronize();
    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t step = 0; step < sample_count; ++step) {
        if (damped) {
            wave3d::cuda::advance_elastic_sponge_step(
                device_wavefield,
                device_coefficients,
                device_source,
                device_receivers,
                device_traces,
                device_sponge,
                step);
        } else {
            wave3d::cuda::advance_elastic_interior_step(
                device_wavefield,
                device_coefficients,
                device_source,
                device_receivers,
                device_traces,
                step);
        }
    }
    wave3d::cuda::synchronize();
    const auto end = std::chrono::steady_clock::now();

    TraceResult result{
        std::vector<float>(sample_count),
        std::vector<float>(sample_count),
        std::vector<float>(sample_count),
        std::chrono::duration<double, std::milli>(end - begin).count(),
        true};
    device_traces.download(result.vx, result.vy, result.vz);
    wave3d::ElasticWavefield final_wavefield(grid);
    device_wavefield.download(final_wavefield);
    for (const auto* field : {
             &final_wavefield.vx_m_s,
             &final_wavefield.vy_m_s,
             &final_wavefield.vz_m_s,
             &final_wavefield.sxx_pa,
             &final_wavefield.syy_pa,
             &final_wavefield.szz_pa,
             &final_wavefield.sxy_pa,
             &final_wavefield.sxz_pa,
             &final_wavefield.syz_pa}) {
        result.fields_finite = result.fields_finite &&
            std::all_of(field->begin(), field->end(), [](float value) {
                return std::isfinite(value);
            });
    }
    result.fields_finite = result.fields_finite &&
        std::all_of(result.vx.begin(), result.vx.end(), [](float value) {
            return std::isfinite(value);
        }) &&
        std::all_of(result.vy.begin(), result.vy.end(), [](float value) {
            return std::isfinite(value);
        }) &&
        std::all_of(result.vz.begin(), result.vz.end(), [](float value) {
            return std::isfinite(value);
        });
    return result;
}

[[nodiscard]] double peak_abs(
    const std::vector<float>& values,
    std::size_t begin,
    std::size_t end) {
    double peak = 0.0;
    for (std::size_t index = begin; index < std::min(end, values.size()); ++index) {
        peak = std::max(peak, std::abs(static_cast<double>(values[index])));
    }
    return peak;
}

void test_reflection_and_long_stability() {
    constexpr std::size_t samples = 600;
    const auto undamped = run_case(false, samples);
    const auto damped = run_case(true, samples);
    const auto early_error =
        normalized_max_error(damped.vx, undamped.vx, 0, 70);
    const double undamped_late = peak_abs(undamped.vx, 350, 520);
    const double damped_late = peak_abs(damped.vx, 350, 520);
    const double reflection_ratio = damped_late / undamped_late;

    std::cout << std::setprecision(10)
              << "sponge early_normalized_error=" << early_error
              << " late_reflection_ratio=" << reflection_ratio
              << " undamped_late_peak=" << undamped_late
              << " damped_late_peak=" << damped_late
              << " undamped_ms=" << undamped.elapsed_ms
              << " damped_ms=" << damped.elapsed_ms << '\n';
    expect(early_error <= 1.0e-6, "sponge changed the pre-boundary record");
    expect(undamped_late > 0.0, "undamped reflection window must be nonzero");
    expect(reflection_ratio <= 0.12, "sponge reflection exceeds debug threshold");
    expect(damped.fields_finite, "600-step sponge run must remain finite");
}

} // namespace

int main() {
    try {
        test_cpu_cuda_application();
        if (std::getenv("WAVE3D_SANITIZER_FAST") == nullptr) {
            test_reflection_and_long_stability();
        }
    } catch (const std::exception& error) {
        std::cerr << "CUDA sponge test failure: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " CUDA sponge test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D CUDA sponge tests passed\n";
    return 0;
}
