#include "wave3d/io/hdf5.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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

[[nodiscard]] std::filesystem::path temporary_path(const char* suffix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("wave3d_hdf5_" + std::to_string(stamp) + suffix);
}

[[nodiscard]] wave3d::Grid3D grid() {
    return {
        4, 3, 5,
        10.0F, 12.5F, 15.0F,
        6,
        {2, 3}, {4, 2}, {0, 5}};
}

void test_model_round_trip() {
    const auto expected = wave3d::make_horizontal_layered_model(
        grid(),
        {{0.0, {3000.0F, 1700.0F, 2300.0F}},
         {30.0, {3600.0F, 2100.0F, 2550.0F}}});
    const auto path = temporary_path("_model.h5");
    wave3d::io::write_hdf5_model(path.string(), expected);
    const auto actual = wave3d::io::read_hdf5_model(path.string());
    std::filesystem::remove(path);
    expect(
        wave3d::same_grid_geometry(actual.grid, expected.grid),
        "HDF5 model grid/axes changed");
    expect(
        actual.vp_m_s == expected.vp_m_s &&
            actual.vs_m_s == expected.vs_m_s &&
            actual.density_kg_m3 == expected.density_kg_m3,
        "HDF5 model values changed");
}

[[nodiscard]] wave3d::io::ThreeComponentTraces traces() {
    const auto source = wave3d::prepare_moment_tensor_source(
        grid(),
        {15.0, 12.5, 30.0},
        0.002,
        {1.0e12, -2.0e12, 3.0e12, 4.0e11, -5.0e11, 6.0e11},
        {25.0, 0.04, 1.5});
    wave3d::io::ThreeComponentTraces result{
        3,
        5,
        0.00050000000000000012,
        {{1.25, 2.5, 0.0}, {11.5, 7.75, 15.0}, {25.25, 20.5, 30.0}},
        source,
        std::vector<float>(15),
        std::vector<float>(15),
        std::vector<float>(15)};
    for (std::size_t index = 0; index < 15; ++index) {
        result.vx_m_s[index] = static_cast<float>(index) + 0.25F;
        result.vy_m_s[index] = -static_cast<float>(index) - 0.5F;
        result.vz_m_s[index] = 2.0F * static_cast<float>(index) + 0.75F;
    }
    return result;
}

void test_trace_round_trip() {
    const auto expected = traces();
    const auto path = temporary_path("_traces.h5");
    wave3d::io::write_hdf5_traces(path.string(), expected);
    const auto actual = wave3d::io::read_hdf5_traces(path.string());
    std::filesystem::remove(path);
    expect(
        actual.receiver_count == expected.receiver_count &&
            actual.sample_count == expected.sample_count &&
            actual.dt_s == expected.dt_s,
        "HDF5 trace shape/exact dt changed");
    expect(
        actual.vx_m_s == expected.vx_m_s &&
            actual.vy_m_s == expected.vy_m_s &&
            actual.vz_m_s == expected.vz_m_s,
        "HDF5 trace values/components changed");
    expect(
        actual.receiver_coordinates_m[1].x_m ==
                expected.receiver_coordinates_m[1].x_m &&
            actual.receiver_coordinates_m[2].z_m ==
                expected.receiver_coordinates_m[2].z_m,
        "HDF5 receiver coordinates/order changed");
    expect(
        actual.source.moment.m_xz_nm == expected.source.moment.m_xz_nm &&
            actual.source.storage_location.y == expected.source.storage_location.y &&
            actual.source.wavelet.peak_rate_s_inv ==
                expected.source.wavelet.peak_rate_s_inv,
        "HDF5 source metadata changed");
}

void test_sparse_snapshot_round_trip() {
    const wave3d::io::SparseVelocitySnapshot expected{
        grid(),
        17,
        0.008500000000000001,
        {{6, 6, 6}, {8, 9, 10}, {10, 11, 12}},
        {1.25F, 2.5F, 3.75F},
        {-1.0F, -2.0F, -3.0F},
        {0.125F, 0.25F, 0.5F}};
    const auto path = temporary_path("_snapshot.h5");
    wave3d::io::write_hdf5_sparse_snapshot(path.string(), expected);
    const auto actual = wave3d::io::read_hdf5_sparse_snapshot(path.string());
    std::filesystem::remove(path);
    expect(
        wave3d::same_grid_geometry(actual.grid, expected.grid) &&
            actual.step_index == expected.step_index &&
            actual.time_s == expected.time_s,
        "HDF5 sparse snapshot metadata changed");
    expect(
        actual.storage_indices[1].x == expected.storage_indices[1].x &&
            actual.storage_indices[2].z == expected.storage_indices[2].z &&
            actual.vx_m_s == expected.vx_m_s &&
            actual.vy_m_s == expected.vy_m_s &&
            actual.vz_m_s == expected.vz_m_s,
        "HDF5 sparse snapshot indices/values changed");
}

void test_truncated_rejection() {
    const auto path = temporary_path("_bad.h5");
    {
        std::ofstream output(path, std::ios::binary);
        output << "not hdf5";
    }
    bool threw = false;
    try {
        static_cast<void>(wave3d::io::read_hdf5_model(path.string()));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    std::filesystem::remove(path);
    expect(threw, "malformed HDF5 must fail explicitly");
}

} // namespace

int main() {
    test_model_round_trip();
    test_trace_round_trip();
    test_sparse_snapshot_round_trip();
    test_truncated_rejection();
    if (failures != 0) {
        std::cerr << failures << " HDF5 I/O test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D HDF5 I/O tests passed\n";
    return 0;
}
