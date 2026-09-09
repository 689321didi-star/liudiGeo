#include "wave3d/io/receiver_data.hpp"
#include "wave3d/model/physical_model.hpp"
#include "wave3d/propagation/forward_propagator.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

std::atomic<bool> track_allocations{false};
std::atomic<std::size_t> tracked_allocation_count{0};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void release_tracked_memory(void* memory) noexcept {
    std::free(memory);
}

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
    release_tracked_memory(memory);
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

constexpr std::size_t sample_count = 4;
constexpr double dt_s = 0.0005;

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

[[nodiscard]] wave3d::Grid3D grid() {
    return {
        9, 9, 9,
        10.0F, 10.0F, 10.0F,
        6,
        {}, {}, {}};
}

[[nodiscard]] wave3d::CpuReferencePropagatorInputs inputs() {
    const auto source = wave3d::prepare_moment_tensor_source_stencils(
        grid(),
        wave3d::prepare_moment_tensor_source(
            grid(),
            {40.0, 40.0, 40.0},
            0.0,
            wave3d::isotropic_explosion(1.0e12),
            {30.0, 0.04, 1.0}));
    const auto receivers = wave3d::prepare_receiver_stencils(
        grid(),
        wave3d::prepare_receiver_set(
            grid(),
            {{30.0, 40.0, 40.0}, {45.0, 40.0, 40.0}}));
    return {
        wave3d::prepare_elastic_coefficients(
            wave3d::make_homogeneous_model(
                grid(), {3200.0F, 2200.0F, 2500.0F})),
        source,
        receivers,
        sample_count,
        dt_s};
}

struct DirectResult {
    wave3d::ElasticWavefield wavefield;
    wave3d::io::ThreeComponentTraces traces;

    explicit DirectResult(const wave3d::Grid3D& source_grid)
        : wavefield(source_grid) {}
};

[[nodiscard]] DirectResult direct_result() {
    auto problem = inputs();
    DirectResult result(problem.coefficients.grid);
    result.traces.receiver_count = problem.receivers.receivers.size();
    result.traces.sample_count = sample_count;
    result.traces.dt_s = dt_s;
    result.traces.source = problem.source.source;
    for (const auto& receiver : problem.receivers.receivers) {
        result.traces.receiver_coordinates_m.push_back(
            receiver.receiver.physical_location);
    }
    const auto values = result.traces.receiver_count * sample_count;
    result.traces.vx_m_s.resize(values);
    result.traces.vy_m_s.resize(values);
    result.traces.vz_m_s.resize(values);
    std::vector<wave3d::ReceiverVelocitySample> frame(
        result.traces.receiver_count);
    for (std::size_t step = 0; step < sample_count; ++step) {
        wave3d::cpu_advance_elastic_interior_step(
            result.wavefield,
            problem.coefficients,
            problem.source,
            problem.receivers,
            step,
            dt_s,
            frame);
        for (std::size_t receiver = 0;
             receiver < result.traces.receiver_count;
             ++receiver) {
            const auto index = receiver * sample_count + step;
            result.traces.vx_m_s[index] = frame[receiver].vx_m_s;
            result.traces.vy_m_s[index] = frame[receiver].vy_m_s;
            result.traces.vz_m_s[index] = frame[receiver].vz_m_s;
        }
    }
    return result;
}

class MetadataObserver final : public wave3d::IForwardObserver {
public:
    void after_step(
        const wave3d::ForwardStepMetadata& metadata,
        const wave3d::ElasticWavefieldConstView& wavefield) override {
        if (count >= records.size()) {
            throw std::runtime_error("observer received too many steps");
        }
        records[count] = metadata;
        observed_cells[count] = wavefield.cell_count();
        ++count;
    }

    std::array<wave3d::ForwardStepMetadata, sample_count> records{};
    std::array<std::size_t, sample_count> observed_cells{};
    std::size_t count{0};
};

void expect_equal_fields(
    const wave3d::ElasticWavefieldConstView& actual,
    const wave3d::ElasticWavefieldConstView& expected) {
    const std::array<wave3d::ReadOnlyFloatView, 9> actual_fields{{
        actual.vx_m_s, actual.vy_m_s, actual.vz_m_s,
        actual.sxx_pa, actual.syy_pa, actual.szz_pa,
        actual.sxy_pa, actual.sxz_pa, actual.syz_pa}};
    const std::array<wave3d::ReadOnlyFloatView, 9> expected_fields{{
        expected.vx_m_s, expected.vy_m_s, expected.vz_m_s,
        expected.sxx_pa, expected.syy_pa, expected.szz_pa,
        expected.sxy_pa, expected.sxz_pa, expected.syz_pa}};
    for (std::size_t field = 0; field < actual_fields.size(); ++field) {
        expect(
            std::equal(
                actual_fields[field].begin(),
                actual_fields[field].end(),
                expected_fields[field].begin()),
            "factory wavefield differs from direct CPU stepping");
    }
}

