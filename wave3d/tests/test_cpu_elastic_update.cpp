#include "wave3d/physics/cpu_elastic_update.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool nearly_equal(double actual, double expected, double tolerance) {
    return std::abs(actual - expected) <=
           tolerance * std::max(1.0, std::abs(expected));
}

bool all_zero(const std::vector<float>& field) {
    return std::all_of(field.begin(), field.end(), [](float value) {
        return value == 0.0F;
    });
}

wave3d::Grid3D test_grid() {
    return {
        5, 5, 5,
        1.0F, 2.0F, 4.0F,
        6,
        {0, 0}, {0, 0}, {0, 0}};
}

wave3d::ElasticCoefficients test_coefficients() {
    const auto model = wave3d::make_homogeneous_model(
        test_grid(), {4.0F, 2.0F, 1000.0F});
    return wave3d::prepare_elastic_coefficients(model);
}

struct LatticeOffset {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct AffineField {
    double dx{0.0};
    double dy{0.0};
    double dz{0.0};
    double constant{0.0};
};

void fill_affine(
    std::vector<float>& field,
    const wave3d::Grid3D& grid,
    const LatticeOffset& offset,
    const AffineField& affine) {
    for (std::size_t z = 0; z < grid.allocated_nz(); ++z) {
        const auto z_m = (static_cast<double>(z) + offset.z) * grid.dz_m;
        for (std::size_t y = 0; y < grid.allocated_ny(); ++y) {
            const auto y_m = (static_cast<double>(y) + offset.y) * grid.dy_m;
            for (std::size_t x = 0; x < grid.allocated_nx(); ++x) {
                const auto x_m =
                    (static_cast<double>(x) + offset.x) * grid.dx_m;
                field[grid.linear_index(x, y, z)] = static_cast<float>(
                    affine.dx * x_m + affine.dy * y_m + affine.dz * z_m +
                    affine.constant);
            }
        }
    }
}

void test_wavefield_ownership_and_zero_state() {
    static_assert(
        !std::is_copy_constructible<wave3d::ElasticWavefield>::value,
        "wavefield ownership must not copy implicitly");
    static_assert(
        std::is_nothrow_move_constructible<wave3d::ElasticWavefield>::value,
        "wavefield ownership must move without throwing");

    wave3d::ElasticWavefield wavefield(test_grid());
    const auto cells = test_grid().allocated_cell_count();
    expect(wavefield.cell_count() == cells, "wavefield must use padded storage");
    expect(
        wavefield.vy_m_s.size() == cells && wavefield.vz_m_s.size() == cells &&
            wavefield.sxx_pa.size() == cells &&
            wavefield.syy_pa.size() == cells &&
            wavefield.szz_pa.size() == cells &&
            wavefield.sxy_pa.size() == cells &&
            wavefield.sxz_pa.size() == cells &&
            wavefield.syz_pa.size() == cells,
        "all nine wavefield components must share the padded layout");
    expect(
        all_zero(wavefield.vx_m_s) && all_zero(wavefield.vy_m_s) &&
            all_zero(wavefield.vz_m_s) && all_zero(wavefield.sxx_pa) &&
            all_zero(wavefield.syy_pa) && all_zero(wavefield.szz_pa) &&
            all_zero(wavefield.sxy_pa) && all_zero(wavefield.sxz_pa) &&
            all_zero(wavefield.syz_pa),
        "new elastic wavefields must represent v0=0 and sigma(-1/2)=0");

    wave3d::ElasticWavefield moved(std::move(wavefield));
    expect(moved.cell_count() == cells, "wavefield move must transfer ownership");
    wave3d::require_valid_elastic_wavefield_layout(moved);

    bool threw = false;
    try {
        wave3d::ElasticWavefield invalid(wave3d::Grid3D{});
        static_cast<void>(invalid);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "wavefield construction must reject an invalid grid");
}

void test_manufactured_stress_update() {
    const auto coefficients = test_coefficients();
    wave3d::ElasticWavefield wavefield(test_grid());

    fill_affine(
        wavefield.vx_m_s, wavefield.grid, {0.5, 0.0, 0.0}, {2, -3, 4, 1});
    fill_affine(
        wavefield.vy_m_s, wavefield.grid, {0.0, 0.5, 0.0}, {-5, 6, 7, -2});
    fill_affine(
        wavefield.vz_m_s, wavefield.grid, {0.0, 0.0, 0.5}, {8, -9, 10, 3});
    std::fill(wavefield.sxx_pa.begin(), wavefield.sxx_pa.end(), 100.0F);
    std::fill(wavefield.syy_pa.begin(), wavefield.syy_pa.end(), 200.0F);
    std::fill(wavefield.szz_pa.begin(), wavefield.szz_pa.end(), 300.0F);
    std::fill(wavefield.sxy_pa.begin(), wavefield.sxy_pa.end(), 400.0F);
    std::fill(wavefield.sxz_pa.begin(), wavefield.sxz_pa.end(), 500.0F);
    std::fill(wavefield.syz_pa.begin(), wavefield.syz_pa.end(), 600.0F);

    const wave3d::StorageIndex3D target{8, 8, 8};
    const auto index = wavefield.grid.linear_index(target.x, target.y, target.z);
    const auto original_vx = wavefield.vx_m_s[index];
    constexpr double dt_s = 0.125;
    wave3d::cpu_update_elastic_stresses(wavefield, coefficients, dt_s);

    const auto lambda = static_cast<double>(coefficients.lambda_pa[index]);
    const auto mu =
        static_cast<double>(coefficients.shear_modulus_pa[index]);
    const auto mu_xy =
        static_cast<double>(coefficients.shear_modulus_xy_pa[index]);
    const auto mu_xz =
        static_cast<double>(coefficients.shear_modulus_xz_pa[index]);
    const auto mu_yz =
        static_cast<double>(coefficients.shear_modulus_yz_pa[index]);

    expect(
        nearly_equal(
            wavefield.sxx_pa[index],
            100.0 + dt_s * ((lambda + 2.0 * mu) * 2.0 +
                            lambda * (6.0 + 10.0)),
            2.0e-6),
        "sxx must include dvx/dx, dvy/dy, and dvz/dz");
    expect(
        nearly_equal(
            wavefield.syy_pa[index],
            200.0 + dt_s * ((lambda + 2.0 * mu) * 6.0 +
                            lambda * (2.0 + 10.0)),
            2.0e-6),
        "syy must include its three accepted normal derivatives");
    expect(
        nearly_equal(
            wavefield.szz_pa[index],
            300.0 + dt_s * ((lambda + 2.0 * mu) * 10.0 +
                            lambda * (2.0 + 6.0)),
            2.0e-6),
        "szz must include its three accepted normal derivatives");
    expect(
        nearly_equal(
            wavefield.sxy_pa[index],
            400.0 + dt_s * mu_xy * (-3.0 - 5.0),
            2.0e-6),
        "sxy must include dvx/dy and dvy/dx");
    expect(
        nearly_equal(
            wavefield.sxz_pa[index],
            500.0 + dt_s * mu_xz * (4.0 + 8.0),
            2.0e-6),
        "sxz must include dvx/dz and dvz/dx");
    expect(
        nearly_equal(
            wavefield.syz_pa[index],
            600.0 + dt_s * mu_yz * (7.0 - 9.0),
            2.0e-6),
        "syz must include dvy/dz and dvz/dy");
    expect(
        wavefield.vx_m_s[index] == original_vx,
        "stress update must not advance velocity");

    const auto corner = wavefield.grid.linear_index(0, 0, 0);
    expect(
        wavefield.sxx_pa[corner] == 100.0F &&
            wavefield.sxy_pa[corner] == 400.0F &&
            wavefield.sxz_pa[corner] == 500.0F &&
            wavefield.syz_pa[corner] == 600.0F,
        "stress targets without complete stencils must remain unchanged");
}

void test_manufactured_velocity_update() {
    const auto coefficients = test_coefficients();
    wave3d::ElasticWavefield wavefield(test_grid());

    fill_affine(
        wavefield.sxx_pa, wavefield.grid, {0.0, 0.0, 0.0}, {11, 12, 13, 1});
    fill_affine(
        wavefield.syy_pa, wavefield.grid, {0.0, 0.0, 0.0}, {14, 15, 16, 2});
    fill_affine(
        wavefield.szz_pa, wavefield.grid, {0.0, 0.0, 0.0}, {17, 18, 19, 3});
    fill_affine(
        wavefield.sxy_pa, wavefield.grid, {0.5, 0.5, 0.0}, {20, 21, 22, 4});
    fill_affine(
        wavefield.sxz_pa, wavefield.grid, {0.5, 0.0, 0.5}, {23, 24, 25, 5});
    fill_affine(
        wavefield.syz_pa, wavefield.grid, {0.0, 0.5, 0.5}, {26, 27, 28, 6});

    const wave3d::StorageIndex3D target{8, 8, 8};
    const auto index = wavefield.grid.linear_index(target.x, target.y, target.z);
    const auto original_sxx = wavefield.sxx_pa[index];
    constexpr double dt_s = 0.125;
    wave3d::cpu_update_elastic_velocities(wavefield, coefficients, dt_s);

    expect(
        nearly_equal(
            wavefield.vx_m_s[index],
            dt_s * coefficients.buoyancy_x_m3_kg[index] *
                (11.0 + 21.0 + 25.0),
            2.0e-6),
        "vx must include dsxx/dx, dsxy/dy, and dsxz/dz");
    expect(
        nearly_equal(
            wavefield.vy_m_s[index],
            dt_s * coefficients.buoyancy_y_m3_kg[index] *
                (20.0 + 15.0 + 28.0),
            2.0e-6),
        "vy must include dsxy/dx, dsyy/dy, and dsyz/dz");
    expect(
        nearly_equal(
            wavefield.vz_m_s[index],
            dt_s * coefficients.buoyancy_z_m3_kg[index] *
                (23.0 + 27.0 + 19.0),
            2.0e-6),
        "vz must include dsxz/dx, dsyz/dy, and dszz/dz");
    expect(
        wavefield.sxx_pa[index] == original_sxx,
        "velocity update must not advance stress");

    const auto corner = wavefield.grid.linear_index(0, 0, 0);
    expect(
        wavefield.vx_m_s[corner] == 0.0F &&
            wavefield.vy_m_s[corner] == 0.0F &&
            wavefield.vz_m_s[corner] == 0.0F,
        "velocity targets without complete stencils must remain unchanged");
}

void test_layout_time_and_overflow_rejections() {
    auto coefficients = test_coefficients();
    wave3d::ElasticWavefield wavefield(test_grid());

    wavefield.vx_m_s.pop_back();
    bool threw = false;
    try {
        wave3d::cpu_update_elastic_stresses(wavefield, coefficients, 0.1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "a malformed wavefield layout must fail before updating");

    wave3d::ElasticWavefield valid_wavefield(test_grid());
    coefficients.bulk_modulus_pa.pop_back();
    threw = false;
    try {
        wave3d::cpu_update_elastic_velocities(
            valid_wavefield, coefficients, 0.1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "a malformed coefficient layout must fail before updating");

    coefficients = test_coefficients();
    auto different_grid = test_grid();
    different_grid.dx_m = 2.0F;
    wave3d::ElasticWavefield mismatched_wavefield(different_grid);
    threw = false;
    try {
        wave3d::cpu_update_elastic_stresses(
            mismatched_wavefield, coefficients, 0.1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "wavefield and coefficient grid mismatch must fail");

    threw = false;
    try {
        wave3d::cpu_update_elastic_stresses(valid_wavefield, coefficients, 0.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "a non-positive time step must fail");

    threw = false;
    try {
        wave3d::cpu_update_elastic_velocities(
            valid_wavefield,
            coefficients,
            std::numeric_limits<double>::infinity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "a non-finite time step must fail");

    fill_affine(
        valid_wavefield.vx_m_s,
        valid_wavefield.grid,
        {0.5, 0.0, 0.0},
        {1.0e36, 0.0, 0.0, 0.0});
    threw = false;
    try {
        wave3d::cpu_update_elastic_stresses(
            valid_wavefield, coefficients, 0.125);
    } catch (const std::overflow_error&) {
        threw = true;
    }
    expect(threw, "a stress update outside float32 must fail explicitly");
}

} // namespace

int main() {
    test_wavefield_ownership_and_zero_state();
    test_manufactured_stress_update();
    test_manufactured_velocity_update();
    test_layout_time_and_overflow_rejections();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Wave3D CPU elastic-update tests passed\n";
    return 0;
}
