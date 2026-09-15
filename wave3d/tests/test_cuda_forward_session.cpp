#include "wave3d/acquisition/moment_source_injector.hpp"
#include "wave3d/acquisition/receiver.hpp"
#include "wave3d/acquisition/receiver_sampler.hpp"
#include "wave3d/boundary/cpml.hpp"
#include "wave3d/boundary/free_surface.hpp"
#include "wave3d/cuda/cpml.hpp"
#include "wave3d/cuda/cuda_error.hpp"
#include "wave3d/cuda/elastic_propagator.hpp"
#include "wave3d/cuda/forward_session.hpp"
#include "wave3d/cuda/free_surface.hpp"
#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/model/physical_model.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

constexpr double dt_s = 0.0005;
constexpr std::size_t sample_count = 8;
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

[[nodiscard]] wave3d::Grid3D free_surface_grid() {
    return {
        9, 9, 9,
        10.0F, 10.0F, 10.0F,
        6,
        {6, 6}, {6, 6}, {0, 6}};
}

[[nodiscard]] wave3d::CpmlParameters cpml_parameters(bool top_enabled) {
    return {
        dt_s,
        3300.0,
        25.0,
        1.0e-3,
        2.0,
        1.0,
        {true, true, true, true, top_enabled, true}};
}

struct HostProblem {
    wave3d::Grid3D grid;
    wave3d::ElasticCoefficients coefficients;
    wave3d::PreparedMomentTensorSource source;
    wave3d::PreparedReceiverSet receivers;
    wave3d::CpmlProfile profile;
    wave3d::TractionFreeSurface surface;
};

[[nodiscard]] HostProblem make_problem() {
    const auto grid = free_surface_grid();
    return {
        grid,
        wave3d::prepare_elastic_coefficients(
            wave3d::make_horizontal_layered_model(
                grid,
                {
                    {0.0, {3000.0F, 1800.0F, 2300.0F}},
                    {50.0, {3300.0F, 1900.0F, 2500.0F}},
                })),
        wave3d::prepare_moment_tensor_source_stencils(
            grid,
            wave3d::prepare_moment_tensor_source(
                grid,
                {40.0, 40.0, 30.0},
                0.0,
                wave3d::isotropic_explosion(1.0e12),
                {25.0, 0.004, 1.0})),
        wave3d::prepare_receiver_stencils(
            grid,
            wave3d::prepare_receiver_set(
                grid,
                std::vector<wave3d::PhysicalPoint3D>{
                    {40.0, 40.0, 0.0},
                    {50.0, 40.0, 0.0},
                    {40.0, 50.0, 0.0}})),
        wave3d::prepare_cpml_profile(grid, cpml_parameters(false)),
        wave3d::prepare_traction_free_surface(grid)};
}

struct RunResult {
    wave3d::ElasticWavefield wavefield;
    std::vector<float> vx;
    std::vector<float> vy;
    std::vector<float> vz;

    RunResult(const wave3d::Grid3D& grid, std::size_t trace_values)
        : wavefield(grid),
          vx(trace_values),
          vy(trace_values),
          vz(trace_values) {}
};

[[nodiscard]] RunResult run_direct(const HostProblem& problem) {
    wave3d::cuda::DeviceElasticCoefficients coefficients(problem.coefficients);
    wave3d::cuda::DeviceElasticWavefield wavefield(problem.grid);
    wave3d::cuda::DeviceMomentTensorSource source(problem.source);
    wave3d::cuda::DeviceReceiverSet receivers(problem.receivers);
    wave3d::cuda::DeviceReceiverTraces traces(
        problem.receivers.receivers.size(), sample_count, dt_s);
    wave3d::cuda::DeviceCpmlProfile profile(problem.profile);
    wave3d::cuda::DeviceCpmlState state(problem.grid);
    wave3d::cuda::DeviceTractionFreeSurface surface(problem.surface);
    for (std::size_t step = 0; step < sample_count; ++step) {
        wave3d::cuda::advance_elastic_free_surface_step(
            wavefield,
            coefficients,
            source,
            receivers,
            traces,
            profile,
            state,
            surface,
            step);
    }
    wave3d::cuda::synchronize();

    RunResult result(
        problem.grid,
        problem.receivers.receivers.size() * sample_count);
    wavefield.download(result.wavefield);
    traces.download(result.vx, result.vy, result.vz);
    return result;
}

void expect_equal(
    const wave3d::ElasticWavefield& actual,
    const wave3d::ElasticWavefield& expected) {
    const std::array<const std::vector<float>*, 9> actual_fields{{
        &actual.vx_m_s,
        &actual.vy_m_s,
        &actual.vz_m_s,
        &actual.sxx_pa,
        &actual.syy_pa,
        &actual.szz_pa,
        &actual.sxy_pa,
        &actual.sxz_pa,
        &actual.syz_pa}};
    const std::array<const std::vector<float>*, 9> expected_fields{{
        &expected.vx_m_s,
        &expected.vy_m_s,
        &expected.vz_m_s,
        &expected.sxx_pa,
        &expected.syy_pa,
        &expected.szz_pa,
        &expected.sxy_pa,
        &expected.sxz_pa,
        &expected.syz_pa}};
    for (std::size_t component = 0; component < actual_fields.size();
         ++component) {
        expect(
            *actual_fields[component] == *expected_fields[component],
            "batched session wavefield differs from direct CUDA composition");
    }
}

