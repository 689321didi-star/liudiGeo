#include "wave3d/model/elastic_coefficients.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool nearly_equal(float actual, double expected, double relative_tolerance) {
    return std::abs(static_cast<double>(actual) - expected) <=
           relative_tolerance * std::max(1.0, std::abs(expected));
}

wave3d::Grid3D coefficient_grid() {
    return {
        2, 2, 2,
        10.0F, 10.0F, 10.0F,
        1,
        {0, 0}, {0, 0}, {0, 0}};
}

void set_material(
    wave3d::PhysicalModel& model,
    std::size_t x,
    std::size_t y,
    std::size_t z,
    const wave3d::ElasticMaterial& material) {
    const auto index = model.grid.physical_linear_index(x, y, z);
    model.vp_m_s[index] = material.vp_m_s;
    model.vs_m_s[index] = material.vs_m_s;
    model.density_kg_m3[index] = material.density_kg_m3;
}

double mu(const wave3d::ElasticMaterial& material) {
    return static_cast<double>(material.density_kg_m3) *
           static_cast<double>(material.vs_m_s) *
           static_cast<double>(material.vs_m_s);
}

double harmonic_four(double a, double b, double c, double d) {
    return 4.0 / (1.0 / a + 1.0 / b + 1.0 / c + 1.0 / d);
}

void test_modulus_conversion_and_negative_lambda() {
    const wave3d::ElasticMaterial material{3000.0F, 1000.0F, 2000.0F};
    const auto moduli = wave3d::elastic_moduli(material);
    expect(moduli.lambda_pa == 14.0e9, "lambda conversion must be exact here");
    expect(
        moduli.shear_modulus_pa == 2.0e9,
        "shear-modulus conversion must be exact here");
    expect(
        std::abs(moduli.bulk_modulus_pa - 15.333333333333334e9) < 2.0e-6,
        "bulk-modulus conversion must use the accepted formula");

    const wave3d::ElasticMaterial auxetic_lambda{1600.0F, 1200.0F, 2000.0F};
    const auto negative = wave3d::elastic_moduli(auxetic_lambda);
    expect(negative.lambda_pa < 0.0, "valid negative lambda must be retained");
    expect(
        negative.bulk_modulus_pa > 0.0 && negative.shear_modulus_pa > 0.0,
        "negative lambda remains valid when K and mu are positive");
}

