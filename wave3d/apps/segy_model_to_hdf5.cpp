#include "wave3d/io/hdf5.hpp"
#include "wave3d/io/segy.hpp"
#include "wave3d/model/physical_model.hpp"

#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

[[nodiscard]] std::size_t parse_size(const char* text, const char* name) {
    std::size_t consumed = 0;
    const std::string value_text(text);
    const auto value = std::stoull(value_text, &consumed);
    if (consumed != value_text.size() || value == 0) {
        throw std::invalid_argument(std::string(name) + " must be positive");
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] float parse_spacing(const char* text, const char* name) {
    std::size_t consumed = 0;
    const std::string value_text(text);
    const double value = std::stod(value_text, &consumed);
    if (consumed != value_text.size() || !std::isfinite(value) ||
        !(value > 0.0) || value > std::numeric_limits<float>::max()) {
        throw std::invalid_argument(
            std::string(name) + " must be finite, positive float32");
    }
    return static_cast<float>(value);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 11) {
        std::cerr << "usage: wave3d_segy_model_to_hdf5 VP.sgy VS.sgy RHO.sgy "
                     "OUTPUT.h5 NX NY NZ DX_M DY_M DZ_M\n";
        return 2;
    }
    try {
        const auto nx = parse_size(argv[5], "NX");
        const auto ny = parse_size(argv[6], "NY");
        const auto nz = parse_size(argv[7], "NZ");
        const wave3d::Grid3D grid{
            nx, ny, nz,
            parse_spacing(argv[8], "DX_M"),
            parse_spacing(argv[9], "DY_M"),
            parse_spacing(argv[10], "DZ_M"),
            6,
            {}, {}, {}};
        wave3d::PhysicalModel model{
            grid,
            wave3d::io::read_ieee_segy_volume_zyx(argv[1], nx, ny, nz),
            wave3d::io::read_ieee_segy_volume_zyx(argv[2], nx, ny, nz),
            wave3d::io::read_ieee_segy_volume_zyx(argv[3], nx, ny, nz)};
        wave3d::require_valid_physical_model(model);
        wave3d::io::write_hdf5_model(argv[4], model);
        std::cout << "converted SEG-Y model to " << argv[4]
                  << " with axes [z][y][x]\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SEG-Y model conversion failed: " << error.what() << '\n';
        return 1;
    }
}
