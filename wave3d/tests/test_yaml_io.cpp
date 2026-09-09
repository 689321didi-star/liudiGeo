#include "wave3d/io/yaml_config.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::filesystem::path temporary_path(const char* suffix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("wave3d_yaml_" + std::to_string(stamp) + suffix);
}

[[nodiscard]] wave3d::io::ForwardRunConfiguration configuration() {
    wave3d::SimulationConfig simulation{};
    simulation.grid = {
        11, 9, 7,
        10.0F, 12.5F, 15.0F,
        6,
        {4, 5}, {3, 6}, {0, 7}};
    simulation.time = {0.0005, 0.125};
    simulation.material = {
        3200.0F, 3200.0F, 1800.0F, 1800.0F, 2400.0F, 2400.0F};
    simulation.top_boundary = wave3d::TopBoundary::FreeSurface;
    simulation.numerics = {0.85, 20.0};
    const auto source = wave3d::prepare_moment_tensor_source(
        simulation.grid,
        {35.0, 25.0, 30.0},
        0.003,
        {1.0e12, -2.0e12, 3.0e12, 4.0e11, -5.0e11, 6.0e11},
        {25.0, 0.04, 1.25});
    return {
        simulation,
        {3200.0F, 1800.0F, 2400.0F},
        source,
        {{1.25, 2.5, 0.0}, {75.5, 55.25, 15.0}},
        "models/layered.h5",
        "runs/example"};
}

void test_round_trip() {
    const auto expected = configuration();
    const auto path = temporary_path(".yaml");
    wave3d::io::write_resolved_yaml(path.string(), expected);
    const auto actual = wave3d::io::load_yaml_run_configuration(path.string());
    std::filesystem::remove(path);
    expect(
        wave3d::same_grid_geometry(
            actual.simulation.grid, expected.simulation.grid),
        "YAML grid changed");
    expect(
        actual.simulation.time.dt_s == expected.simulation.time.dt_s &&
            actual.simulation.time.total_time_s ==
                expected.simulation.time.total_time_s &&
            actual.simulation.numerics.design_frequency_hz ==
                expected.simulation.numerics.design_frequency_hz,
        "YAML time/numerical metadata changed");
    expect(
        actual.source.physical_location.x_m ==
                expected.source.physical_location.x_m &&
            actual.source.moment.m_yz_nm == expected.source.moment.m_yz_nm &&
            actual.source.wavelet.peak_rate_s_inv ==
                expected.source.wavelet.peak_rate_s_inv,
        "YAML source metadata changed");
    expect(
        actual.receiver_coordinates_m.size() == 2 &&
            actual.receiver_coordinates_m[1].y_m == 55.25,
        "YAML receiver order/coordinates changed");
    expect(
        actual.model_hdf5_path == "models/layered.h5" &&
            actual.output_directory == "runs/example",
        "YAML paths changed");
}

void test_malformed_rejection() {
    const auto path = temporary_path("_invalid.yaml");
    {
        std::ofstream output(path);
        output << "grid: [invalid\n";
    }
    bool threw = false;
    try {
        static_cast<void>(
            wave3d::io::load_yaml_run_configuration(path.string()));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    std::filesystem::remove(path);
    expect(threw, "malformed YAML must fail explicitly");
}

} // namespace

int main() {
    test_round_trip();
    test_malformed_rejection();
    if (failures != 0) {
        std::cerr << failures << " YAML I/O test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D YAML I/O tests passed\n";
    return 0;
}
