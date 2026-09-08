#include "wave3d/core/coordinates.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near(double left, double right, double tolerance = 1.0e-12) {
    return std::abs(left - right) <= tolerance;
}

wave3d::Grid3D test_grid() {
    return {
        5, 4, 3,
        10.0F, 20.0F, 5.0F,
        2,
        {1, 3}, {2, 4}, {0, 5}};
}

void test_fractional_coordinate_mapping() {
    const auto grid = test_grid();
    const auto maximum = wave3d::physical_domain_max(grid);
    expect(
        near(maximum.x_m, 40.0) && near(maximum.y_m, 60.0) &&
            near(maximum.z_m, 10.0),
        "physical domain maximum must use physical node coordinates");

    const wave3d::PhysicalPoint3D point{15.0, 20.0, 2.5};
    const auto physical = wave3d::physical_to_grid_coordinate(grid, point);
    expect(
        near(physical.x, 1.5) && near(physical.y, 1.0) &&
            near(physical.z, 0.5),
        "physical point must map to fractional physical-grid coordinates");

    const auto storage = wave3d::physical_to_storage_coordinate(grid, point);
    expect(
        near(storage.x, 4.5) && near(storage.y, 5.0) &&
            near(storage.z, 2.5),
        "physical point must include halo and lower-boundary offsets");
}

void test_integer_coordinate_mapping() {
    const auto grid = test_grid();
    const wave3d::PhysicalGridIndex3D physical{4, 3, 2};
    const auto storage = wave3d::physical_to_storage_index(grid, physical);
    expect(
        storage.x == 7 && storage.y == 7 && storage.z == 4,
        "physical index must map to allocated storage index");

    const auto point = wave3d::physical_index_to_point(grid, physical);
    expect(
        near(point.x_m, 40.0) && near(point.y_m, 60.0) &&
            near(point.z_m, 10.0),
        "physical index must map back to metres");
    expect(
        grid.physical_linear_index(1, 0, 0) == 1 &&
            grid.physical_linear_index(0, 1, 0) == 5 &&
            grid.physical_linear_index(0, 0, 1) == 20,
        "physical model layout must be [z][y][x] with x contiguous");
}

void test_invalid_coordinates() {
    const auto grid = test_grid();
    bool threw = false;
    try {
        static_cast<void>(wave3d::physical_to_grid_coordinate(
            grid, {-0.01, 0.0, 0.0}));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "negative physical coordinate must fail");

    threw = false;
    try {
        static_cast<void>(wave3d::physical_to_grid_coordinate(
            grid, {40.01, 0.0, 0.0}));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "coordinate beyond final physical node must fail");

    threw = false;
    try {
        static_cast<void>(wave3d::physical_to_grid_coordinate(
            grid,
            {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "non-finite physical coordinate must fail");

    threw = false;
    try {
        static_cast<void>(wave3d::physical_to_storage_index(grid, {5, 0, 0}));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "out-of-range physical index must fail");

    auto invalid_grid = grid;
    invalid_grid.dx_m = 0.0F;
    threw = false;
    try {
        wave3d::require_valid_grid_geometry(invalid_grid);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "invalid grid geometry must fail coordinate preparation");
}

void test_coordinate_metadata() {
    const std::string convention(wave3d::coordinate_convention());
    expect(
        convention.find("x=east") != std::string::npos &&
            convention.find("z=down") != std::string::npos &&
            convention.find("surface_z=0") != std::string::npos,
        "coordinate convention must be explicit in metadata");
}

} // namespace

int main() {
    test_fractional_coordinate_mapping();
    test_integer_coordinate_mapping();
    test_invalid_coordinates();
    test_coordinate_metadata();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Wave3D coordinate tests passed\n";
    return 0;
}
