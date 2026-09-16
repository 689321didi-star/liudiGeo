#include "wave3d/io/segy.hpp"

#include "wave3d/core/checked_size.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wave3d::io {
namespace {

void put_u16(std::vector<unsigned char>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<unsigned char>((value >> 8U) & 0xffU);
    bytes[offset + 1] = static_cast<unsigned char>(value & 0xffU);
}

void put_i16(std::vector<unsigned char>& bytes, std::size_t offset, std::int16_t value) {
    put_u16(bytes, offset, static_cast<std::uint16_t>(value));
}

void put_u32(std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<unsigned char>((value >> 24U) & 0xffU);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 16U) & 0xffU);
    bytes[offset + 2] = static_cast<unsigned char>((value >> 8U) & 0xffU);
    bytes[offset + 3] = static_cast<unsigned char>(value & 0xffU);
}

void put_i32(std::vector<unsigned char>& bytes, std::size_t offset, std::int32_t value) {
    put_u32(bytes, offset, static_cast<std::uint32_t>(value));
}

[[nodiscard]] std::uint16_t get_u16(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1]));
}

[[nodiscard]] std::uint32_t get_u32(
    const unsigned char* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

[[nodiscard]] std::int32_t scaled_coordinate(double coordinate_m) {
    const double scaled = std::round(coordinate_m * 1000.0);
    if (!std::isfinite(scaled) ||
        scaled < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        scaled > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        throw std::overflow_error("SEG-Y millimetre coordinate exceeds int32");
    }
    return static_cast<std::int32_t>(scaled);
}

[[nodiscard]] std::uint16_t segy_interval_us(double dt_s) {
    const double exact_microseconds = dt_s * 1.0e6;
    const double rounded_microseconds = std::round(exact_microseconds);
    const double tolerance = 1.0e-9 * std::max(1.0, std::abs(exact_microseconds));
    if (!std::isfinite(exact_microseconds) || exact_microseconds < 1.0 ||
        exact_microseconds > 65535.0 ||
        std::abs(exact_microseconds - rounded_microseconds) > tolerance) {
        throw std::invalid_argument(
            "SEG-Y Revision 1 sample interval must be an integer number of "
            "microseconds in [1,65535]");
    }
    return static_cast<std::uint16_t>(rounded_microseconds);
}

void put_text_card(
    std::string& header,
    std::size_t card,
    const std::string& body) {
    std::ostringstream prefix;
    prefix << 'C' << std::setw(2) << card + 1 << ' ';
    const auto line_start = card * 80;
    const auto prefix_text = prefix.str();
    std::copy(prefix_text.begin(), prefix_text.end(), header.begin() + line_start);
    const auto body_size = std::min<std::size_t>(body.size(), 80 - prefix_text.size());
    std::copy_n(
        body.begin(),
        body_size,
        header.begin() + line_start + prefix_text.size());
}

[[nodiscard]] std::string scientific_value(double value) {
    std::ostringstream output;
    output << std::scientific << std::setprecision(9) << value;
    return output.str();
}

void write_text_header(
    std::ofstream& output,
    const ThreeComponentTraces& traces,
    std::uint16_t interval_us,
    std::size_t trace_count) {
    std::string header(3200, ' ');
    for (std::size_t card = 0; card < 40; ++card) {
        put_text_card(header, card, "");
    }
    put_text_card(header, 0, "WAVE3D THREE-COMPONENT PARTICLE-VELOCITY RECEIVER RECORD");
    put_text_card(
        header,
        1,
        "SEG-Y REVISION 1, BIG-ENDIAN, IEEE FLOAT32 SAMPLE FORMAT CODE 5");
    put_text_card(header, 2, "ONE FILE; TRACE ORDER: RECEIVER-MAJOR, THEN VX,VY,VZ");
    put_text_card(header, 3, "VX = IN-LINE/EAST (TRACE IDENTIFICATION CODE 14)");
    put_text_card(header, 4, "VY = CROSS-LINE/NORTH (TRACE IDENTIFICATION CODE 13)");
    put_text_card(header, 5, "VZ = VERTICAL/DOWN (TRACE IDENTIFICATION CODE 12)");
    put_text_card(
        header,
        6,
        "COORDINATES: X EAST, Y NORTH, Z POSITIVE DOWN; LENGTH UNIT METRE");
    put_text_card(
        header,
        7,
        "SCALCO=-1000 AND SCALEL=-1000; INTEGER COORDINATES ARE MILLIMETRES");
    put_text_card(
        header,
        8,
        "TRACE SAMPLES: PARTICLE VELOCITY M/S; NORMALIZATION NONE");
    put_text_card(
        header,
        9,
        "SAMPLE INTERVAL US=" + std::to_string(interval_us) +
            "; SAMPLES/TRACE=" + std::to_string(traces.sample_count));
    put_text_card(
        header,
        10,
        "RECEIVERS=" + std::to_string(traces.receiver_count) +
            "; DATA TRACES=" + std::to_string(trace_count) +
            "; ONE COMMON-SOURCE ENSEMBLE");
    put_text_card(
        header,
        11,
        "SOURCE LOCATION M: X=" +
            scientific_value(traces.source.physical_location.x_m) + " Y=" +
            scientific_value(traces.source.physical_location.y_m) + " Z=" +
            scientific_value(traces.source.physical_location.z_m));
    put_text_card(
        header,
        12,
        "SOURCE ORIGIN TIME S=" +
            scientific_value(traces.source.origin_time_s));
    put_text_card(
        header,
        13,
        "MOMENT TENSOR ORDER: MXX,MYY,MZZ,MXY,MXZ,MYZ; UNIT N*M");
    put_text_card(
        header,
        14,
        "MXX=" + scientific_value(traces.source.moment.m_xx_nm) +
            " MYY=" + scientific_value(traces.source.moment.m_yy_nm) +
            " MZZ=" + scientific_value(traces.source.moment.m_zz_nm));
    put_text_card(
        header,
        15,
        "MXY=" + scientific_value(traces.source.moment.m_xy_nm) +
            " MXZ=" + scientific_value(traces.source.moment.m_xz_nm) +
            " MYZ=" + scientific_value(traces.source.moment.m_yz_nm));
    put_text_card(
        header,
        16,
        "RICKER FREQ HZ=" +
            scientific_value(traces.source.wavelet.dominant_frequency_hz) +
            " PEAK DELAY S=" +
            scientific_value(traces.source.wavelet.peak_delay_s));
    put_text_card(
        header,
        17,
        "FIRST STORED SAMPLE TIME=DT; TIME AXIS IS (N+1)*DT");
    put_text_card(header, 18, "FIXED-LENGTH TRACES; NO EXTENDED TEXTUAL HEADERS");
    put_text_card(header, 39, "END TEXTUAL HEADER");
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
}

void write_float_be(std::ofstream& output, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "SEG-Y float32 size changed");
    std::memcpy(&bits, &value, sizeof(bits));
    const std::array<char, 4> bytes{{
        static_cast<char>((bits >> 24U) & 0xffU),
        static_cast<char>((bits >> 16U) & 0xffU),
        static_cast<char>((bits >> 8U) & 0xffU),
        static_cast<char>(bits & 0xffU)}};
    output.write(bytes.data(), bytes.size());
}

