#include "wave3d/core/simulation_config.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

wave3d::SimulationConfig valid_config() {
    wave3d::SimulationConfig config{};
    config.grid = {
        200, 200, 200,
        10.0F, 10.0F, 10.0F,
        6,
        {20, 20}, {20, 20}, {0, 20}};
    config.time = {0.0005F, 2.0F};
    config.material = {
        4000.0F, 4000.0F, 2300.0F, 2300.0F, 2500.0F, 2500.0F};
    return config;
}

void test_dimensions_and_indexing() {
    const auto config = valid_config();
    const auto& grid = config.grid;
    expect(grid.physical_cell_count() == 8'000'000, "200 cubed cell count");
    expect(grid.allocated_nx() == 252, "allocated x dimension");
    expect(grid.allocated_ny() == 252, "allocated y dimension");
    expect(grid.allocated_nz() == 232, "free-surface allocated z dimension");
    expect(grid.physical_origin_x() == 26, "physical x origin");
    expect(grid.physical_origin_z() == 6, "physical z origin below halo");
    expect(grid.linear_index(1, 0, 0) == 1, "x must be contiguous");
    expect(grid.linear_index(0, 1, 0) == 252, "y stride");
    expect(grid.linear_index(0, 0, 1) == 252 * 252, "z stride");
}

void test_invalid_index() {
    const auto grid = valid_config().grid;
    bool threw = false;
    try {
        static_cast<void>(grid.linear_index(grid.allocated_nx(), 0, 0));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "out-of-range index must throw");
}

void test_validation() {
    auto config = valid_config();
    expect(wave3d::validate(config).empty(), "reference configuration must validate");

    config.time.dt_s = 0.01F;
    expect(!wave3d::validate(config).empty(), "unstable time step must fail validation");

    config = valid_config();
    config.grid.dx_m = std::numeric_limits<float>::infinity();
    expect(!wave3d::validate(config).empty(), "infinite spacing must fail validation");

    config = valid_config();
    config.time.total_time_s = std::numeric_limits<float>::infinity();
    expect(!wave3d::validate(config).empty(), "infinite time must fail validation");

    config = valid_config();
    config.material.max_density_kg_m3 =
        std::numeric_limits<float>::quiet_NaN();
    expect(!wave3d::validate(config).empty(), "NaN density must fail validation");

    config = valid_config();
    config.material.min_vs_m_s = 0.0F;
    expect(!wave3d::validate(config).empty(), "zero Vs is outside solid scope");

    config = valid_config();
    config.material.min_vp_m_s = 2000.0F;
    config.material.max_vs_m_s = 1800.0F;
    expect(
        !wave3d::validate(config).empty(),
        "material extrema must guarantee positive bulk modulus");

    config = valid_config();
    config.grid.z_boundary.lower_absorbing = 20;
    expect(
        !wave3d::validate(config).empty(),
        "free surface with top absorption must fail validation");

    config = valid_config();
    config.top_boundary = wave3d::TopBoundary::Absorbing;
    expect(
        !wave3d::validate(config).empty(),
        "absorbing top without a layer must fail validation");
}

void test_time_step_count() {
    expect(
        wave3d::TimeConfig{0.3F, 1.0F}.step_count() == 4,
        "partial final time step must be counted");

    bool threw = false;
    try {
        static_cast<void>(wave3d::TimeConfig{
            std::numeric_limits<float>::min(),
            std::numeric_limits<float>::max()}.step_count());
    } catch (const std::overflow_error&) {
        threw = true;
    }
    expect(threw, "time-step count overflow must throw");
}

void test_overflow_detection() {
    auto grid = valid_config().grid;
    grid.nx = std::numeric_limits<std::size_t>::max();
    bool threw = false;
    try {
        static_cast<void>(grid.allocated_cell_count());
    } catch (const std::overflow_error&) {
        threw = true;
    }
    expect(threw, "grid allocation overflow must throw");

    grid = valid_config().grid;
    grid.nx = std::numeric_limits<std::size_t>::max() / 2;
    grid.ny = 3;
    threw = false;
    try {
        static_cast<void>(grid.linear_index(0, 0, 0));
    } catch (const std::overflow_error&) {
        threw = true;
    }
    expect(threw, "linear indexing on an overflowing grid must throw");

    grid = valid_config().grid;
    grid.halo = std::numeric_limits<std::size_t>::max();
    grid.x_boundary.lower_absorbing = 1;
    threw = false;
    try {
        static_cast<void>(grid.physical_origin_x());
    } catch (const std::overflow_error&) {
        threw = true;
    }
    expect(threw, "physical-origin overflow must throw");
}

} // namespace

int main() {
    test_dimensions_and_indexing();
    test_invalid_index();
    test_validation();
    test_time_step_count();
    test_overflow_detection();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Wave3D core tests passed\n";
    return 0;
}
