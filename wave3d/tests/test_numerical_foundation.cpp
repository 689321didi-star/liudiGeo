#include "wave3d/numerics/elastic_validation.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool nearly_equal(double actual, double expected, double tolerance) {
    return std::abs(actual - expected) <= tolerance;
}

bool contains_error(
    const std::vector<std::string>& errors,
    const std::string& fragment) {
    for (const auto& error : errors) {
        if (error.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

wave3d::SimulationConfig sample_config() {
    wave3d::SimulationConfig config{};
    config.grid = {
        200, 200, 200,
        10.0F, 10.0F, 10.0F,
        6,
        {20, 20}, {20, 20}, {0, 20}};
    config.time = {0.0005, 2.0};
    config.material = {
        4000.0F, 4000.0F, 2300.0F, 2300.0F, 2500.0F, 2500.0F};
    config.numerics = {0.9, 45.0};
    return config;
}

wave3d::SimulationConfig threshold_config() {
    auto config = sample_config();
    config.grid.dx_m = 10.0F;
    config.grid.dy_m = 10.0F;
    config.grid.dz_m = 10.0F;
    config.material = {
        3000.0F, 3000.0F, 2000.0F, 2000.0F, 2200.0F, 2200.0F};
    config.numerics.design_frequency_hz = 40.0;
    config.time.dt_s = 1.0 / (40.0 * 20.0);
    return config;
}

void test_exact_coefficients_and_moments() {
    expect(wave3d::staggered_fd_radius == 6, "operator radius must be six");
    expect(
        wave3d::staggered_fd_spatial_order == 12,
        "operator spatial order must be twelve");

    constexpr std::array<std::int64_t, 6> numerators{{
        160083, -12705, 22869, -5445, 847, -63}};
    constexpr std::array<std::int64_t, 6> denominators{{
        131072, 131072, 1310720, 1835008, 2359296, 2883584}};
    for (std::size_t index = 0; index < numerators.size(); ++index) {
        const auto ratio = wave3d::staggered_first_derivative_exact[index];
        expect(
            ratio.numerator == numerators[index] &&
                ratio.denominator == denominators[index],
            "FD coefficient must retain its accepted exact fraction");
        expect(
            wave3d::staggered_first_derivative_coefficients[index] ==
                ratio.value(),
            "FD double must be generated from its exact fraction");
    }

    for (int power = 1; power <= 11; power += 2) {
        long double moment = 0.0L;
        for (std::size_t index = 0;
             index < wave3d::staggered_fd_radius;
             ++index) {
            const auto ratio =
                wave3d::staggered_first_derivative_exact[index];
            const auto coefficient =
                static_cast<long double>(ratio.numerator) /
                static_cast<long double>(ratio.denominator);
            const auto offset = static_cast<long double>(index) + 0.5L;
            moment += 2.0L * coefficient * std::pow(offset, power);
        }
        const auto expected = power == 1 ? 1.0L : 0.0L;
        expect(
            std::abs(moment - expected) < 1.0e-11L,
            "FD coefficients must satisfy all moments through power eleven");
    }

    long double power_thirteen_moment = 0.0L;
    for (std::size_t index = 0;
         index < wave3d::staggered_fd_radius;
         ++index) {
        const auto ratio = wave3d::staggered_first_derivative_exact[index];
        const auto coefficient = static_cast<long double>(ratio.numerator) /
                                 static_cast<long double>(ratio.denominator);
        const auto offset = static_cast<long double>(index) + 0.5L;
        power_thirteen_moment +=
            2.0L * coefficient * std::pow(offset, 13);
    }
    constexpr long double factorial_thirteen = 6227020800.0L;
    constexpr long double expected_error =
        -231.0L / 54525952.0L;
    expect(
        std::abs(
            power_thirteen_moment / factorial_thirteen - expected_error) <
            1.0e-15L,
        "power-thirteen moment must match the accepted truncation error");

    expect(
        wave3d::staggered_symbol_max_exact.numerator == 1187803 &&
            wave3d::staggered_symbol_max_exact.denominator == 887040,
        "spectral maximum must retain its accepted exact fraction");
    expect(
        nearly_equal(
            wave3d::staggered_symbol(std::acos(-1.0)),
            wave3d::staggered_symbol_max,
            2.0e-15),
        "Nyquist symbol must equal the analytical spectral maximum");
}

void test_dispersion_reference_values() {
    expect(
        nearly_equal(
            wave3d::staggered_spatial_phase_velocity_ratio(5.0),
            0.9999616092655956,
            2.0e-15),
        "five-PPW phase ratio must match the independent reference");
    expect(
        nearly_equal(
            wave3d::staggered_spatial_group_velocity_ratio(5.0),
            0.9995421118172195,
            2.0e-15),
        "five-PPW group ratio must match the independent reference");
}

void test_sample_report_and_cfl_threshold() {
    auto config = sample_config();
    expect(
        wave3d::validate_staggered_elastic(config).empty(),
        "sample configuration must pass accepted numerical gates");
    const auto report = wave3d::elastic_numerical_report(config);
    expect(
        nearly_equal(report.cfl_dt_limit_s, 0.0009701093205349901, 2.0e-18),
        "sample CFL limit must match the accepted derivation");
    expect(
        nearly_equal(
            report.shear_points_per_wavelength[0],
            2300.0 / 450.0,
            1.0e-14),
        "sample report must expose shear points per wavelength");
    expect(
        nearly_equal(report.time_samples_per_period, 1.0 / 0.0225, 1.0e-13),
        "sample report must expose time samples per design period");

    const auto limit = report.cfl_dt_limit_s;
    config.time.dt_s = limit;
    expect(
        !contains_error(
            wave3d::validate_staggered_elastic(config), "CFL limit"),
        "the exact CFL threshold must pass");
    config.time.dt_s = std::nextafter(limit, std::numeric_limits<double>::infinity());
    expect(
        contains_error(
            wave3d::validate_staggered_elastic(config), "CFL limit"),
        "a time step above the CFL threshold must fail");
}

void test_design_band_thresholds() {
    auto config = threshold_config();
    expect(
        wave3d::validate_staggered_elastic(config).empty(),
        "exact spatial and temporal design thresholds must pass");

    config.grid.dx_m = std::nextafter(
        10.0F, std::numeric_limits<float>::infinity());
    expect(
        contains_error(
            wave3d::validate_staggered_elastic(config), "along x"),
        "fewer than five spatial points per wavelength must fail");

    config = threshold_config();
    config.time.dt_s = std::nextafter(
        config.time.dt_s, std::numeric_limits<double>::infinity());
    expect(
        contains_error(
            wave3d::validate_staggered_elastic(config), "twenty time samples"),
        "fewer than twenty time samples per period must fail");

    config = threshold_config();
    config.numerics.design_frequency_hz = 0.0;
    expect(
        contains_error(wave3d::validate_staggered_elastic(config), "frequency"),
        "a missing design frequency must fail explicitly");

    config = threshold_config();
    config.numerics.cfl_safety_factor = 1.0;
    expect(
        contains_error(
            wave3d::validate_staggered_elastic(config), "safety factor"),
        "a CFL safety factor outside the open unit interval must fail");
}

void test_resolved_metadata() {
    const auto metadata =
        wave3d::resolved_elastic_numerical_metadata(sample_config());
    expect(
        metadata.find("radius=6") != std::string::npos &&
            metadata.find("spatial_order=12") != std::string::npos &&
            metadata.find("design_frequency_hz=45") != std::string::npos &&
            metadata.find("shear_ppw_xyz=") != std::string::npos,
        "resolved metadata must preserve the operator and design margins");
}

} // namespace

int main() {
    test_exact_coefficients_and_moments();
    test_dispersion_reference_values();
    test_sample_report_and_cfl_threshold();
    test_design_band_thresholds();
    test_resolved_metadata();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Wave3D numerical-foundation tests passed\n";
    return 0;
}
