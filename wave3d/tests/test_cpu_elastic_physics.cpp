#include "wave3d/acquisition/moment_source_injector.hpp"
#include "wave3d/acquisition/receiver_sampler.hpp"
#include "wave3d/diagnostics/elastic_energy.hpp"
#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/model/physical_model.hpp"
#include "wave3d/numerics/elastic_validation.hpp"
#include "wave3d/physics/cpu_elastic_step.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::atomic<bool> track_allocations{false};
std::atomic<std::size_t> tracked_allocation_count{0};

} // namespace

void* operator new(std::size_t size) {
    if (track_allocations.load(std::memory_order_relaxed)) {
        tracked_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    ::operator delete(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete[](memory);
}

namespace {

constexpr double dt_s = 0.0005;
constexpr double dominant_frequency_hz = 40.0;
constexpr double delayed_peak_s = 1.5 / dominant_frequency_hz;
constexpr double scalar_moment_nm = 1.0e12;
constexpr float vp_m_s = 3200.0F;
constexpr float vs_m_s = 2200.0F;
constexpr float density_kg_m3 = 2500.0F;
constexpr double receiver_radius_m = 90.0;
constexpr std::size_t arrival_sample_count = 180;
constexpr std::size_t causal_sample_count = 124;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Function>
void expect_throws(Function&& function, const std::string& message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] wave3d::Grid3D validation_grid() {
    return {
        41,
        41,
        41,
        10.0F,
        10.0F,
        10.0F,
        wave3d::staggered_fd_radius,
        {},
        {},
        {}};
}

[[nodiscard]] wave3d::PhysicalPoint3D source_point() {
    return {200.0, 200.0, 200.0};
}

enum ReceiverNumber : std::size_t {
    PositiveX = 0,
    NegativeX = 1,
    PositiveY = 2,
    NegativeY = 3,
    PositiveZ = 4,
    NegativeZ = 5,
};

[[nodiscard]] std::vector<wave3d::PhysicalPoint3D> axis_receivers() {
    const auto center = source_point();
    return {
        {center.x_m + receiver_radius_m, center.y_m, center.z_m},
        {center.x_m - receiver_radius_m, center.y_m, center.z_m},
        {center.x_m, center.y_m + receiver_radius_m, center.z_m},
        {center.x_m, center.y_m - receiver_radius_m, center.z_m},
        {center.x_m, center.y_m, center.z_m + receiver_radius_m},
        {center.x_m, center.y_m, center.z_m - receiver_radius_m}};
}

[[nodiscard]] wave3d::MomentTensorSource make_source(
    const wave3d::Grid3D& grid,
    const wave3d::SymmetricMomentTensor& moment,
    double peak_delay_s) {
    return wave3d::prepare_moment_tensor_source(
        grid,
        source_point(),
        0.0,
        moment,
        {dominant_frequency_hz, peak_delay_s, 1.0});
}

struct TraceBuffer {
    std::size_t receiver_count{0};
    std::size_t sample_count{0};
    std::vector<float> vx_m_s;
    std::vector<float> vy_m_s;
    std::vector<float> vz_m_s;

    TraceBuffer(std::size_t receivers, std::size_t samples)
        : receiver_count(receivers),
          sample_count(samples),
          vx_m_s(receivers * samples),
          vy_m_s(receivers * samples),
          vz_m_s(receivers * samples) {}

    [[nodiscard]] std::size_t index(
        std::size_t receiver,
        std::size_t sample) const {
        return receiver * sample_count + sample;
    }
};

class AllocationTrackingScope {
public:
    AllocationTrackingScope() {
        tracked_allocation_count.store(0, std::memory_order_relaxed);
        track_allocations.store(true, std::memory_order_relaxed);
    }

    AllocationTrackingScope(const AllocationTrackingScope&) = delete;
    AllocationTrackingScope& operator=(const AllocationTrackingScope&) = delete;

    ~AllocationTrackingScope() {
        track_allocations.store(false, std::memory_order_relaxed);
    }
};

void run_steps(
    wave3d::ElasticWavefield& wavefield,
    const wave3d::ElasticCoefficients& coefficients,
    const wave3d::PreparedMomentTensorSource& source,
    const wave3d::PreparedReceiverSet& receivers,
    TraceBuffer& traces,
    std::vector<double>* energy_j = nullptr) {
    expect(
        traces.receiver_count == receivers.receivers.size(),
        "trace receiver count must match prepared receivers");
    expect(
        energy_j == nullptr || energy_j->size() == traces.sample_count,
        "energy storage must be preallocated to the sample count");

    std::vector<wave3d::ReceiverVelocitySample> frame(
        traces.receiver_count);
    const auto* const frame_data = frame.data();
    const auto* const vx_data = traces.vx_m_s.data();
    const auto* const vy_data = traces.vy_m_s.data();
    const auto* const vz_data = traces.vz_m_s.data();
    const auto frame_capacity = frame.capacity();
    const auto vx_capacity = traces.vx_m_s.capacity();
    const auto vy_capacity = traces.vy_m_s.capacity();
    const auto vz_capacity = traces.vz_m_s.capacity();

    {
        AllocationTrackingScope allocation_scope;
        for (std::size_t step = 0; step < traces.sample_count; ++step) {
            wave3d::cpu_advance_elastic_interior_step(
                wavefield,
                coefficients,
                source,
                receivers,
                step,
                dt_s,
                frame);
            for (std::size_t receiver = 0;
                 receiver < traces.receiver_count;
                 ++receiver) {
                const auto trace_index = traces.index(receiver, step);
                traces.vx_m_s[trace_index] = frame[receiver].vx_m_s;
                traces.vy_m_s[trace_index] = frame[receiver].vy_m_s;
                traces.vz_m_s[trace_index] = frame[receiver].vz_m_s;
            }
            if (energy_j != nullptr) {
                (*energy_j)[step] =
                    wave3d::elastic_energy(wavefield, coefficients).total_j;
            }
        }
    }

    expect(
        tracked_allocation_count.load(std::memory_order_relaxed) == 0,
        "CPU elastic time loop performed a dynamic allocation");
    expect(
        frame.data() == frame_data && frame.capacity() == frame_capacity &&
            traces.vx_m_s.data() == vx_data &&
            traces.vy_m_s.data() == vy_data &&
            traces.vz_m_s.data() == vz_data &&
            traces.vx_m_s.capacity() == vx_capacity &&
            traces.vy_m_s.capacity() == vy_capacity &&
            traces.vz_m_s.capacity() == vz_capacity,
        "CPU elastic time loop changed a preallocated buffer");
}

[[nodiscard]] double relative_l2_error(
    const std::vector<float>& first,
    std::size_t first_receiver,
    const std::vector<float>& second,
    std::size_t second_receiver,
    std::size_t sample_count,
    double second_scale) {
    double difference = 0.0;
    double reference = 0.0;
    for (std::size_t sample = 0; sample < sample_count; ++sample) {
        const double first_value = static_cast<double>(
            first[first_receiver * sample_count + sample]);
        const double second_value = second_scale * static_cast<double>(
            second[second_receiver * sample_count + sample]);
        const double residual = first_value - second_value;
        difference += residual * residual;
        reference += first_value * first_value;
    }
    expect(reference > 0.0, "relative L2 reference trace must be non-zero");
    return std::sqrt(difference / reference);
}

[[nodiscard]] double trace_energy(
    const std::vector<float>& trace,
    std::size_t receiver,
    std::size_t sample_count) {
    double result = 0.0;
    for (std::size_t sample = 0; sample < sample_count; ++sample) {
        const double value = static_cast<double>(
            trace[receiver * sample_count + sample]);
        result += value * value;
    }
    return result;
}

[[nodiscard]] double ricker_time_derivative(
    double sample_time_s,
    double feature_time_s) {
    constexpr double pi = 3.141592653589793238462643383279502884;
    const double a =
        pi * dominant_frequency_hz * (sample_time_s - feature_time_s);
    return 2.0 * pi * dominant_frequency_hz * a *
           (2.0 * a * a - 3.0) * std::exp(-a * a);
}

struct MatchedArrival {
    double feature_time_s{0.0};
    double absolute_correlation{0.0};
};

[[nodiscard]] double absolute_matched_correlation(
    const std::vector<float>& trace,
    std::size_t receiver,
    std::size_t sample_count,
    double candidate_time_s) {
    double dot = 0.0;
    double trace_norm = 0.0;
    double template_norm = 0.0;
    for (std::size_t sample = 0; sample < sample_count; ++sample) {
        const double value = static_cast<double>(
            trace[receiver * sample_count + sample]);
        const double sample_time_s =
            static_cast<double>(sample + 1) * dt_s;
        const double reference =
            ricker_time_derivative(sample_time_s, candidate_time_s);
        dot += value * reference;
        trace_norm += value * value;
        template_norm += reference * reference;
    }
    if (!(trace_norm > 0.0) || !(template_norm > 0.0)) {
        return 0.0;
    }
    return std::abs(dot) / std::sqrt(trace_norm * template_norm);
}

[[nodiscard]] MatchedArrival matched_arrival(
    const std::vector<float>& trace,
    std::size_t receiver,
    std::size_t sample_count,
    double theoretical_time_s) {
    const double half_search_width_s = 1.0 / dominant_frequency_hz;
    const double search_begin_s = theoretical_time_s - half_search_width_s;
    const double search_end_s = theoretical_time_s + half_search_width_s;
    MatchedArrival best{};

    for (std::size_t candidate_sample = 0;
         candidate_sample < sample_count;
         ++candidate_sample) {
        const double candidate_time_s =
            static_cast<double>(candidate_sample + 1) * dt_s;
        if (candidate_time_s < search_begin_s ||
            candidate_time_s > search_end_s) {
            continue;
        }

        const double correlation = absolute_matched_correlation(
            trace,
            receiver,
            sample_count,
            candidate_time_s);
        if (correlation > best.absolute_correlation) {
            best = {candidate_time_s, correlation};
        }
    }

    double left = std::max(search_begin_s, best.feature_time_s - dt_s);
    double right = std::min(search_end_s, best.feature_time_s + dt_s);
    constexpr double inverse_golden_ratio =
        0.618033988749894848204586834365638118;
    double first = right - inverse_golden_ratio * (right - left);
    double second = left + inverse_golden_ratio * (right - left);
    double first_value = absolute_matched_correlation(
        trace, receiver, sample_count, first);
    double second_value = absolute_matched_correlation(
        trace, receiver, sample_count, second);
    for (std::size_t iteration = 0; iteration < 32; ++iteration) {
        if (first_value < second_value) {
            left = first;
            first = second;
            first_value = second_value;
            second = left + inverse_golden_ratio * (right - left);
            second_value = absolute_matched_correlation(
                trace, receiver, sample_count, second);
        } else {
            right = second;
            second = first;
            second_value = first_value;
            first = right - inverse_golden_ratio * (right - left);
            first_value = absolute_matched_correlation(
                trace, receiver, sample_count, first);
        }
    }
    best.feature_time_s = 0.5 * (left + right);
    best.absolute_correlation = absolute_matched_correlation(
        trace,
        receiver,
        sample_count,
        best.feature_time_s);
    return best;
}

[[nodiscard]] double combined_phase_velocity_ratio(double speed_m_s) {
    constexpr double pi = 3.141592653589793238462643383279502884;
    const double points_per_wavelength =
        speed_m_s / (dominant_frequency_hz * 10.0);
    const double spatial =
        wave3d::staggered_spatial_phase_velocity_ratio(points_per_wavelength);
    const double argument = pi * dominant_frequency_hz * dt_s * spatial;
    return std::asin(argument) / (pi * dominant_frequency_hz * dt_s);
}

[[nodiscard]] double arrival_tolerance_s(double speed_m_s) {
    const double ratio = combined_phase_velocity_ratio(speed_m_s);
    const double travel_time_s = receiver_radius_m / speed_m_s;
    return 2.0 * dt_s + travel_time_s * std::abs(1.0 / ratio - 1.0);
}

void test_numerical_and_window_preconditions() {
    const auto grid = validation_grid();
    const double cfl_limit_s =
        wave3d::elastic_cfl_dt_limit_s(grid, vp_m_s, 0.9);
    expect(dt_s < cfl_limit_s, "physical test time step must pass CFL");
    expect(
        static_cast<double>(vs_m_s) /
                (dominant_frequency_hz * static_cast<double>(grid.dx_m)) >=
            wave3d::minimum_design_points_per_wavelength,
        "physical test must pass the shear-wave design band");
    expect(
        1.0 / (dominant_frequency_hz * dt_s) >=
            wave3d::minimum_design_time_samples_per_period,
        "physical test must pass temporal sampling");
    expect(
        arrival_tolerance_s(vp_m_s) <= 0.00102,
        "predeclared P-arrival tolerance ceiling changed");
    expect(
        arrival_tolerance_s(vs_m_s) <= 0.00103,
        "predeclared S-arrival tolerance ceiling changed");
    expect(
        static_cast<double>(arrival_sample_count) * dt_s <
            310.0 / static_cast<double>(vp_m_s),
        "arrival run must end before the earliest theoretical reflection");
    expect(
        static_cast<double>(causal_sample_count) * dt_s <
            200.0 / static_cast<double>(vp_m_s),
        "energy run must end before fastest theoretical boundary contact");
}

void test_explosion_arrival_symmetry_and_leakage(
    const wave3d::ElasticCoefficients& coefficients,
    const wave3d::PreparedReceiverSet& receivers) {
    const auto source = wave3d::prepare_moment_tensor_source_stencils(
        coefficients.grid,
        make_source(
            coefficients.grid,
            wave3d::isotropic_explosion(scalar_moment_nm),
            delayed_peak_s));
    wave3d::ElasticWavefield wavefield(coefficients.grid);
    TraceBuffer traces(receivers.receivers.size(), arrival_sample_count);
    run_steps(wavefield, coefficients, source, receivers, traces);

    const double theoretical_p_time_s =
        delayed_peak_s + receiver_radius_m / static_cast<double>(vp_m_s);
    const auto arrival = matched_arrival(
        traces.vx_m_s,
        PositiveX,
        traces.sample_count,
        theoretical_p_time_s);
    expect(
        arrival.absolute_correlation >= 0.5,
        "P arrival must correlate with the analytical Ricker derivative");
    expect(
        std::abs(arrival.feature_time_s - theoretical_p_time_s) <=
            arrival_tolerance_s(vp_m_s),
        "P matched-filter feature missed its theoretical arrival tolerance: " +
            std::to_string(arrival.feature_time_s) + " versus " +
            std::to_string(theoretical_p_time_s));

    constexpr double symmetry_tolerance = 2.0e-5;
    const double x_antisymmetry = relative_l2_error(
        traces.vx_m_s,
        PositiveX,
        traces.vx_m_s,
        NegativeX,
        traces.sample_count,
        -1.0);
    const double y_antisymmetry = relative_l2_error(
        traces.vy_m_s,
        PositiveY,
        traces.vy_m_s,
        NegativeY,
        traces.sample_count,
        -1.0);
    const double z_antisymmetry = relative_l2_error(
        traces.vz_m_s,
        PositiveZ,
        traces.vz_m_s,
        NegativeZ,
        traces.sample_count,
        -1.0);
    const double xy_symmetry = relative_l2_error(
        traces.vx_m_s,
        PositiveX,
        traces.vy_m_s,
        PositiveY,
        traces.sample_count,
        1.0);
    const double xz_symmetry = relative_l2_error(
        traces.vx_m_s,
        PositiveX,
        traces.vz_m_s,
        PositiveZ,
        traces.sample_count,
        1.0);
    expect(
        x_antisymmetry <= symmetry_tolerance,
        "opposite x explosion traces violated radial antisymmetry");
    expect(
        y_antisymmetry <= symmetry_tolerance,
        "opposite y explosion traces violated radial antisymmetry");
    expect(
        z_antisymmetry <= symmetry_tolerance,
        "opposite z explosion traces violated radial antisymmetry");
    expect(
        xy_symmetry <= symmetry_tolerance,
        "positive x and y explosion radial traces violated cubic symmetry");
    expect(
        xz_symmetry <= symmetry_tolerance,
        "positive x and z explosion radial traces violated cubic symmetry");

    const double radial_energy =
        trace_energy(traces.vx_m_s, PositiveX, traces.sample_count) +
        trace_energy(traces.vx_m_s, NegativeX, traces.sample_count) +
        trace_energy(traces.vy_m_s, PositiveY, traces.sample_count) +
        trace_energy(traces.vy_m_s, NegativeY, traces.sample_count) +
        trace_energy(traces.vz_m_s, PositiveZ, traces.sample_count) +
        trace_energy(traces.vz_m_s, NegativeZ, traces.sample_count);
    double transverse_energy = 0.0;
    for (const auto receiver : {PositiveX, NegativeX}) {
        transverse_energy +=
            trace_energy(traces.vy_m_s, receiver, traces.sample_count) +
            trace_energy(traces.vz_m_s, receiver, traces.sample_count);
    }
    for (const auto receiver : {PositiveY, NegativeY}) {
        transverse_energy +=
            trace_energy(traces.vx_m_s, receiver, traces.sample_count) +
            trace_energy(traces.vz_m_s, receiver, traces.sample_count);
    }
    for (const auto receiver : {PositiveZ, NegativeZ}) {
        transverse_energy +=
            trace_energy(traces.vx_m_s, receiver, traces.sample_count) +
            trace_energy(traces.vy_m_s, receiver, traces.sample_count);
    }
    expect(radial_energy > 0.0, "explosion radial trace energy must be positive");
    expect(
        transverse_energy / radial_energy <= 1.0e-8,
        "isotropic explosion transverse leakage exceeded its fixed limit");
    std::cout << std::setprecision(12)
              << "P feature: expected=" << theoretical_p_time_s
              << " observed=" << arrival.feature_time_s
              << " tolerance=" << arrival_tolerance_s(vp_m_s)
              << " correlation=" << arrival.absolute_correlation
              << " max_symmetry_error="
              << std::max(
                     {x_antisymmetry,
                      y_antisymmetry,
                      z_antisymmetry,
                      xy_symmetry,
                      xz_symmetry})
              << " transverse_ratio=" << transverse_energy / radial_energy
              << '\n';
}

void test_s_arrival_and_symmetry(
    const wave3d::ElasticCoefficients& coefficients,
    const wave3d::PreparedReceiverSet& receivers) {
    const wave3d::SymmetricMomentTensor mxy{
        0.0, 0.0, 0.0, scalar_moment_nm, 0.0, 0.0};
    const auto source = wave3d::prepare_moment_tensor_source_stencils(
        coefficients.grid,
        make_source(coefficients.grid, mxy, delayed_peak_s));
    wave3d::ElasticWavefield wavefield(coefficients.grid);
    TraceBuffer traces(receivers.receivers.size(), arrival_sample_count);
    run_steps(wavefield, coefficients, source, receivers, traces);

    const double theoretical_s_time_s =
        delayed_peak_s + receiver_radius_m / static_cast<double>(vs_m_s);
    const auto arrival = matched_arrival(
        traces.vy_m_s,
        PositiveX,
        traces.sample_count,
        theoretical_s_time_s);
    expect(
        arrival.absolute_correlation >= 0.5,
        "S arrival must correlate with the analytical Ricker derivative");
    expect(
        std::abs(arrival.feature_time_s - theoretical_s_time_s) <=
            arrival_tolerance_s(vs_m_s),
        "S matched-filter feature missed its theoretical arrival tolerance: " +
            std::to_string(arrival.feature_time_s) + " versus " +
            std::to_string(theoretical_s_time_s) + ", correlation=" +
            std::to_string(arrival.absolute_correlation));
    const double antisymmetry = relative_l2_error(
        traces.vy_m_s,
        PositiveX,
        traces.vy_m_s,
        NegativeX,
        traces.sample_count,
        -1.0);
    expect(
        antisymmetry <= 2.0e-5,
        "opposite x Mxy shear traces violated antisymmetry");
    std::cout << std::setprecision(12)
              << "S feature: expected=" << theoretical_s_time_s
              << " observed=" << arrival.feature_time_s
              << " tolerance=" << arrival_tolerance_s(vs_m_s)
              << " correlation=" << arrival.absolute_correlation
              << " antisymmetry_error=" << antisymmetry << '\n';
}

[[nodiscard]] float first_significant_sample(
    const std::vector<float>& trace,
    std::size_t receiver,
    std::size_t sample_count) {
    double maximum = 0.0;
    for (std::size_t sample = 0; sample < sample_count; ++sample) {
        maximum = std::max(
            maximum,
            std::abs(static_cast<double>(
                trace[receiver * sample_count + sample])));
    }
    expect(maximum > 0.0, "polarity trace must be non-zero");
    const double threshold = 0.01 * maximum;
    for (std::size_t sample = 0; sample < sample_count; ++sample) {
        const float value = trace[receiver * sample_count + sample];
        if (std::abs(static_cast<double>(value)) >= threshold) {
            return value;
        }
    }
    throw std::runtime_error("polarity trace has no significant sample");
}

void test_causal_polarity_and_energy(
    const wave3d::ElasticCoefficients& coefficients,
    const wave3d::PreparedReceiverSet& receivers) {
    const auto source = wave3d::prepare_moment_tensor_source_stencils(
        coefficients.grid,
        make_source(
            coefficients.grid,
            wave3d::isotropic_explosion(scalar_moment_nm),
            0.0));
    wave3d::ElasticWavefield wavefield(coefficients.grid);
    TraceBuffer traces(receivers.receivers.size(), causal_sample_count);
    std::vector<double> energy_j(causal_sample_count);
    run_steps(wavefield, coefficients, source, receivers, traces, &energy_j);

    const float positive_first = first_significant_sample(
        traces.vx_m_s,
        PositiveX,
        traces.sample_count);
    const float negative_first = first_significant_sample(
        traces.vx_m_s,
        NegativeX,
        traces.sample_count);
    expect(
        positive_first > 0.0F,
        "positive x first significant explosion motion must be outward");
    expect(
        negative_first < 0.0F,
        "negative x first significant explosion motion must be outward");

    const std::size_t energy_begin =
        static_cast<std::size_t>(std::ceil(0.038 / dt_s)) - 1;
    const std::size_t energy_end =
        static_cast<std::size_t>(std::floor(0.0615 / dt_s));
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = 0.0;
    for (std::size_t sample = energy_begin; sample < energy_end; ++sample) {
        expect(
            std::isfinite(energy_j[sample]) && energy_j[sample] > 0.0,
            "post-source elastic energy must be finite and positive");
        minimum = std::min(minimum, energy_j[sample]);
        maximum = std::max(maximum, energy_j[sample]);
    }
    expect(
        maximum / minimum <= 1.05,
        "post-source pre-boundary energy exceeded its fixed 5% envelope");
    std::cout << std::setprecision(12)
              << "causal polarity: +x=" << positive_first
              << " -x=" << negative_first
              << " post_source_energy_ratio=" << maximum / minimum << '\n';
}

void expect_equal_wavefields(
    const wave3d::ElasticWavefield& first,
    const wave3d::ElasticWavefield& second) {
    expect(first.vx_m_s == second.vx_m_s, "deterministic vx mismatch");
    expect(first.vy_m_s == second.vy_m_s, "deterministic vy mismatch");
    expect(first.vz_m_s == second.vz_m_s, "deterministic vz mismatch");
    expect(first.sxx_pa == second.sxx_pa, "deterministic sxx mismatch");
    expect(first.syy_pa == second.syy_pa, "deterministic syy mismatch");
    expect(first.szz_pa == second.szz_pa, "deterministic szz mismatch");
    expect(first.sxy_pa == second.sxy_pa, "deterministic sxy mismatch");
    expect(first.sxz_pa == second.sxz_pa, "deterministic sxz mismatch");
    expect(first.syz_pa == second.syz_pa, "deterministic syz mismatch");
}

void test_determinism_and_step_validation() {
    const wave3d::Grid3D grid{
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
    const auto model = wave3d::make_homogeneous_model(
        grid,
        {vp_m_s, vs_m_s, density_kg_m3});
    const auto coefficients = wave3d::prepare_elastic_coefficients(model);
    const auto raw_source = wave3d::prepare_moment_tensor_source(
        grid,
        {70.0, 70.0, 70.0},
        0.0,
        wave3d::isotropic_explosion(scalar_moment_nm),
        {dominant_frequency_hz, 0.0, 1.0});
    const auto source =
        wave3d::prepare_moment_tensor_source_stencils(grid, raw_source);
    const auto receiver_set = wave3d::prepare_receiver_set(
        grid,
        std::vector<wave3d::PhysicalPoint3D>{{80.0, 70.0, 70.0}});
    const auto receivers =
        wave3d::prepare_receiver_stencils(grid, receiver_set);

    wave3d::ElasticWavefield first(grid);
    wave3d::ElasticWavefield second(grid);
    TraceBuffer first_trace(1, 6);
    TraceBuffer second_trace(1, 6);
    run_steps(first, coefficients, source, receivers, first_trace);
    run_steps(second, coefficients, source, receivers, second_trace);
    expect_equal_wavefields(first, second);
    expect(
        first_trace.vx_m_s == second_trace.vx_m_s &&
            first_trace.vy_m_s == second_trace.vy_m_s &&
            first_trace.vz_m_s == second_trace.vz_m_s,
        "repeated CPU receiver traces must be bitwise deterministic");

    wave3d::ElasticWavefield untouched(grid);
    std::vector<wave3d::ReceiverVelocitySample> wrong_frame;
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::cpu_advance_elastic_interior_step(
                untouched,
                coefficients,
                source,
                receivers,
                0,
                dt_s,
                wrong_frame);
        },
        "CPU step must reject an incorrectly sized receiver frame");
    expect(
        std::all_of(
            untouched.sxx_pa.begin(),
            untouched.sxx_pa.end(),
            [](float value) { return value == 0.0F; }),
        "CPU step input rejection must precede wavefield mutation");
}

} // namespace

int main() {
    try {
        test_numerical_and_window_preconditions();
        const auto grid = validation_grid();
        const auto model = wave3d::make_homogeneous_model(
            grid,
            {vp_m_s, vs_m_s, density_kg_m3});
        const auto coefficients = wave3d::prepare_elastic_coefficients(model);
        const auto receiver_set =
            wave3d::prepare_receiver_set(grid, axis_receivers());
        const auto receivers =
            wave3d::prepare_receiver_stencils(grid, receiver_set);

        test_explosion_arrival_symmetry_and_leakage(coefficients, receivers);
        test_s_arrival_and_symmetry(coefficients, receivers);
        test_causal_polarity_and_energy(coefficients, receivers);
        test_determinism_and_step_validation();
    } catch (const std::exception& error) {
        std::cerr << "CPU elastic physics test failure: " << error.what()
                  << '\n';
        return 1;
    }

    std::cout << "CPU elastic physics tests passed\n";
    return 0;
}
