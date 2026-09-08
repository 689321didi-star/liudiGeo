#include "wave3d/model/physical_model.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

wave3d::Grid3D test_grid() {
    return {
        4, 3, 5,
        10.0F, 10.0F, 10.0F,
        1,
        {1, 1}, {1, 1}, {0, 1}};
}

void test_homogeneous_model() {
    const wave3d::ElasticMaterial material{3000.0F, 1700.0F, 2200.0F};
    const auto model = wave3d::make_homogeneous_model(test_grid(), material);
    expect(model.cell_count() == 60, "homogeneous model cell count must match grid");
    expect(wave3d::validate(model).empty(), "generated homogeneous model must validate");
    expect(
        std::all_of(model.vp_m_s.begin(), model.vp_m_s.end(), [](float value) {
            return value == 3000.0F;
        }),
        "homogeneous Vp must be deterministic");

    const auto extrema = wave3d::physical_model_extrema(model);
    expect(
        extrema.minimum.vp_m_s == 3000.0F &&
            extrema.maximum.vs_m_s == 1700.0F &&
            extrema.maximum.density_kg_m3 == 2200.0F,
        "homogeneous extrema must match the input material");
}

void test_horizontal_layers() {
    const std::vector<wave3d::HorizontalLayer> layers{
        {0.0, {2500.0F, 1200.0F, 2000.0F}},
        {20.0, {3200.0F, 1800.0F, 2300.0F}},
        {35.0, {4000.0F, 2300.0F, 2600.0F}}};
    const auto first = wave3d::make_horizontal_layered_model(test_grid(), layers);
    const auto second = wave3d::make_horizontal_layered_model(test_grid(), layers);
    expect(
        first.vp_m_s == second.vp_m_s && first.vs_m_s == second.vs_m_s &&
            first.density_kg_m3 == second.density_kg_m3,
        "layered model generation must be deterministic");

    const auto& grid = first.grid;
    expect(
        first.vp_m_s[grid.physical_linear_index(3, 2, 0)] == 2500.0F &&
            first.vp_m_s[grid.physical_linear_index(0, 0, 1)] == 2500.0F,
        "nodes above the first interface must use layer zero");
    expect(
        first.vp_m_s[grid.physical_linear_index(0, 0, 2)] == 3200.0F &&
            first.vp_m_s[grid.physical_linear_index(0, 0, 3)] == 3200.0F,
        "nodes at and below the 20 m interface must use layer one");
    expect(
        first.vp_m_s[grid.physical_linear_index(0, 0, 4)] == 4000.0F,
        "first node below the 35 m interface must use the deepest layer");

    const auto extrema = wave3d::physical_model_extrema(first);
    expect(
        extrema.minimum.vp_m_s == 2500.0F &&
            extrema.maximum.vp_m_s == 4000.0F &&
            extrema.minimum.density_kg_m3 == 2000.0F &&
            extrema.maximum.density_kg_m3 == 2600.0F,
        "layered model extrema must span every layer");
}

void test_invalid_models() {
    auto model = wave3d::make_homogeneous_model(
        test_grid(), {3000.0F, 1700.0F, 2200.0F});
    model.vp_m_s.pop_back();
    expect(!wave3d::validate(model).empty(), "mismatched model array must fail");

    model = wave3d::make_homogeneous_model(
        test_grid(), {3000.0F, 1700.0F, 2200.0F});
    model.vs_m_s[5] = std::numeric_limits<float>::quiet_NaN();
    expect(!wave3d::validate(model).empty(), "non-finite model cell must fail");

    model = wave3d::make_homogeneous_model(
        test_grid(), {3000.0F, 1700.0F, 2200.0F});
    model.vs_m_s[5] = model.vp_m_s[5];
    expect(!wave3d::validate(model).empty(), "Vp not greater than Vs must fail");

    bool threw = false;
    try {
        static_cast<void>(wave3d::make_horizontal_layered_model(test_grid(), {}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "empty layer definition must fail");

    threw = false;
    try {
        static_cast<void>(wave3d::make_horizontal_layered_model(
            test_grid(),
            {{0.0, {2500.0F, 1200.0F, 2000.0F}},
             {50.0, {3200.0F, 1800.0F, 2300.0F}}}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "layer below physical model extent must fail");
}

} // namespace

int main() {
    test_homogeneous_model();
    test_horizontal_layers();
    test_invalid_models();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Wave3D physical-model tests passed\n";
    return 0;
}
