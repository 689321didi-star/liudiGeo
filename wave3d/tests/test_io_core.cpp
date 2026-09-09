#include "wave3d/io/csv_receivers.hpp"
#include "wave3d/io/data.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

[[nodiscard]] std::filesystem::path temporary_path(const char* suffix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("wave3d_io_" + std::to_string(stamp) + suffix);
}

void test_csv_round_trip() {
    const std::vector<wave3d::PhysicalPoint3D> expected{
        {1.25, 2.5, 0.0},
        {10.125000000000002, 20.75, 3.125},
        {99.5, 88.25, 77.125}};
    const auto path = temporary_path(".csv");
    wave3d::io::write_receiver_csv(path.string(), expected);
    const auto actual = wave3d::io::read_receiver_csv(path.string());
    std::filesystem::remove(path);
    expect(actual.size() == expected.size(), "CSV receiver count changed");
    for (std::size_t receiver = 0; receiver < expected.size(); ++receiver) {
        expect(
            actual[receiver].x_m == expected[receiver].x_m &&
                actual[receiver].y_m == expected[receiver].y_m &&
                actual[receiver].z_m == expected[receiver].z_m,
            "CSV coordinate/order changed");
    }

    const auto invalid_path = temporary_path("_invalid.csv");
    {
        std::ofstream output(invalid_path);
        output << "x,y,z\n1,2,3\n";
    }
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                wave3d::io::read_receiver_csv(invalid_path.string()));
        },
        "CSV with wrong header must fail");
    std::filesystem::remove(invalid_path);
}

void test_data_validation() {
    wave3d::io::ThreeComponentTraces traces{};
    traces.receiver_count = 1;
    traces.sample_count = 2;
    traces.dt_s = 0.0005;
    traces.receiver_coordinates_m = {{1.0, 2.0, 3.0}};
    traces.source.moment = {1.0, 1.0, 1.0, 0.0, 0.0, 0.0};
    traces.source.wavelet = {30.0, 0.04, 1.0};
    traces.vx_m_s = {1.0F, 2.0F};
    traces.vy_m_s = {3.0F, 4.0F};
    traces.vz_m_s = {5.0F, 6.0F};
    wave3d::io::require_valid_traces(traces);
    traces.vz_m_s[1] = std::numeric_limits<float>::infinity();
    expect_throws<std::invalid_argument>(
        [&] { wave3d::io::require_valid_traces(traces); },
        "non-finite trace sample must fail");
    traces.vz_m_s[1] = 6.0F;
    traces.source.origin_time_s =
        std::numeric_limits<double>::quiet_NaN();
    expect_throws<std::invalid_argument>(
        [&] { wave3d::io::require_valid_traces(traces); },
        "non-finite source metadata must fail");
}

} // namespace

int main() {
    test_csv_round_trip();
    test_data_validation();
    if (failures != 0) {
        std::cerr << failures << " core I/O test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D core I/O tests passed\n";
    return 0;
}