struct ScaledReceiverCoordinates {
    std::int32_t x_mm{0};
    std::int32_t y_mm{0};
    std::int32_t elevation_mm{0};
};

struct ComponentDescription {
    std::int16_t trace_identification_code;
    const std::vector<float>* samples;
};

void write_three_component_record(
    const std::string& path,
    const ThreeComponentTraces& traces) {
    require_segy_rev1_sample_axis(traces.sample_count, traces.dt_s);
    const auto interval_us = segy_interval_us(traces.dt_s);
    const auto trace_count = detail::checked_size_product(
        traces.receiver_count,
        std::size_t{3},
        "SEG-Y three-component trace count overflow");
    if (trace_count > static_cast<std::size_t>(
                          std::numeric_limits<std::int32_t>::max())) {
        throw std::invalid_argument("SEG-Y trace count exceeds int32 sequence range");
    }
    const std::int32_t source_x_mm =
        scaled_coordinate(traces.source.physical_location.x_m);
    const std::int32_t source_y_mm =
        scaled_coordinate(traces.source.physical_location.y_m);
    const std::int32_t source_depth_mm =
        scaled_coordinate(traces.source.physical_location.z_m);
    std::vector<ScaledReceiverCoordinates> receiver_coordinates;
    receiver_coordinates.reserve(traces.receiver_count);
    for (const auto& receiver : traces.receiver_coordinates_m) {
        receiver_coordinates.push_back({
            scaled_coordinate(receiver.x_m),
            scaled_coordinate(receiver.y_m),
            scaled_coordinate(-receiver.z_m)});
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open SEG-Y output: " + path);
    }
    write_text_header(output, traces, interval_us, trace_count);
    std::vector<unsigned char> binary_header(400);
    put_i32(binary_header, 0, 1);
    put_i32(binary_header, 4, 1);
    put_i32(binary_header, 8, 1);
    put_i16(
        binary_header,
        12,
        trace_count <= static_cast<std::size_t>(
                           std::numeric_limits<std::int16_t>::max())
            ? static_cast<std::int16_t>(trace_count)
            : 0);
    put_u16(binary_header, 16, interval_us);
    put_u16(binary_header, 18, interval_us);
    put_u16(binary_header, 20, static_cast<std::uint16_t>(traces.sample_count));
    put_u16(binary_header, 22, static_cast<std::uint16_t>(traces.sample_count));
    put_u16(binary_header, 24, 5);
    put_i16(binary_header, 28, 5);
    put_u16(binary_header, 54, 1);
    put_u16(binary_header, 300, 0x0100);
    put_u16(binary_header, 302, 1);
    put_i16(binary_header, 304, 0);
    output.write(
        reinterpret_cast<const char*>(binary_header.data()),
        static_cast<std::streamsize>(binary_header.size()));

    const std::array<ComponentDescription, 3> components{{
        {14, &traces.vx_m_s},
        {13, &traces.vy_m_s},
        {12, &traces.vz_m_s}}};
    for (std::size_t receiver = 0; receiver < traces.receiver_count; ++receiver) {
        for (std::size_t component = 0; component < components.size(); ++component) {
            const auto trace_index = receiver * components.size() + component;
            const auto trace_number = static_cast<std::int32_t>(trace_index + 1);
            std::vector<unsigned char> trace_header(240);
            put_i32(trace_header, 0, trace_number);
            put_i32(trace_header, 4, trace_number);
            put_i32(trace_header, 8, 1);
            put_i32(trace_header, 12, trace_number);
            put_i32(trace_header, 16, 1);
            put_i32(trace_header, 20, 1);
            put_i32(trace_header, 24, trace_number);
            put_i16(
                trace_header,
                28,
                components[component].trace_identification_code);
            put_i16(trace_header, 30, 1);
            put_i16(trace_header, 32, 1);
            put_i16(trace_header, 34, 1);
            put_i32(
                trace_header,
                40,
                receiver_coordinates[receiver].elevation_mm);
            put_i32(trace_header, 44, 0);
            put_i32(trace_header, 48, source_depth_mm);
            put_i16(trace_header, 68, -1000);
            put_i16(trace_header, 70, -1000);
            put_i32(trace_header, 72, source_x_mm);
            put_i32(trace_header, 76, source_y_mm);
            put_i32(
                trace_header,
                80,
                receiver_coordinates[receiver].x_mm);
            put_i32(
                trace_header,
                84,
                receiver_coordinates[receiver].y_mm);
            put_i16(trace_header, 88, 1);
            put_u16(
                trace_header,
                114,
                static_cast<std::uint16_t>(traces.sample_count));
            put_u16(trace_header, 116, interval_us);
            put_i32(trace_header, 196, 1);
            put_i16(trace_header, 200, 1);
            output.write(
                reinterpret_cast<const char*>(trace_header.data()),
                static_cast<std::streamsize>(trace_header.size()));
            for (std::size_t sample = 0; sample < traces.sample_count; ++sample) {
                write_float_be(
                    output,
                    (*components[component].samples)[
                        receiver * traces.sample_count + sample]);
            }
        }
    }
    if (!output) {
        throw std::runtime_error("failed while writing SEG-Y output: " + path);
    }
}

} // namespace

