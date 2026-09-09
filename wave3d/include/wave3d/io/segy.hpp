#pragma once

#include "wave3d/io/data.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace wave3d::io {

void write_segy(
    const std::string& path,
    const ThreeComponentTraces& traces);

[[nodiscard]] std::vector<float> read_ieee_segy_samples(
    const std::string& path,
    std::size_t expected_trace_count,
    std::size_t expected_samples_per_trace);

[[nodiscard]] std::vector<float> read_ieee_segy_volume(
    const std::string& path,
    std::size_t nx,
    std::size_t ny,
    std::size_t nz);

} // namespace wave3d::io
