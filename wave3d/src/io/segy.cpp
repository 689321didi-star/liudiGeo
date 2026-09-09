#include "wave3d/io/segy.hpp"

#include "wave3d/core/checked_size.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
    const double microseconds = std::round(dt_s * 1.0e6);
    if (!std::isfinite(microseconds) || microseconds < 1.0 ||
        microseconds > 65535.0) {
        throw std::invalid_argument(
            "SEG-Y rounded sample interval must fit unsigned 16-bit microseconds");
    }
    return static_cast<std::uint16_t>(microseconds);
}

void write_text_header(std::ofstream& output, TraceComponent component) {
    std::string header(3200, ' ');
    for (std::size_t card = 0; card < 40; ++card) {
        std::ostringstream prefix;
        prefix << 'C' << std::setw(2) << std::setfill('0') << card + 1 << ' ';
        const auto text = prefix.str();
        std::copy(text.begin(), text.end(), header.begin() + card * 80);
    }
    const std::string title =
        std::string("WAVE3D ELASTIC PARTICLE VELOCITY COMPONENT ") +
        trace_component_name(component) + " SI M/S IEEE FLOAT32";
    std::copy(title.begin(), title.end(), header.begin() + 4);
    const std::string final_card = "C40 END TEXTUAL HEADER";
    std::copy(final_card.begin(), final_card.end(), header.begin() + 39 * 80);
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

void write_sidecar(
    const std::string& path,
    const ThreeComponentTraces& traces,
    TraceComponent component,
    std::uint16_t interval_us) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open SEG-Y sidecar: " + path);
    }
    output << std::setprecision(17)
           << "{\n"
           << "  \"schema\": \"wave3d.segy-sidecar.v1\",\n"
           << "  \"component\": \"" << trace_component_name(component)
           << "\",\n"
           << "  \"orientation\": \"x=east,y=north,z=down\",\n"
           << "  \"trace_axes\": \"receiver,time\",\n"
           << "  \"units\": \"m/s\",\n"
           << "  \"normalization\": \"none\",\n"
           << "  \"dt_s_exact\": " << traces.dt_s << ",\n"
           << "  \"dt_us_header_rounded\": " << interval_us << ",\n"
           << "  \"receiver_count\": " << traces.receiver_count << ",\n"
           << "  \"sample_count\": " << traces.sample_count << ",\n"
           << "  \"source_location_m\": ["
           << traces.source.physical_location.x_m << ','
           << traces.source.physical_location.y_m << ','
           << traces.source.physical_location.z_m << "],\n"
           << "  \"source_origin_time_s\": " << traces.source.origin_time_s
           << ",\n"
           << "  \"moment_tensor_order\": \"Mxx,Myy,Mzz,Mxy,Mxz,Myz\",\n"
           << "  \"moment_tensor_nm\": ["
           << traces.source.moment.m_xx_nm << ','
           << traces.source.moment.m_yy_nm << ','
           << traces.source.moment.m_zz_nm << ','
           << traces.source.moment.m_xy_nm << ','
           << traces.source.moment.m_xz_nm << ','
           << traces.source.moment.m_yz_nm << "],\n"
           << "  \"receiver_coordinates_m\": [";
    for (std::size_t receiver = 0; receiver < traces.receiver_count; ++receiver) {
        const auto& point = traces.receiver_coordinates_m[receiver];
        if (receiver != 0) {
            output << ',';
        }
        output << '[' << point.x_m << ',' << point.y_m << ',' << point.z_m << ']';
    }
    output << "]\n}\n";
    if (!output) {
        throw std::runtime_error("failed while writing SEG-Y sidecar: " + path);
    }
}

void write_component(
    const std::string& path,
    const ThreeComponentTraces& traces,
    TraceComponent component,
    const std::vector<float>& samples) {
    if (traces.sample_count > 65535) {
        throw std::invalid_argument("SEG-Y trace sample count exceeds uint16");
    }
    const auto interval_us = segy_interval_us(traces.dt_s);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open SEG-Y output: " + path);
    }
    write_text_header(output, component);
    std::vector<unsigned char> binary_header(400);
    put_u16(binary_header, 12, static_cast<std::uint16_t>(
        std::min<std::size_t>(traces.receiver_count, 65535)));
    put_u16(binary_header, 16, interval_us);
    put_u16(binary_header, 20, static_cast<std::uint16_t>(traces.sample_count));
    put_u16(binary_header, 24, 5);
    put_u16(binary_header, 54, 1);
    put_u16(binary_header, 300, 0x0100);
    put_u16(binary_header, 302, 1);
    output.write(
        reinterpret_cast<const char*>(binary_header.data()),
        static_cast<std::streamsize>(binary_header.size()));

    for (std::size_t receiver = 0; receiver < traces.receiver_count; ++receiver) {
        std::vector<unsigned char> trace_header(240);
        put_u32(trace_header, 0, static_cast<std::uint32_t>(receiver + 1));
        put_u32(trace_header, 4, static_cast<std::uint32_t>(receiver + 1));
        put_u32(trace_header, 8, 1);
        put_u32(trace_header, 12, static_cast<std::uint32_t>(receiver + 1));
        put_i16(trace_header, 28, 12);
        put_i16(trace_header, 68, -1000);
        put_i32(trace_header, 72, scaled_coordinate(
            traces.source.physical_location.x_m));
        put_i32(trace_header, 76, scaled_coordinate(
            traces.source.physical_location.y_m));
        put_i32(trace_header, 80, scaled_coordinate(
            traces.receiver_coordinates_m[receiver].x_m));
        put_i32(trace_header, 84, scaled_coordinate(
            traces.receiver_coordinates_m[receiver].y_m));
        put_i16(trace_header, 88, 1);
        put_u16(trace_header, 114, static_cast<std::uint16_t>(traces.sample_count));
        put_u16(trace_header, 116, interval_us);
        output.write(
            reinterpret_cast<const char*>(trace_header.data()),
            static_cast<std::streamsize>(trace_header.size()));
        for (std::size_t sample = 0; sample < traces.sample_count; ++sample) {
            write_float_be(
                output,
                samples[receiver * traces.sample_count + sample]);
        }
    }
    if (!output) {
        throw std::runtime_error("failed while writing SEG-Y output: " + path);
    }
    write_sidecar(path + ".json", traces, component, interval_us);
}

} // namespace

const char* trace_component_name(TraceComponent component) {
    switch (component) {
    case TraceComponent::Vx:
        return "VX";
    case TraceComponent::Vy:
        return "VY";
    case TraceComponent::Vz:
        return "VZ";
    }
    throw std::invalid_argument("unknown trace component");
}

std::array<std::string, 3> write_segy_components(
    const std::string& path_prefix,
    const ThreeComponentTraces& traces) {
    require_valid_traces(traces);
    const std::array<std::string, 3> paths{{
        path_prefix + "_vx.sgy",
        path_prefix + "_vy.sgy",
        path_prefix + "_vz.sgy"}};
    write_component(paths[0], traces, TraceComponent::Vx, traces.vx_m_s);
    write_component(paths[1], traces, TraceComponent::Vy, traces.vy_m_s);
    write_component(paths[2], traces, TraceComponent::Vz, traces.vz_m_s);
    return paths;
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
