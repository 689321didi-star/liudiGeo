#pragma once

#include "wave3d/core/checked_size.hpp"
#include "wave3d/core/coordinates.hpp"
#include "wave3d/core/grid.hpp"

#include <cmath>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wave3d {

struct Receiver {
    PhysicalPoint3D physical_location{};
    FractionalStorageCoordinate3D storage_location{};
};

struct ReceiverSet {
    std::vector<Receiver> receivers;
};

struct RegularSurfaceReceiverGrid {
    double first_x_m{0.0};
    double first_y_m{0.0};
    double spacing_x_m{0.0};
    double spacing_y_m{0.0};
    std::size_t count_x{0};
    std::size_t count_y{0};
};

[[nodiscard]] inline ReceiverSet prepare_receiver_set(
    const Grid3D& grid,
    const std::vector<PhysicalPoint3D>& locations) {
    if (locations.empty()) {
        throw std::invalid_argument("receiver set must not be empty");
    }
    ReceiverSet result{};
    result.receivers.reserve(locations.size());
    for (const auto& location : locations) {
        result.receivers.push_back(
            {location, physical_to_storage_coordinate(grid, location)});
    }
    return result;
}

[[nodiscard]] inline ReceiverSet make_regular_surface_receivers(
    const Grid3D& grid,
    const RegularSurfaceReceiverGrid& specification) {
    if (specification.count_x == 0 || specification.count_y == 0) {
        throw std::invalid_argument("regular receiver counts must be positive");
    }
    if (!std::isfinite(specification.first_x_m) ||
        !std::isfinite(specification.first_y_m) ||
        !std::isfinite(specification.spacing_x_m) ||
        !std::isfinite(specification.spacing_y_m) ||
        specification.first_x_m < 0.0 || specification.first_y_m < 0.0 ||
        specification.spacing_x_m < 0.0 || specification.spacing_y_m < 0.0) {
        throw std::invalid_argument(
            "regular receiver coordinates and spacing must be finite and non-negative");
    }
    if ((specification.count_x > 1 && specification.spacing_x_m == 0.0) ||
        (specification.count_y > 1 && specification.spacing_y_m == 0.0)) {
        throw std::invalid_argument(
            "multi-receiver axes require positive spacing");
    }

    const auto receiver_count = detail::checked_size_product(
        specification.count_x,
        specification.count_y,
        "Wave3D receiver count overflows size_t");
    std::vector<PhysicalPoint3D> locations;
    locations.reserve(receiver_count);
    for (std::size_t y = 0; y < specification.count_y; ++y) {
        const double y_m = specification.first_y_m +
                           static_cast<double>(y) * specification.spacing_y_m;
        for (std::size_t x = 0; x < specification.count_x; ++x) {
            const double x_m = specification.first_x_m +
                               static_cast<double>(x) * specification.spacing_x_m;
            locations.push_back({x_m, y_m, 0.0});
        }
    }
    return prepare_receiver_set(grid, locations);
}

[[nodiscard]] inline std::string resolved_receiver_metadata(
    const ReceiverSet& receiver_set) {
    std::ostringstream output;
    output << "receiver_coordinate_convention=" << coordinate_convention() << '\n'
           << "receiver_components=vx,vy,vz\n"
           << "receiver_count=" << receiver_set.receivers.size() << '\n'
           << "receiver_order=x_fastest_for_regular_grids";
    return output.str();
}

} // namespace wave3d
