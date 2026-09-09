#include "wave3d/boundary/free_surface.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Exception, typename Function>
void expect_throws(Function&& function, const std::string& message) {
    bool threw = false;
    try {
        function();
    } catch (const Exception&) {
        threw = true;
    }
    expect(threw, message);
}

[[nodiscard]] wave3d::Grid3D grid() {
    return {
        5, 5, 5,
        10.0F, 10.0F, 10.0F,
        6,
        {3, 3}, {3, 3}, {0, 3}};
}

void fill(wave3d::ElasticWavefield& wavefield) {
    float component = 1.0F;
    for (auto* field : {
             &wavefield.vx_m_s, &wavefield.vy_m_s, &wavefield.vz_m_s,
             &wavefield.sxx_pa, &wavefield.syy_pa, &wavefield.szz_pa,
             &wavefield.sxy_pa, &wavefield.sxz_pa, &wavefield.syz_pa}) {
        for (std::size_t index = 0; index < field->size(); ++index) {
            (*field)[index] = component +
                              0.001F * static_cast<float>(index);
        }
        component += 1.0F;
    }
}

void test_stress_and_velocity_ghosts() {
    const auto test_grid = grid();
    const auto surface = wave3d::prepare_traction_free_surface(test_grid);
    wave3d::ElasticWavefield wavefield(test_grid);
    fill(wavefield);
    const auto k0 = surface.surface_storage_z;
    const auto below = test_grid.linear_index(2, 3, k0 + 1);
    const float original_below_sxx = wavefield.sxx_pa[below];
    const float original_below_vx = wavefield.vx_m_s[below];
    wave3d::apply_traction_free_stresses(wavefield, surface);

    for (std::size_t y = 0; y < test_grid.allocated_ny(); ++y) {
        for (std::size_t x = 0; x < test_grid.allocated_nx(); ++x) {
            expect(
                wavefield.szz_pa[test_grid.linear_index(x, y, k0)] == 0.0F,
                "surface normal traction must be exactly zero");
            for (std::size_t depth = 1; depth <= 6; ++depth) {
                const auto upper = test_grid.linear_index(x, y, k0 - depth);
                const auto lower = test_grid.linear_index(x, y, k0 + depth);
                expect(
                    wavefield.szz_pa[upper] == -wavefield.szz_pa[lower],
                    "szz ghosts must be odd");
                expect(
                    wavefield.sxx_pa[upper] == wavefield.sxx_pa[lower] &&
                        wavefield.syy_pa[upper] == wavefield.syy_pa[lower] &&
                        wavefield.sxy_pa[upper] == wavefield.sxy_pa[lower],
                    "tangential stress ghosts must be even");
            }
            for (std::size_t depth = 0; depth < 6; ++depth) {
                const auto upper =
                    test_grid.linear_index(x, y, k0 - 1 - depth);
                const auto lower = test_grid.linear_index(x, y, k0 + depth);
                expect(
                    wavefield.sxz_pa[upper] == -wavefield.sxz_pa[lower] &&
                        wavefield.syz_pa[upper] == -wavefield.syz_pa[lower],
                    "half-z shear ghosts must be odd");
            }
            const auto upper_half = test_grid.linear_index(x, y, k0 - 1);
            const auto lower_half = test_grid.linear_index(x, y, k0);
            expect(
                0.5F * (wavefield.sxz_pa[upper_half] +
                         wavefield.sxz_pa[lower_half]) == 0.0F &&
                    0.5F * (wavefield.syz_pa[upper_half] +
                             wavefield.syz_pa[lower_half]) == 0.0F,
                "interpolated surface shear traction must be exactly zero");
        }
    }
    expect(
        wavefield.sxx_pa[below] == original_below_sxx,
        "stress projection must not change subsurface storage");

    wave3d::apply_traction_free_velocities(wavefield, surface);
    for (std::size_t depth = 1; depth <= 6; ++depth) {
        const auto upper = test_grid.linear_index(2, 3, k0 - depth);
        const auto lower = test_grid.linear_index(2, 3, k0 + depth);
        expect(
            wavefield.vx_m_s[upper] == wavefield.vx_m_s[lower] &&
                wavefield.vy_m_s[upper] == wavefield.vy_m_s[lower],
            "horizontal velocity ghosts must be even");
    }
    for (std::size_t depth = 0; depth < 6; ++depth) {
        const auto upper = test_grid.linear_index(2, 3, k0 - 1 - depth);
        const auto lower = test_grid.linear_index(2, 3, k0 + depth);
        expect(
            wavefield.vz_m_s[upper] == wavefield.vz_m_s[lower],
            "vertical half-z velocity ghosts must be even");
    }
    expect(
        wavefield.vx_m_s[below] == original_below_vx,
        "velocity projection must not change subsurface storage");
}

void test_rejection() {
    auto invalid_grid = grid();
    invalid_grid.z_boundary.lower_absorbing = 1;
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                wave3d::prepare_traction_free_surface(invalid_grid));
        },
        "top absorbing cells must be rejected for a free surface");

    const auto valid_grid = grid();
    auto surface = wave3d::prepare_traction_free_surface(valid_grid);
    wave3d::ElasticWavefield wavefield(valid_grid);
    ++surface.surface_storage_z;
    expect_throws<std::invalid_argument>(
        [&] { wave3d::apply_traction_free_stresses(wavefield, surface); },
        "tampered free-surface metadata must fail");
}

} // namespace

int main() {
    test_stress_and_velocity_ghosts();
    test_rejection();
    if (failures != 0) {
        std::cerr << failures << " free-surface test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D free-surface tests passed\n";
    return 0;
}
