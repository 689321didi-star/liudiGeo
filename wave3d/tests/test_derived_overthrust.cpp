#include "wave3d/model/derived_overthrust.hpp"

#include <cmath>
#include <cstddef>
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
void expect_throw(Function&& function, const std::string& message) {
    bool threw = false;
    try {
        function();
    } catch (const Exception&) {
        threw = true;
    }
    expect(threw, message);
}

[[nodiscard]] wave3d::Grid3D source_grid() {
    return {5, 4, 3, 25.0F, 25.0F, 25.0F, 0, {}, {}, {}};
}

[[nodiscard]] std::vector<float> uniquely_indexed_vp() {
    const auto grid = source_grid();
    std::vector<float> values(grid.physical_cell_count());
    for (std::size_t z = 0; z < grid.nz; ++z) {
        for (std::size_t y = 0; y < grid.ny; ++y) {
            for (std::size_t x = 0; x < grid.nx; ++x) {
                values[grid.physical_linear_index(x, y, z)] =
                    2000.0F + static_cast<float>(x) +
                    10.0F * static_cast<float>(y) +
                    100.0F * static_cast<float>(z);
            }
        }
    }
    return values;
}

[[nodiscard]] wave3d::OutputStorageGeometry output_storage() {
    return {6, {2, 3}, {4, 5}, {0, 7}};
}

void test_exact_crop_axes_and_derivation() {
    const wave3d::PhysicalVolumeWindow3D window{1, 1, 1, 3, 2, 2};
    const auto source = uniquely_indexed_vp();
    const auto first = wave3d::make_derived_overthrust_elastic_model(
        source_grid(), source, window, output_storage());
    const auto second = wave3d::make_derived_overthrust_elastic_model(
        source_grid(), source, window, output_storage());

    expect(
        first.grid.nx == 3 && first.grid.ny == 2 && first.grid.nz == 2 &&
            first.grid.dx_m == 25.0F && first.grid.halo == 6 &&
            first.grid.x_boundary.lower_absorbing == 2 &&
            first.grid.y_boundary.upper_absorbing == 5 &&
            first.grid.z_boundary.lower_absorbing == 0 &&
            first.grid.z_boundary.upper_absorbing == 7,
        "derived grid did not preserve crop spacing/storage geometry");
    expect(
        first.vp_m_s[first.grid.physical_linear_index(0, 0, 0)] == 2111.0F &&
            first.vp_m_s[first.grid.physical_linear_index(2, 0, 0)] == 2113.0F &&
            first.vp_m_s[first.grid.physical_linear_index(0, 1, 0)] == 2121.0F &&
            first.vp_m_s[first.grid.physical_linear_index(0, 0, 1)] == 2211.0F,
        "crop changed [z][y][x] order or did not preserve Vp exactly");

    const auto index = first.grid.physical_linear_index(2, 1, 1);
    const double expected_vs = 2223.0 / std::sqrt(3.0);
    const double expected_density =
        1000.0 * 0.31 * std::pow(2223.0, 0.25);
    expect(
        first.vs_m_s[index] == static_cast<float>(expected_vs) &&
            first.density_kg_m3[index] ==
                static_cast<float>(expected_density),
        "elastic derivation did not use the frozen binary64 formulas");
    expect(
        first.vp_m_s == second.vp_m_s &&
            first.vs_m_s == second.vs_m_s &&
            first.density_kg_m3 == second.density_kg_m3,
        "derived model is not deterministic");
    expect(
        wave3d::validate(first).empty(),
        "derived crop does not satisfy the physical-model contract");
}

void test_invalid_source_and_crop_rejection() {
    const wave3d::PhysicalVolumeWindow3D valid{1, 1, 1, 3, 2, 2};
    auto source = uniquely_indexed_vp();
    source.pop_back();
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(wave3d::make_derived_overthrust_elastic_model(
                source_grid(), source, valid, output_storage()));
        },
        "truncated source volume must fail");

    source = uniquely_indexed_vp();
    source[source_grid().physical_linear_index(1, 1, 1)] =
        std::numeric_limits<float>::quiet_NaN();
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(wave3d::make_derived_overthrust_elastic_model(
                source_grid(), source, valid, output_storage()));
        },
        "non-finite source Vp in the crop must fail");

    source = uniquely_indexed_vp();
    source[source_grid().physical_linear_index(1, 1, 1)] = 0.0F;
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(wave3d::make_derived_overthrust_elastic_model(
                source_grid(), source, valid, output_storage()));
        },
        "non-positive source Vp in the crop must fail");

    source = uniquely_indexed_vp();
    source[source_grid().physical_linear_index(1, 1, 1)] =
        std::numeric_limits<float>::denorm_min();
    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(wave3d::make_derived_overthrust_elastic_model(
                source_grid(), source, valid, output_storage()));
        },
        "derived float32 underflow must fail through material validation");

    expect_throw<std::invalid_argument>(
        [&] {
            static_cast<void>(wave3d::make_derived_overthrust_elastic_model(
                source_grid(), source, {1, 1, 1, 0, 2, 2}, output_storage()));
        },
        "zero crop dimension must fail");
    expect_throw<std::out_of_range>(
        [&] {
            static_cast<void>(wave3d::make_derived_overthrust_elastic_model(
                source_grid(), source, {4, 1, 1, 2, 2, 2}, output_storage()));
        },
        "out-of-range crop must fail");
    expect_throw<std::overflow_error>(
        [&] {
            static_cast<void>(wave3d::make_derived_overthrust_elastic_model(
                source_grid(),
                source,
                {std::numeric_limits<std::size_t>::max(), 1, 1, 2, 2, 2},
                output_storage()));
        },
        "overflowing crop range must fail");

    auto bad_storage = output_storage();
    bad_storage.x_boundary.upper_absorbing =
        std::numeric_limits<std::size_t>::max();
    expect_throw<std::overflow_error>(
        [&] {
            static_cast<void>(wave3d::make_derived_overthrust_elastic_model(
                source_grid(), source, valid, bad_storage));
        },
        "overflowing output storage geometry must fail");
}

} // namespace

int main() {
    test_exact_crop_axes_and_derivation();
    test_invalid_source_and_crop_rejection();
    if (failures != 0) {
        std::cerr << failures << " derived Overthrust test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D derived Overthrust model tests passed\n";
    return 0;
}