void test_staggered_averages_and_halo_extension() {
    const wave3d::ElasticMaterial m000{3000.0F, 1000.0F, 2000.0F};
    const wave3d::ElasticMaterial m100{3500.0F, 1500.0F, 2400.0F};
    const wave3d::ElasticMaterial m010{2800.0F, 1200.0F, 2200.0F};
    const wave3d::ElasticMaterial m110{4000.0F, 1800.0F, 2500.0F};
    const wave3d::ElasticMaterial m001{3200.0F, 1100.0F, 2100.0F};
    const wave3d::ElasticMaterial m101{3700.0F, 1400.0F, 2350.0F};
    const wave3d::ElasticMaterial m011{3300.0F, 1300.0F, 2250.0F};
    const wave3d::ElasticMaterial m111{4200.0F, 1700.0F, 2550.0F};

    auto model = wave3d::make_homogeneous_model(coefficient_grid(), m000);
    set_material(model, 0, 0, 0, m000);
    set_material(model, 1, 0, 0, m100);
    set_material(model, 0, 1, 0, m010);
    set_material(model, 1, 1, 0, m110);
    set_material(model, 0, 0, 1, m001);
    set_material(model, 1, 0, 1, m101);
    set_material(model, 0, 1, 1, m011);
    set_material(model, 1, 1, 1, m111);
    expect(wave3d::validate(model).empty(), "heterogeneous test model must validate");

    const auto coefficients = wave3d::prepare_elastic_coefficients(model);
    const auto cells = model.grid.allocated_cell_count();
    expect(
        coefficients.cell_count() == cells &&
            coefficients.bulk_modulus_pa.size() == cells &&
            coefficients.buoyancy_x_m3_kg.size() == cells &&
            coefficients.buoyancy_y_m3_kg.size() == cells &&
            coefficients.buoyancy_z_m3_kg.size() == cells &&
            coefficients.shear_modulus_xy_pa.size() == cells &&
            coefficients.shear_modulus_xz_pa.size() == cells &&
            coefficients.shear_modulus_yz_pa.size() == cells,
        "every prepared coefficient must cover allocated storage");

    const auto ox = model.grid.physical_origin_x();
    const auto oy = model.grid.physical_origin_y();
    const auto oz = model.grid.physical_origin_z();
    const auto center = model.grid.linear_index(ox, oy, oz);
    const auto center_moduli = wave3d::elastic_moduli(m000);
    expect(
        nearly_equal(coefficients.lambda_pa[center], center_moduli.lambda_pa, 1.0e-7),
        "center lambda must use the collocated physical material");
    expect(
        nearly_equal(
            coefficients.shear_modulus_pa[center],
            center_moduli.shear_modulus_pa,
            1.0e-7),
        "center mu must use the collocated physical material");
    expect(
        nearly_equal(
            coefficients.bulk_modulus_pa[center],
            center_moduli.bulk_modulus_pa,
            1.0e-7),
        "center K must use the collocated physical material");
    expect(
        nearly_equal(
            coefficients.buoyancy_x_m3_kg[center],
            2.0 / (2000.0 + 2400.0),
            1.0e-7),
        "x buoyancy must invert the arithmetic face-density mean");
    expect(
        nearly_equal(
            coefficients.buoyancy_y_m3_kg[center],
            2.0 / (2000.0 + 2200.0),
            1.0e-7),
        "y buoyancy must invert the arithmetic face-density mean");
    expect(
        nearly_equal(
            coefficients.buoyancy_z_m3_kg[center],
            2.0 / (2000.0 + 2100.0),
            1.0e-7),
        "z buoyancy must invert the arithmetic face-density mean");
    expect(
        nearly_equal(
            coefficients.shear_modulus_xy_pa[center],
            harmonic_four(mu(m000), mu(m100), mu(m010), mu(m110)),
            1.0e-7),
        "xy shear modulus must use the four-point harmonic mean");
    expect(
        nearly_equal(
            coefficients.shear_modulus_xz_pa[center],
            harmonic_four(mu(m000), mu(m100), mu(m001), mu(m101)),
            1.0e-7),
        "xz shear modulus must use the four-point harmonic mean");
    expect(
        nearly_equal(
            coefficients.shear_modulus_yz_pa[center],
            harmonic_four(mu(m000), mu(m010), mu(m001), mu(m011)),
            1.0e-7),
        "yz shear modulus must use the four-point harmonic mean");

    const auto lower_halo = model.grid.linear_index(0, oy, oz);
    expect(
        nearly_equal(
            coefficients.lambda_pa[lower_halo],
            center_moduli.lambda_pa,
            1.0e-7) &&
            nearly_equal(
                coefficients.buoyancy_x_m3_kg[lower_halo],
                1.0 / 2000.0,
                1.0e-7) &&
            nearly_equal(
                coefficients.shear_modulus_xy_pa[lower_halo],
                harmonic_four(mu(m000), mu(m000), mu(m010), mu(m010)),
                1.0e-7),
        "lower halo must extend edge material constantly before averaging");

    const auto upper_corner = model.grid.linear_index(
        model.grid.allocated_nx() - 1,
        model.grid.allocated_ny() - 1,
        model.grid.allocated_nz() - 1);
    const auto upper_moduli = wave3d::elastic_moduli(m111);
    expect(
        nearly_equal(
            coefficients.bulk_modulus_pa[upper_corner],
            upper_moduli.bulk_modulus_pa,
            1.0e-7) &&
            nearly_equal(
                coefficients.buoyancy_z_m3_kg[upper_corner],
                1.0 / 2550.0,
                1.0e-7) &&
            nearly_equal(
                coefficients.shear_modulus_yz_pa[upper_corner],
                mu(m111),
                1.0e-7),
        "upper halo corner must preserve constant edge extension");
}

void test_float32_overflow_rejection() {
    const wave3d::Grid3D grid{
        1, 1, 1,
        10.0F, 10.0F, 10.0F,
        1,
        {0, 0}, {0, 0}, {0, 0}};
    const auto maximum = std::numeric_limits<float>::max();
    const auto model = wave3d::make_homogeneous_model(
        grid, {maximum, 1.0F, maximum});
    bool threw = false;
    try {
        static_cast<void>(wave3d::prepare_elastic_coefficients(model));
    } catch (const std::overflow_error&) {
        threw = true;
    }
    expect(threw, "coefficient values outside float32 must be rejected");

    const auto minimum = std::numeric_limits<float>::min();
    const auto underflow_model = wave3d::make_homogeneous_model(
        grid, {2.0F * minimum, minimum, minimum});
    threw = false;
    try {
        static_cast<void>(wave3d::prepare_elastic_coefficients(underflow_model));
    } catch (const std::overflow_error&) {
        threw = true;
    }
    expect(threw, "positive coefficients that underflow float32 must be rejected");
}

} // namespace

int main() {
    test_modulus_conversion_and_negative_lambda();
    test_staggered_averages_and_halo_extension();
    test_float32_overflow_rejection();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Wave3D elastic-coefficient tests passed\n";
    return 0;
}
