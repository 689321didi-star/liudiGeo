#include "wave3d/io/hdf5.hpp"
#include "wave3d/io/segy.hpp"
#include "wave3d/io/yaml_config.hpp"
#include "wave3d/model/physical_model.hpp"
#include "wave3d/task/cuda_forward_run.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("wave3d_forward_run_" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] wave3d::Grid3D grid() {
    return {
        9, 9, 9,
        10.0F, 10.0F, 10.0F,
        6,
        {6, 6}, {6, 6}, {0, 6}};
}

[[nodiscard]] wave3d::PhysicalModel model() {
    return wave3d::make_horizontal_layered_model(
        grid(),
        {
            {0.0, {3000.0F, 1800.0F, 2300.0F}},
            {50.0, {3300.0F, 1900.0F, 2500.0F}},
        });
}

[[nodiscard]] wave3d::io::ForwardRunConfiguration configuration() {
    wave3d::SimulationConfig simulation{};
    simulation.grid = grid();
    simulation.time = {0.0005, 0.02};
    simulation.material = {
        3000.0F, 3300.0F, 1800.0F, 1900.0F, 2300.0F, 2500.0F};
    simulation.top_boundary = wave3d::TopBoundary::FreeSurface;
    simulation.numerics = {0.85, 25.0};
    const auto source = wave3d::prepare_moment_tensor_source(
        simulation.grid,
        {40.0, 40.0, 30.0},
        0.0,
        wave3d::isotropic_explosion(1.0e12),
        {25.0, 0.004, 1.0});
    return {
        simulation,
        source,
        {{40.0, 40.0, 0.0}, {50.0, 40.0, 0.0}, {40.0, 50.0, 0.0}},
        "model.h5",
        "result"};
}

[[nodiscard]] std::vector<unsigned char> read_bytes(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::int16_t get_i16(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) {
    const auto bits = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1]));
    return static_cast<std::int16_t>(bits);
}

void test_hdf5_to_single_segy_pipeline() {
    TemporaryDirectory temporary;
    const auto model_path = temporary.path() / "model.h5";
    const auto configuration_path = temporary.path() / "run.yaml";
    wave3d::io::write_hdf5_model(model_path.string(), model());
    wave3d::io::write_resolved_yaml(
        configuration_path.string(), configuration());

    const auto report = wave3d::task::run_cuda_forward_from_yaml(
        configuration_path.string());
    const auto expected_output = temporary.path() / "result" / "record.sgy";
    expect(
        report.output_segy_path == expected_output.string() &&
            std::filesystem::exists(expected_output),
        "production pipeline did not create result/record.sgy");
    expect(
        report.physical_cell_count == 729 &&
            report.receiver_count == 3 && report.sample_count == 40,
        "production report has incorrect model/trace shape");
    expect(
        report.planned_required_bytes <= report.planned_budget_bytes,
        "production run exceeded the accepted memory budget");

    std::size_t output_entries = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(expected_output.parent_path())) {
        static_cast<void>(entry);
        ++output_entries;
    }
    expect(output_entries == 1, "production output directory has extra files");
    expect(
        !std::filesystem::exists(
            expected_output.parent_path() / "record_vx.sgy") &&
            !std::filesystem::exists(expected_output.string() + ".json"),
        "production pipeline created legacy SEG-Y outputs");

    const auto samples = wave3d::io::read_ieee_segy_samples(
        expected_output.string(), 9, 40);
    expect(
        std::any_of(samples.begin(), samples.end(), [](float value) {
            return value != 0.0F;
        }),
        "production SEG-Y record contains no propagated signal");

    const auto bytes = read_bytes(expected_output);
    const std::size_t trace_bytes = 240 + 40 * sizeof(float);
    const std::array<std::int16_t, 3> expected_codes{{14, 13, 12}};
    for (std::size_t trace = 0; trace < 9; ++trace) {
        expect(
            get_i16(bytes, 3600 + trace * trace_bytes + 28) ==
                expected_codes[trace % 3],
            "production SEG-Y component sequence is incorrect");
    }
}

void test_model_metadata_mismatches_fail_before_output() {
    TemporaryDirectory temporary;
    wave3d::io::write_hdf5_model(
        (temporary.path() / "model.h5").string(), model());

    auto extrema_mismatch = configuration();
    extrema_mismatch.simulation.material.max_vp_m_s = 3299.0F;
    extrema_mismatch.output_directory = "bad_extrema";
    const auto extrema_path = temporary.path() / "bad_extrema.yaml";
    wave3d::io::write_resolved_yaml(
        extrema_path.string(), extrema_mismatch);
    bool threw = false;
    try {
        static_cast<void>(wave3d::task::run_cuda_forward_from_yaml(
            extrema_path.string()));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "HDF5/YAML extrema mismatch must fail explicitly");
    expect(
        !std::filesystem::exists(
            temporary.path() / "bad_extrema" / "record.sgy"),
        "extrema mismatch left a SEG-Y output");

    auto grid_mismatch = configuration();
    grid_mismatch.simulation.grid.nx = 10;
    grid_mismatch.source = wave3d::prepare_moment_tensor_source(
        grid_mismatch.simulation.grid,
        grid_mismatch.source.physical_location,
        grid_mismatch.source.origin_time_s,
        grid_mismatch.source.moment,
        grid_mismatch.source.wavelet);
    grid_mismatch.output_directory = "bad_grid";
    const auto grid_path = temporary.path() / "bad_grid.yaml";
    wave3d::io::write_resolved_yaml(grid_path.string(), grid_mismatch);
    threw = false;
    try {
        static_cast<void>(wave3d::task::run_cuda_forward_from_yaml(
            grid_path.string()));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "HDF5/YAML grid mismatch must fail explicitly");
    expect(
        !std::filesystem::exists(
            temporary.path() / "bad_grid" / "record.sgy"),
        "grid mismatch left a SEG-Y output");
}

} // namespace

int main() {
    test_hdf5_to_single_segy_pipeline();
    test_model_metadata_mismatches_fail_before_output();
    if (failures != 0) {
        std::cerr << failures << " CUDA forward-run test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D HDF5-to-SEG-Y CUDA pipeline tests passed\n";
    return 0;
}
