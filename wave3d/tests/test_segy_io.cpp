#include "wave3d/io/segy.hpp"

#include <chrono>
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

[[nodiscard]] std::filesystem::path temporary_prefix() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("wave3d_segy_" + std::to_string(stamp));
}

[[nodiscard]] wave3d::io::ThreeComponentTraces traces() {
    wave3d::MomentTensorSource source{};
    source.physical_location = {10.25, 20.5, 30.75};
    source.storage_location = {7.025, 8.05, 9.075};
    source.origin_time_s = 0.003;
    source.moment = {1.0e12, -2.0e12, 3.0e12, 4.0e11, -5.0e11, 6.0e11};
    source.wavelet = {30.0, 0.04, 1.25};
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
        result.vx_m_s[index] = static_cast<float>(index) + 0.125F;
        result.vy_m_s[index] = -static_cast<float>(index) - 0.25F;
        result.vz_m_s[index] = 2.0F * static_cast<float>(index) + 0.5F;
    }
    return result;
}

[[nodiscard]] std::string read_text(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

void test_component_round_trip_and_sidecars() {
    const auto expected = traces();
    const auto prefix = temporary_prefix();
    const auto paths =
        wave3d::io::write_segy_components(prefix.string(), expected);
    const std::vector<float>* components[] = {
        &expected.vx_m_s, &expected.vy_m_s, &expected.vz_m_s};
    const char* names[] = {"VX", "VY", "VZ"};
    const auto expected_bytes =
        3600 + expected.receiver_count * (240 + expected.sample_count * 4);
    for (std::size_t component = 0; component < paths.size(); ++component) {
        expect(
            std::filesystem::file_size(paths[component]) == expected_bytes,
            "SEG-Y file size/header layout changed");
        const auto values = wave3d::io::read_ieee_segy_samples(
            paths[component], expected.receiver_count, expected.sample_count);
        expect(values == *components[component], "SEG-Y IEEE samples changed");
        const auto sidecar = read_text(paths[component] + ".json");
        expect(
            sidecar.find(std::string("\"component\": \"") + names[component]) !=
                    std::string::npos &&
                sidecar.find("\"dt_s_exact\": 0.00050000000000000012") !=
                    std::string::npos &&
                sidecar.find("\"normalization\": \"none\"") !=
                    std::string::npos &&
                sidecar.find("Mxx,Myy,Mzz,Mxy,Mxz,Myz") != std::string::npos,
            "SEG-Y sidecar lost component/exact metadata");
    }
    const auto volume = wave3d::io::read_ieee_segy_volume(
        paths[0], 3, 1, expected.sample_count);
    expect(volume == expected.vx_m_s, "SEG-Y volume reader changed trace order");

    std::filesystem::resize_file(
        paths[0], std::filesystem::file_size(paths[0]) - 1);
    bool threw = false;
    try {
        static_cast<void>(wave3d::io::read_ieee_segy_samples(
            paths[0], expected.receiver_count, expected.sample_count));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "truncated SEG-Y must fail explicitly");
    for (const auto& path : paths) {
        std::filesystem::remove(path);
        std::filesystem::remove(path + ".json");
    }
}

} // namespace

int main() {
    test_component_round_trip_and_sidecars();
    if (failures != 0) {
        std::cerr << failures << " SEG-Y I/O test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D SEG-Y I/O tests passed\n";
    return 0;
}