void test_view_factory_observer_and_reader() {
    static_assert(std::is_same_v<
        decltype(std::declval<const wave3d::ReadOnlyFloatView&>().data()),
        const float*>);
    static_assert(std::is_same_v<
        decltype(std::declval<const wave3d::ReadOnlyFloatView&>()[0]),
        const float&>);

    const auto expected = direct_result();
    auto propagator = wave3d::make_forward_propagator(
        wave3d::ForwardPropagatorBackend::CpuReferenceInterior,
        inputs());
    MetadataObserver observer;
    propagator->attach_observer(observer);
    expect_throws<std::invalid_argument>(
        [&] { propagator->attach_observer(observer); },
        "duplicate observer must fail");

    tracked_allocation_count.store(0, std::memory_order_relaxed);
    track_allocations.store(true, std::memory_order_relaxed);
    try {
        propagator->run_to_completion();
    } catch (...) {
        track_allocations.store(false, std::memory_order_relaxed);
        throw;
    }
    track_allocations.store(false, std::memory_order_relaxed);
    expect(
        tracked_allocation_count.load(std::memory_order_relaxed) == 0,
        "factory propagation step allocated dynamically");

    expect(propagator->completed_steps() == sample_count, "factory stopped early");
    expect(observer.count == sample_count, "observer missed a completed step");
    for (std::size_t step = 0; step < sample_count; ++step) {
        expect(
            observer.records[step].step_index_n == step &&
                observer.records[step].completed_steps == step + 1 &&
                observer.records[step].velocity_time_s ==
                    static_cast<double>(step + 1) * dt_s &&
                observer.records[step].stress_time_s ==
                    (static_cast<double>(step) + 0.5) * dt_s &&
                observer.observed_cells[step] ==
                    propagator->grid().allocated_cell_count(),
            "observer time level/grid metadata changed");
    }
    expect_equal_fields(
        propagator->wavefield(),
        wave3d::elastic_wavefield_view(expected.wavefield));
    const auto& traces = propagator->receiver_traces();
    expect(
        traces.vx_m_s == expected.traces.vx_m_s &&
            traces.vy_m_s == expected.traces.vy_m_s &&
            traces.vz_m_s == expected.traces.vz_m_s,
        "factory receiver-major traces differ from direct CPU stepping");

    const wave3d::io::ThreeComponentReceiverDataReader reader(traces);
    expect(
        reader.receiver_count() == 2 && reader.sample_count() == sample_count &&
            reader.dt_s() == dt_s &&
            reader.receiver_coordinate_m(1).x_m == 45.0 &&
            reader.source().moment.m_xx_nm == 1.0e12,
        "receiver data reader metadata changed");
    expect(
        reader.sample(wave3d::io::VelocityComponent::X, 1, 3) ==
                traces.vx_m_s[1 * sample_count + 3] &&
            reader.sample(wave3d::io::VelocityComponent::Y, 0, 2) ==
                traces.vy_m_s[2] &&
            reader.sample(wave3d::io::VelocityComponent::Z, 0, 1) ==
                traces.vz_m_s[1],
        "receiver data reader changed component/index order");
    expect_throws<std::out_of_range>(
        [&] {
            static_cast<void>(reader.sample(
                wave3d::io::VelocityComponent::X, 2, 0));
        },
        "receiver reader must reject invalid receiver");
    expect_throws<std::out_of_range>(
        [&] { static_cast<void>(propagator->wavefield().vx_m_s[999999]); },
        "read-only wavefield view must reject invalid index");
    expect_throws<std::out_of_range>(
        [&] { propagator->advance_one(); },
        "completed propagator must reject another step");
    expect_throws<std::logic_error>(
        [&] {
            MetadataObserver late;
            propagator->attach_observer(late);
        },
        "observer attachment after start must fail");
}

} // namespace

int main() {
    try {
        test_view_factory_observer_and_reader();
    } catch (const std::exception& error) {
        std::cerr << "forward interface test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Wave3D forward interface tests passed\n";
    return 0;
}
