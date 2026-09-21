#pragma once

#include "wave3d/io/data.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace wave3d::io {

enum class SegyComponent { Vx, Vy, Vz };

void require_segy_rev1_sample_axis(
    std::size_t sample_count,
    double dt_s);

void write_component_segy(
    const std::string& path,
    const ThreeComponentTraces& traces,
    SegyComponent component);

void require_ieee_segy_layout(
    const std::string& path,
    std::size_t expected_trace_count,
    std::size_t expected_samples_per_trace);

void require_ieee_component_segy_layout(
    const std::string& path,
    std::size_t expected_receiver_count,
    std::size_t expected_samples_per_trace,
    SegyComponent component);

[[nodiscard]] std::vector<float> read_ieee_segy_samples(
    const std::string& path,
    std::size_t expected_trace_count,
    std::size_t expected_samples_per_trace);

// Reads trace-major values: trace=(y*nx+x), then z sample.
[[nodiscard]] std::vector<float> read_ieee_segy_volume(
    const std::string& path,
    std::size_t nx,
    std::size_t ny,
    std::size_t nz);

// Reads the same subset and returns canonical Wave3D [z][y][x] storage.
[[nodiscard]] std::vector<float> read_ieee_segy_volume_zyx(
    const std::string& path,
    std::size_t nx,
    std::size_t ny,
    std::size_t nz);

} // namespace wave3d::io
