#pragma once

#include "wave3d/core/coordinates.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace wave3d::io {

inline void write_receiver_csv(
    const std::string& path,
    const std::vector<PhysicalPoint3D>& receivers) {
    if (receivers.empty()) {
        throw std::invalid_argument("receiver CSV cannot be empty");
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open receiver CSV for writing: " + path);
    }
    output << "x_m,y_m,z_m\n" << std::setprecision(17);
    for (const auto& point : receivers) {
        if (!std::isfinite(point.x_m) || !std::isfinite(point.y_m) ||
            !std::isfinite(point.z_m)) {
            throw std::invalid_argument("receiver CSV coordinates must be finite");
        }
        output << point.x_m << ',' << point.y_m << ',' << point.z_m << '\n';
    }
    if (!output) {
        throw std::runtime_error("failed while writing receiver CSV: " + path);
    }
}

[[nodiscard]] inline std::vector<PhysicalPoint3D> read_receiver_csv(
    const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open receiver CSV for reading: " + path);
    }
    std::string line;
    if (!std::getline(input, line) || line != "x_m,y_m,z_m") {
        throw std::invalid_argument(
            "receiver CSV header must be exactly x_m,y_m,z_m");
    }
    std::vector<PhysicalPoint3D> receivers;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) {
            throw std::invalid_argument(
                "receiver CSV contains an empty data row");
        }
        const auto first = line.find(',');
        const auto second = first == std::string::npos
                                ? std::string::npos
                                : line.find(',', first + 1);
        if (first == std::string::npos || second == std::string::npos ||
            line.find(',', second + 1) != std::string::npos) {
            throw std::invalid_argument(
                "receiver CSV row " + std::to_string(line_number) +
                " must contain three columns");
        }
        std::size_t consumed_x = 0;
        std::size_t consumed_y = 0;
        std::size_t consumed_z = 0;
        const std::string x_text = line.substr(0, first);
        const std::string y_text = line.substr(first + 1, second - first - 1);
        const std::string z_text = line.substr(second + 1);
        const double x = std::stod(x_text, &consumed_x);
        const double y = std::stod(y_text, &consumed_y);
        const double z = std::stod(z_text, &consumed_z);
        if (consumed_x != x_text.size() || consumed_y != y_text.size() ||
            consumed_z != z_text.size() || !std::isfinite(x) ||
            !std::isfinite(y) || !std::isfinite(z)) {
            throw std::invalid_argument(
                "receiver CSV row contains an invalid coordinate");
        }
        receivers.push_back({x, y, z});
    }
    if (receivers.empty()) {
        throw std::invalid_argument("receiver CSV contains no receivers");
    }
    return receivers;
}

} // namespace wave3d::io
