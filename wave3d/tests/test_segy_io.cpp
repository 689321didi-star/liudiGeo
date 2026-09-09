#include "wave3d/io/segy.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

[[nodiscard]] std::vector<unsigned char> read_bytes(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::uint16_t get_u16(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1]));
}

[[nodiscard]] std::int16_t get_i16(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) {
    return static_cast<std::int16_t>(get_u16(bytes, offset));
}

[[nodiscard]] std::uint32_t get_u32(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

[[nodiscard]] std::int32_t get_i32(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) {
    return static_cast<std::int32_t>(get_u32(bytes, offset));
}

[[nodiscard]] float get_float32(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) {
    const auto bits = get_u32(bytes, offset);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

[[nodiscard]] std::vector<float> receiver_major_component_samples(
    const wave3d::io::ThreeComponentTraces& values) {
    std::vector<float> result;
    result.reserve(values.receiver_count * 3 * values.sample_count);
    const std::array<const std::vector<float>*, 3> components{{
        &values.vx_m_s, &values.vy_m_s, &values.vz_m_s}};
    for (std::size_t receiver = 0; receiver < values.receiver_count; ++receiver) {
        for (const auto* component : components) {
            const auto begin = component->begin() +
                               static_cast<std::ptrdiff_t>(
                                   receiver * values.sample_count);
            result.insert(
                result.end(),
                begin,
                begin + static_cast<std::ptrdiff_t>(values.sample_count));
        }
    }
    return result;
}

void test_single_file_standard_headers_and_samples() {
    const auto expected = traces();
    const auto prefix = temporary_prefix();
    const auto path = prefix.string() + ".sgy";
    wave3d::io::write_segy(path, expected);

    expect(std::filesystem::exists(path), "single SEG-Y output was not created");
    for (const char* suffix : {"_vx.sgy", "_vy.sgy", "_vz.sgy"}) {
        expect(
            !std::filesystem::exists(prefix.string() + suffix),
            "legacy per-component SEG-Y output was created");
    }
    expect(
        !std::filesystem::exists(path + ".json"),
        "legacy SEG-Y JSON sidecar was created");

    const auto bytes = read_bytes(path);
    const auto trace_bytes = std::size_t{240} + expected.sample_count * 4;
    const auto trace_count = expected.receiver_count * 3;
    expect(
        bytes.size() == 3600 + trace_count * trace_bytes,
        "single SEG-Y file size/header layout is incorrect");

    const std::string textual_header(bytes.begin(), bytes.begin() + 3200);
    expect(
        textual_header.find("SEG-Y REVISION 1, BIG-ENDIAN") != std::string::npos &&
            textual_header.find("TRACE ORDER: RECEIVER-MAJOR, THEN VX,VY,VZ") !=
                std::string::npos &&
            textual_header.find("VX = IN-LINE/EAST") != std::string::npos &&
            textual_header.find("VY = CROSS-LINE/NORTH") != std::string::npos &&
            textual_header.find("VZ = VERTICAL/DOWN") != std::string::npos &&
            textual_header.find("NORMALIZATION NONE") != std::string::npos &&
            textual_header.find("TIME AXIS IS (N+1)*DT") != std::string::npos &&
            textual_header.find("C40 END TEXTUAL HEADER") != std::string::npos,
        "SEG-Y textual header lost required semantics");

    const std::size_t binary = 3200;
    expect(get_i32(bytes, binary) == 1, "SEG-Y job identification is wrong");
    expect(get_i32(bytes, binary + 4) == 1, "SEG-Y line number is wrong");
    expect(get_i32(bytes, binary + 8) == 1, "SEG-Y reel number is wrong");
    expect(
        get_i16(bytes, binary + 12) == static_cast<std::int16_t>(trace_count),
        "SEG-Y common-source ensemble trace count is wrong");
    expect(
        get_u16(bytes, binary + 16) == 500 &&
            get_u16(bytes, binary + 18) == 500,
        "SEG-Y binary sample intervals are wrong");
    expect(
        get_u16(bytes, binary + 20) == expected.sample_count &&
            get_u16(bytes, binary + 22) == expected.sample_count,
        "SEG-Y binary sample counts are wrong");
    expect(get_u16(bytes, binary + 24) == 5, "SEG-Y format is not IEEE float32");
    expect(get_i16(bytes, binary + 28) == 5, "SEG-Y sorting is not common-source");
    expect(get_i16(bytes, binary + 54) == 1, "SEG-Y measurement system is not SI");
    expect(get_u16(bytes, binary + 300) == 0x0100, "SEG-Y revision is not 1.0");
    expect(get_i16(bytes, binary + 302) == 1, "SEG-Y traces are not fixed length");
    expect(
        get_i16(bytes, binary + 304) == 0,
        "SEG-Y unexpectedly declares extended textual headers");

    const std::array<std::int16_t, 3> component_codes{{14, 13, 12}};
    const std::array<const std::vector<float>*, 3> components{{
        &expected.vx_m_s, &expected.vy_m_s, &expected.vz_m_s}};
    for (std::size_t receiver = 0; receiver < expected.receiver_count; ++receiver) {
        for (std::size_t component = 0; component < components.size(); ++component) {
            const auto trace_index = receiver * components.size() + component;
            const auto header = 3600 + trace_index * trace_bytes;
            const auto sequence = static_cast<std::int32_t>(trace_index + 1);
            expect(
                get_i32(bytes, header) == sequence &&
                    get_i32(bytes, header + 4) == sequence &&
                    get_i32(bytes, header + 8) == 1 &&
                    get_i32(bytes, header + 12) == sequence &&
                    get_i32(bytes, header + 16) == 1 &&
                    get_i32(bytes, header + 20) == 1 &&
                    get_i32(bytes, header + 24) == sequence,
                "SEG-Y trace/field/ensemble sequence headers are wrong");
            expect(
                get_i16(bytes, header + 28) == component_codes[component],
                "SEG-Y trace component identification is wrong");
            expect(
                get_i32(bytes, header + 40) ==
                        -static_cast<std::int32_t>(
                            expected.receiver_coordinates_m[receiver].z_m * 1000.0) &&
                    get_i32(bytes, header + 44) == 0 &&
                    get_i32(bytes, header + 48) == 30750,
                "SEG-Y elevation/source-depth fields are wrong");
            expect(
                get_i16(bytes, header + 68) == -1000 &&
                    get_i16(bytes, header + 70) == -1000,
                "SEG-Y elevation/coordinate scalars are wrong");
            expect(
                get_i32(bytes, header + 72) == 10250 &&
                    get_i32(bytes, header + 76) == 20500 &&
                    get_i32(bytes, header + 80) ==
                        static_cast<std::int32_t>(
                            expected.receiver_coordinates_m[receiver].x_m * 1000.0) &&
                    get_i32(bytes, header + 84) ==
                        static_cast<std::int32_t>(
                            expected.receiver_coordinates_m[receiver].y_m * 1000.0) &&
                    get_i16(bytes, header + 88) == 1,
                "SEG-Y source/receiver coordinate headers are wrong");
            expect(
                get_u16(bytes, header + 114) == expected.sample_count &&
                    get_u16(bytes, header + 116) == 500,
                "SEG-Y trace sampling headers are wrong");
            for (std::size_t sample = 0; sample < expected.sample_count; ++sample) {
                expect(
                    get_float32(bytes, header + 240 + sample * 4) ==
                        (*components[component])[
                            receiver * expected.sample_count + sample],
                    "SEG-Y receiver-major component sample changed");
            }
        }
    }

    const auto expected_values = receiver_major_component_samples(expected);
    const auto actual_values = wave3d::io::read_ieee_segy_samples(
        path, trace_count, expected.sample_count);
    expect(actual_values == expected_values, "SEG-Y combined sample readback changed");

    std::filesystem::resize_file(path, std::filesystem::file_size(path) - 1);
    bool truncated_threw = false;
    try {
        static_cast<void>(wave3d::io::read_ieee_segy_samples(
            path, trace_count, expected.sample_count));
    } catch (const std::invalid_argument&) {
        truncated_threw = true;
    }
    expect(truncated_threw, "truncated SEG-Y must fail explicitly");
    std::filesystem::remove(path);
}

void test_fractional_microsecond_interval_rejected() {
    auto invalid = traces();
    invalid.dt_s = 0.0005005;
    const auto path = temporary_prefix().string() + ".sgy";
    bool threw = false;
    try {
        wave3d::io::write_segy(path, invalid);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "fractional-microsecond Rev1 interval must fail explicitly");
    expect(
        !std::filesystem::exists(path),
        "invalid SEG-Y interval left a partial output file");

    bool count_threw = false;
    try {
        wave3d::io::require_segy_rev1_sample_axis(65536, 0.0005);
    } catch (const std::invalid_argument&) {
        count_threw = true;
    }
    expect(count_threw, "oversized Rev1 sample axis must fail before writing");
}

} // namespace

int main() {
    test_single_file_standard_headers_and_samples();
    test_fractional_microsecond_interval_rejected();
    if (failures != 0) {
        std::cerr << failures << " SEG-Y I/O test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D single-file SEG-Y I/O tests passed\n";
    return 0;
}
