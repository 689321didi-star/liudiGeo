#include "wave3d/boundary/cpml.hpp"
#include "wave3d/model/physical_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

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

[[nodiscard]] wave3d::Grid3D test_grid() {
    return {
        5, 5, 5,
        10.0F, 10.0F, 10.0F,
        6,
        {4, 4}, {4, 4}, {4, 4}};
}

[[nodiscard]] wave3d::CpmlParameters parameters() {
    return {0.001, 3200.0, 30.0, 1.0e-3, 2.0, 1.0, {}};
}

void test_coefficients_and_state() {
    const auto grid = test_grid();
    const auto profile = wave3d::prepare_cpml_profile(grid, parameters());
    const auto origin = grid.physical_origin_x();
    const auto& x = profile.axes[0];
    expect(x.a_integer[origin] == 0.0F, "interior CPML a must be zero");
    expect(x.b_integer[origin] == 1.0F, "interior CPML b must be one");
    expect(
        x.inverse_kappa_integer[origin] == 1.0F,
        "interior inverse kappa must be one");

    const auto coordinate = origin - 1;
    const double r = 0.25;
    const double sigma_max =
        -3.0 * 3200.0 * std::log(1.0e-3) / (2.0 * 40.0);
    const double sigma = sigma_max * r * r;
    constexpr double pi = 3.141592653589793238462643383279502884;
    const double alpha = pi * 30.0 * (1.0 - r);
    const double expected_b = std::exp(-(sigma + alpha) * 0.001);
    const double expected_a = sigma * (expected_b - 1.0) / (sigma + alpha);
    expect(
        std::abs(x.b_integer[coordinate] - expected_b) < 2.0e-7,
        "integer CPML b formula changed");
    expect(
        std::abs(x.a_integer[coordinate] - expected_a) < 2.0e-7,
        "integer CPML a formula changed");
    expect(
        x.a_half[coordinate] != x.a_integer[coordinate],
        "integer and half-grid CPML profiles must be distinct");

    static_assert(
        wave3d::cpml_memory_field_count == 18,
        "elastic CPML requires 18 derivative memories");
    static_assert(
        !std::is_copy_constructible<wave3d::CpmlState>::value,
        "CPML state must be move-only");
    wave3d::CpmlState state(grid);
    expect(
        state.bytes() ==
            18 * grid.allocated_cell_count() * sizeof(float),
        "CPML state byte count changed");
    for (const auto& field : state.fields) {
        expect(
            std::all_of(field.begin(), field.end(), [](float value) {
                return value == 0.0F;
            }),
            "new CPML memory must be zero");
    }
    wave3d::CpmlState moved(std::move(state));
    expect(moved.fields[0].size() == grid.allocated_cell_count(),
           "CPML move must retain state");
}

void test_cpu_updates_populate_memory() {
    const auto grid = test_grid();
    const auto profile = wave3d::prepare_cpml_profile(grid, parameters());
    auto coefficients = wave3d::prepare_elastic_coefficients(
        wave3d::make_homogeneous_model(
            grid, {3200.0F, 2200.0F, 2500.0F}));
    wave3d::ElasticWavefield wavefield(grid);
    for (std::size_t index = 0; index < wavefield.cell_count(); ++index) {
        wavefield.vx_m_s[index] = static_cast<float>(
            1.0e-4 * std::sin(0.01 * static_cast<double>(index)));
        wavefield.vy_m_s[index] = static_cast<float>(
            1.0e-4 * std::cos(0.013 * static_cast<double>(index)));
        wavefield.vz_m_s[index] = static_cast<float>(
            1.0e-4 * std::sin(0.017 * static_cast<double>(index)));
    }
    wave3d::CpmlState state(grid);
    wave3d::cpu_update_elastic_stresses_cpml(
        wavefield, coefficients, profile, state);
    wave3d::cpu_update_elastic_velocities_cpml(
        wavefield, coefficients, profile, state);
    bool any_memory = false;
    for (const auto& field : state.fields) {
        any_memory = any_memory ||
            std::any_of(field.begin(), field.end(), [](float value) {
                return value != 0.0F;
            });
    }
    expect(any_memory, "CPML update must populate derivative memories");
    for (const auto* field : {
             &wavefield.vx_m_s, &wavefield.vy_m_s, &wavefield.vz_m_s,
             &wavefield.sxx_pa, &wavefield.syy_pa, &wavefield.szz_pa,
             &wavefield.sxy_pa, &wavefield.sxz_pa, &wavefield.syz_pa}) {
        expect(
            std::all_of(field->begin(), field->end(), [](float value) {
                return std::isfinite(value);
            }),
            "CPU CPML update must remain finite");
    }
}

void test_rejection() {
    const auto grid = test_grid();
    auto invalid = parameters();
    invalid.target_reflection = 1.0;
    expect_throws<std::invalid_argument>(
        [&] { static_cast<void>(wave3d::prepare_cpml_profile(grid, invalid)); },
        "invalid CPML reflection must fail");
    invalid = parameters();
    invalid.dt_s = std::numeric_limits<double>::quiet_NaN();
    expect_throws<std::invalid_argument>(
        [&] { static_cast<void>(wave3d::prepare_cpml_profile(grid, invalid)); },
        "non-finite CPML dt must fail");
    invalid = parameters();
    invalid.polynomial_power = 0.5;
    expect_throws<std::invalid_argument>(
        [&] { static_cast<void>(wave3d::prepare_cpml_profile(grid, invalid)); },
        "CPML power below one must fail");
    invalid = parameters();
    invalid.sides.z_min = true;
    auto no_top_width = grid;
    no_top_width.z_boundary.lower_absorbing = 0;
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                wave3d::prepare_cpml_profile(no_top_width, invalid));
        },
        "enabled CPML side without width must fail");
}

} // namespace

int main() {
    test_coefficients_and_state();
    test_cpu_updates_populate_memory();
    test_rejection();
    if (failures != 0) {
        std::cerr << failures << " CPML test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D CPML tests passed\n";
    return 0;
}
