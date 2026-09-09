#include "wave3d/checkpoint/checkpoint_store.hpp"
#include "wave3d/model/physical_model.hpp"
#include "wave3d/propagation/forward_propagator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

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
    return {9, 9, 9, 10.0F, 10.0F, 10.0F, 6, {}, {}, {}};
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
        wave3d::prepare_receiver_set(grid(), {{30.0, 40.0, 40.0}}));
    return {
        wave3d::prepare_elastic_coefficients(
            wave3d::make_homogeneous_model(
                grid(), {3200.0F, 2200.0F, 2500.0F})),
        source,
        receivers,
        4,
        0.0005};
}

struct Snapshot {
    wave3d::checkpoint::CheckpointMetadata metadata;
    std::array<std::vector<float>, 9> fields;
};

[[nodiscard]] std::array<wave3d::ReadOnlyFloatView, 9> fields(
    const wave3d::ElasticWavefieldConstView& view) {
    return {{
        view.vx_m_s, view.vy_m_s, view.vz_m_s,
        view.sxx_pa, view.syy_pa, view.szz_pa,
        view.sxy_pa, view.sxz_pa, view.syz_pa}};
}

[[nodiscard]] std::array<std::vector<float>*, 9> fields(
    wave3d::ElasticWavefield& wavefield) {
    return {{
        &wavefield.vx_m_s, &wavefield.vy_m_s, &wavefield.vz_m_s,
        &wavefield.sxx_pa, &wavefield.syy_pa, &wavefield.szz_pa,
        &wavefield.sxy_pa, &wavefield.sxz_pa, &wavefield.syz_pa}};
}

class MockCheckpointStore final
    : public wave3d::checkpoint::ICheckpointStore {
public:
    void save(
        const wave3d::checkpoint::CheckpointMetadata& metadata,
        const wave3d::ElasticWavefieldConstView& wavefield) override {
        if (!wave3d::same_grid_geometry(metadata.grid, wavefield.grid) ||
            contains(metadata.time_level.completed_steps)) {
            throw std::invalid_argument(
                "mock checkpoint metadata is mismatched or duplicate");
        }
        Snapshot snapshot{};
        snapshot.metadata = metadata;
        const auto source_fields = fields(wavefield);
        for (std::size_t field = 0; field < source_fields.size(); ++field) {
            snapshot.fields[field].assign(
                source_fields[field].begin(), source_fields[field].end());
        }
        snapshots.push_back(std::move(snapshot));
    }

    [[nodiscard]] bool contains(
        std::size_t completed_steps) const noexcept override {
        return find(completed_steps) != snapshots.end();
    }

    [[nodiscard]] wave3d::checkpoint::CheckpointMetadata metadata(
        std::size_t completed_steps) const override {
        const auto found = find(completed_steps);
        if (found == snapshots.end()) {
            throw std::out_of_range("mock checkpoint is absent");
        }
        return found->metadata;
    }

    void restore(
        std::size_t completed_steps,
        wave3d::ElasticWavefield& destination) const override {
        const auto found = find(completed_steps);
        if (found == snapshots.end()) {
            throw std::out_of_range("mock checkpoint is absent");
        }
        wave3d::require_valid_elastic_wavefield_layout(destination);
        if (!wave3d::same_grid_geometry(found->metadata.grid, destination.grid)) {
            throw std::invalid_argument(
                "mock checkpoint destination grid differs");
        }
        const auto destination_fields = fields(destination);
        for (std::size_t field = 0; field < destination_fields.size(); ++field) {
            std::copy(
                found->fields[field].begin(),
                found->fields[field].end(),
                destination_fields[field]->begin());
        }
    }

    std::vector<Snapshot> snapshots;

private:
    [[nodiscard]] std::vector<Snapshot>::const_iterator find(
        std::size_t completed_steps) const noexcept {
        return std::find_if(
            snapshots.begin(), snapshots.end(),
            [completed_steps](const Snapshot& snapshot) {
                return snapshot.metadata.time_level.completed_steps ==
                       completed_steps;
            });
    }
};

void expect_same_store(
    const MockCheckpointStore& first,
    const MockCheckpointStore& second) {
    expect(first.snapshots.size() == second.snapshots.size(),
           "checkpoint count is not deterministic");
    for (std::size_t snapshot = 0;
         snapshot < first.snapshots.size();
         ++snapshot) {
        expect(
            wave3d::checkpoint::same_checkpoint_metadata(
                first.snapshots[snapshot].metadata,
                second.snapshots[snapshot].metadata) &&
                first.snapshots[snapshot].fields ==
                    second.snapshots[snapshot].fields,
            "checkpoint metadata/state is not bitwise deterministic");
    }
}

[[nodiscard]] MockCheckpointStore run_with_checkpoints() {
    MockCheckpointStore store;
    wave3d::checkpoint::CheckpointObserver observer(store, 2);
    auto propagator = wave3d::make_forward_propagator(
        wave3d::ForwardPropagatorBackend::CpuReferenceInterior,
        inputs());
    propagator->attach_observer(observer);
    propagator->run_to_completion();
    return store;
}

void test_checkpoint_observer_and_restore() {
    auto first = run_with_checkpoints();
    const auto second = run_with_checkpoints();
    expect(first.contains(2) && first.contains(4) && !first.contains(1),
           "checkpoint interval changed");
    expect_same_store(first, second);
    const auto metadata = first.metadata(2);
    expect(
        metadata.time_level.step_index_n == 1 &&
            metadata.time_level.completed_steps == 2 &&
            metadata.time_level.velocity_time_s == 0.001 &&
            metadata.time_level.stress_time_s == 0.00075,
        "checkpoint time levels changed");

    wave3d::ElasticWavefield restored(grid());
    first.restore(4, restored);
    const auto restored_fields = fields(wave3d::elastic_wavefield_view(restored));
    for (std::size_t field = 0; field < restored_fields.size(); ++field) {
        expect(
            std::equal(
                restored_fields[field].begin(),
                restored_fields[field].end(),
                first.snapshots[1].fields[field].begin()),
            "checkpoint restore changed a wavefield value");
    }
    expect_throws<std::out_of_range>(
        [&] { first.restore(3, restored); },
        "missing checkpoint restore must fail");
    wave3d::ElasticWavefield wrong_grid(
        {8, 9, 9, 10.0F, 10.0F, 10.0F, 6, {}, {}, {}});
    expect_throws<std::invalid_argument>(
        [&] { first.restore(4, wrong_grid); },
        "checkpoint restore to another grid must fail");
    expect_throws<std::invalid_argument>(
        [&] { wave3d::checkpoint::CheckpointObserver invalid(first, 0); },
        "zero checkpoint interval must fail");
}

} // namespace

int main() {
    try {
        test_checkpoint_observer_and_restore();
    } catch (const std::exception& error) {
        std::cerr << "checkpoint interface test failure: " << error.what()
                  << '\n';
        return 1;
    }
    std::cout << "Wave3D checkpoint interface tests passed\n";
    return 0;
}