void test_batched_session_matches_direct_composition() {
    const auto problem = make_problem();
    const auto reference = run_direct(problem);
    wave3d::cuda::CudaForwardSession session(
        problem.coefficients,
        problem.source,
        problem.receivers,
        problem.profile,
        problem.surface,
        sample_count);

    expect(session.total_steps() == sample_count, "session total is incorrect");
    expect(session.completed_steps() == 0, "new session already advanced");
    expect(
        session.remaining_steps() == sample_count,
        "initial remaining count is incorrect");
    expect(
        !session.finished() && !session.failed(),
        "new session state is invalid");
    expect(
        !session.last_completed_step().has_value(),
        "new session has step metadata");
    expect(session.receiver_count() == 3, "session receiver count is incorrect");
    expect(session.owned_device_bytes() > 0, "session reports no device ownership");

    const auto view = session.device_wavefield_view();
    wave3d::cuda::require_valid_device_elastic_wavefield_view(view);
    expect(
        wave3d::same_grid_geometry(view.grid, problem.grid) &&
            view.cell_count() == problem.grid.allocated_cell_count(),
        "session device view has incorrect geometry");

    std::vector<float> vx(3 * sample_count);
    std::vector<float> vy(vx.size());
    std::vector<float> vz(vx.size());
    expect_throws<std::logic_error>(
        [&] { session.download_receiver_traces(vx, vy, vz); },
        "session allowed trace download before completion");
    expect_throws<std::invalid_argument>(
        [&] { static_cast<void>(session.advance(0)); },
        "session accepted a zero-sized batch");

    auto metadata = session.advance(1);
    expect(
        metadata.step_index_n == 0 && metadata.completed_steps == 1 &&
            metadata.velocity_time_s == dt_s &&
            metadata.stress_time_s == 0.5 * dt_s,
        "first session batch metadata is incorrect");
    metadata = session.advance(2);
    expect(
        metadata.step_index_n == 2 && metadata.completed_steps == 3 &&
            metadata.velocity_time_s == 3.0 * dt_s &&
            metadata.stress_time_s == 2.5 * dt_s,
        "second session batch metadata is incorrect");
    metadata = session.advance(100);
    expect(
        metadata.step_index_n == sample_count - 1 &&
            metadata.completed_steps == sample_count && session.finished() &&
            session.remaining_steps() == 0,
        "final session batch did not clip at total steps");
    expect_throws<std::out_of_range>(
        [&] { static_cast<void>(session.advance(1)); },
        "session advanced after completion");

    RunResult actual(problem.grid, vx.size());
    session.download_wavefield(actual.wavefield);
    session.download_receiver_traces(actual.vx, actual.vy, actual.vz);
    expect_equal(actual.wavefield, reference.wavefield);
    expect(
        actual.vx == reference.vx,
        "session VX traces differ from direct CUDA");
    expect(
        actual.vy == reference.vy,
        "session VY traces differ from direct CUDA");
    expect(
        actual.vz == reference.vz,
        "session VZ traces differ from direct CUDA");
}

void test_invalid_session_compositions_fail() {
    const auto problem = make_problem();
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::cuda::CudaForwardSession invalid(
                problem.coefficients,
                problem.source,
                problem.receivers,
                problem.profile,
                std::nullopt,
                sample_count);
        },
        "session accepted a missing top boundary");
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::cuda::CudaForwardSession invalid(
                problem.coefficients,
                problem.source,
                problem.receivers,
                problem.profile,
                problem.surface,
                0);
        },
        "session accepted zero total steps");

    auto other_grid = problem.grid;
    ++other_grid.nx;
    const auto other_source = wave3d::prepare_moment_tensor_source_stencils(
        other_grid,
        wave3d::prepare_moment_tensor_source(
            other_grid,
            {40.0, 40.0, 30.0},
            0.0,
            wave3d::isotropic_explosion(1.0e12),
            {25.0, 0.004, 1.0}));
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::cuda::CudaForwardSession invalid(
                problem.coefficients,
                other_source,
                problem.receivers,
                problem.profile,
                problem.surface,
                sample_count);
        },
        "session accepted mismatched source geometry");
}

} // namespace

int main() {
    static_assert(
        !std::is_copy_constructible<wave3d::cuda::CudaForwardSession>::value,
        "CUDA forward session must not copy device ownership");
    static_assert(
        !std::is_move_constructible<wave3d::cuda::CudaForwardSession>::value,
        "CUDA forward session keeps a stable lifetime for exposed views");

    try {
        test_batched_session_matches_direct_composition();
        test_invalid_session_compositions_fail();
    } catch (const std::exception& error) {
        std::cerr << "CUDA forward session test failure: " << error.what()
                  << '\n';
        ++failures;
    }
    if (failures != 0) {
        std::cerr << failures << " CUDA forward session test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D CUDA forward session tests passed\n";
    return 0;
}