void require_segy_rev1_sample_axis(
    std::size_t sample_count,
    double dt_s) {
    if (sample_count == 0 || sample_count > 65535) {
        throw std::invalid_argument(
            "SEG-Y Revision 1 sample count must be in [1,65535]");
    }
    static_cast<void>(segy_interval_us(dt_s));
}

void write_segy(
    const std::string& path,
    const ThreeComponentTraces& traces) {
    require_valid_traces(traces);
    write_three_component_record(path, traces);
}

void require_ieee_segy_layout(
    const std::string& path,
    std::size_t expected_trace_count,
    std::size_t expected_samples_per_trace) {
    if (expected_trace_count == 0 || expected_samples_per_trace == 0 ||
        expected_samples_per_trace > 65535) {
        throw std::invalid_argument("expected SEG-Y dimensions are invalid");
    }
    const auto sample_bytes = detail::checked_size_product(
        expected_samples_per_trace,
        sizeof(float),
        "SEG-Y trace byte count overflow");
    const auto trace_bytes = detail::checked_size_add(
        std::size_t{240}, sample_bytes, "SEG-Y trace byte count overflow");
    const auto expected_bytes = detail::checked_size_add(
        std::size_t{3600},
        detail::checked_size_product(
            expected_trace_count,
            trace_bytes,
            "SEG-Y file byte count overflow"),
        "SEG-Y file byte count overflow");
    std::error_code size_error;
    const auto actual_bytes = std::filesystem::file_size(path, size_error);
    if (size_error || actual_bytes != expected_bytes) {
        throw std::invalid_argument("SEG-Y file size does not match its dimensions");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open SEG-Y input: " + path);
    }
    std::vector<unsigned char> headers(3600);
    input.read(
        reinterpret_cast<char*>(headers.data()),
        static_cast<std::streamsize>(headers.size()));
    if (input.gcount() != static_cast<std::streamsize>(headers.size()) ||
        get_u16(headers, 3200 + 24) != 5 ||
        get_u16(headers, 3200 + 20) != expected_samples_per_trace) {
        throw std::invalid_argument(
            "SEG-Y must have complete headers, format 5, and matching samples");
    }
    for (std::size_t trace = 0; trace < expected_trace_count; ++trace) {
        std::vector<unsigned char> trace_header(240);
        input.read(
            reinterpret_cast<char*>(trace_header.data()),
            static_cast<std::streamsize>(trace_header.size()));
        if (input.gcount() != static_cast<std::streamsize>(trace_header.size()) ||
            get_u16(trace_header, 114) != expected_samples_per_trace) {
            throw std::invalid_argument("SEG-Y trace header is truncated/mismatched");
        }
        input.seekg(static_cast<std::streamoff>(sample_bytes), std::ios::cur);
        if (!input) {
            throw std::invalid_argument("SEG-Y sample data is truncated");
        }
    }
    char extra = 0;
    if (input.get(extra)) {
        throw std::invalid_argument("SEG-Y file has unexpected extra traces/data");
    }
}

std::vector<float> read_ieee_segy_samples(
    const std::string& path,
    std::size_t expected_trace_count,
    std::size_t expected_samples_per_trace) {
    if (expected_trace_count == 0 || expected_samples_per_trace == 0 ||
        expected_samples_per_trace > 65535) {
        throw std::invalid_argument("expected SEG-Y dimensions are invalid");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open SEG-Y input: " + path);
    }
    std::vector<unsigned char> headers(3600);
    input.read(
        reinterpret_cast<char*>(headers.data()),
        static_cast<std::streamsize>(headers.size()));
    if (input.gcount() != static_cast<std::streamsize>(headers.size()) ||
        get_u16(headers, 3200 + 24) != 5 ||
        get_u16(headers, 3200 + 20) != expected_samples_per_trace) {
        throw std::invalid_argument(
            "SEG-Y must have complete headers, format 5, and matching samples");
    }
    const auto value_count = detail::checked_size_product(
        expected_trace_count,
        expected_samples_per_trace,
        "SEG-Y value count overflow");
    std::vector<float> values(value_count);
    for (std::size_t trace = 0; trace < expected_trace_count; ++trace) {
        std::vector<unsigned char> trace_header(240);
        input.read(
            reinterpret_cast<char*>(trace_header.data()),
            static_cast<std::streamsize>(trace_header.size()));
        if (input.gcount() != static_cast<std::streamsize>(trace_header.size()) ||
            get_u16(trace_header, 114) != expected_samples_per_trace) {
            throw std::invalid_argument("SEG-Y trace header is truncated/mismatched");
        }
        for (std::size_t sample = 0; sample < expected_samples_per_trace; ++sample) {
            std::array<unsigned char, 4> bytes{};
            input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
            if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
                throw std::invalid_argument("SEG-Y sample data is truncated");
            }
            const std::uint32_t bits = get_u32(bytes.data());
            float value = 0.0F;
            std::memcpy(&value, &bits, sizeof(value));
            if (!std::isfinite(value)) {
                throw std::invalid_argument("SEG-Y sample is non-finite");
            }
            values[trace * expected_samples_per_trace + sample] = value;
        }
    }
    char extra = 0;
    if (input.get(extra)) {
        throw std::invalid_argument("SEG-Y file has unexpected extra traces/data");
    }
    return values;
}

std::vector<float> read_ieee_segy_volume(
    const std::string& path,
    std::size_t nx,
    std::size_t ny,
    std::size_t nz) {
    const auto traces = detail::checked_size_product(
        nx, ny, "SEG-Y model trace count overflow");
    return read_ieee_segy_samples(path, traces, nz);
}

} // namespace wave3d::io
