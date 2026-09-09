#include "wave3d/boundary/sponge.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
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

[[nodiscard]] wave3d::Grid3D test_grid() {
    return {
        3, 3, 3,
        10.0F, 10.0F, 10.0F,
        6,
        {4, 4}, {4, 4}, {4, 4}};
}

[[nodiscard]] float factor_at(
    const wave3d::SpongeProfile& profile,
    std::size_t x,
    std::size_t y,
    std::size_t z) {
    return profile.damping[profile.grid.linear_index(x, y, z)];
}

void test_profile_geometry() {
    constexpr double outer = 0.75;
    const auto grid = test_grid();
    const auto profile = wave3d::prepare_sponge_profile(grid, outer);
    const auto ox = grid.physical_origin_x();
    const auto oy = grid.physical_origin_y();
    const auto oz = grid.physical_origin_z();

    for (std::size_t z = 0; z < grid.nz; ++z) {
        for (std::size_t y = 0; y < grid.ny; ++y) {
            for (std::size_t x = 0; x < grid.nx; ++x) {
                expect(
                    factor_at(profile, ox + x, oy + y, oz + z) == 1.0F,
                    "physical sponge coefficient must be exactly one");
            }
        }
    }

    const auto center_y = oy + 1;
    const auto center_z = oz + 1;
    const float face = factor_at(profile, ox - 4, center_y, center_z);
    const float edge = factor_at(profile, ox - 4, oy - 4, center_z);
    const float corner = factor_at(profile, ox - 4, oy - 4, oz - 4);
    expect(std::abs(face - outer) < 1.0e-7, "outer face factor changed");
    expect(std::abs(edge - outer * outer) < 1.0e-7, "edge factor must multiply");
    expect(
        std::abs(corner - outer * outer * outer) < 1.0e-7,
        "corner factor must multiply all axes");
    expect(
        factor_at(profile, 0, center_y, center_z) == face,
        "halo beyond a sponge side must retain its outer factor");

    float previous = 1.0F;
    for (std::size_t depth = 1; depth <= 4; ++depth) {
        const float current =
            factor_at(profile, ox - depth, center_y, center_z);
        expect(current < previous, "lower-side sponge must be monotonic");
        previous = current;
    }
    previous = 1.0F;
    const auto physical_last_x = ox + grid.nx - 1;
    for (std::size_t depth = 1; depth <= 4; ++depth) {
        const float current =
            factor_at(profile, physical_last_x + depth, center_y, center_z);
        expect(current < previous, "upper-side sponge must be monotonic");
        previous = current;
    }
    expect(
        profile.bytes() == grid.allocated_cell_count() * sizeof(float),
        "sponge owned bytes changed");
}

void fill_fields(wave3d::ElasticWavefield& wavefield) {
    float value = 1.0F;
    for (auto* field : {
             &wavefield.vx_m_s, &wavefield.vy_m_s, &wavefield.vz_m_s,
             &wavefield.sxx_pa, &wavefield.syy_pa, &wavefield.szz_pa,
             &wavefield.sxy_pa, &wavefield.sxz_pa, &wavefield.syz_pa}) {
        std::fill(field->begin(), field->end(), value);
        value += 1.0F;
    }
}

void test_separate_application_and_noop() {
    const auto grid = test_grid();
    const auto profile = wave3d::prepare_sponge_profile(grid, 0.75);
    wave3d::ElasticWavefield wavefield(grid);
    fill_fields(wavefield);
    const auto index = grid.linear_index(
        grid.physical_origin_x() - 4,
        grid.physical_origin_y() + 1,
        grid.physical_origin_z() + 1);
    const auto factor = profile.damping[index];

    wave3d::apply_sponge_to_stresses(wavefield, profile);
    expect(wavefield.vx_m_s[index] == 1.0F, "stress hook must not damp velocity");
    expect(
        wavefield.sxx_pa[index] == 4.0F * factor &&
            wavefield.syz_pa[index] == 9.0F * factor,
        "stress hook must damp all six stresses");
    wave3d::apply_sponge_to_velocities(wavefield, profile);
    expect(
        wavefield.vx_m_s[index] == factor &&
            wavefield.vz_m_s[index] == 3.0F * factor,
        "velocity hook must damp all three velocities");

    wave3d::ElasticWavefield unchanged(grid);
    fill_fields(unchanged);
    const auto no_op = wave3d::prepare_sponge_profile(grid, 1.0);
    wave3d::apply_sponge_to_stresses(unchanged, no_op);
    wave3d::apply_sponge_to_velocities(unchanged, no_op);
    expect(
        unchanged.vx_m_s[index] == 1.0F &&
            unchanged.syz_pa[index] == 9.0F,
        "outer damping one must be an exact no-op");
}

void test_rejection() {
    const auto grid = test_grid();
    expect_throws<std::invalid_argument>(
        [&] { static_cast<void>(wave3d::prepare_sponge_profile(grid, 0.0)); },
        "zero outer damping must fail");
    expect_throws<std::invalid_argument>(
        [&] { static_cast<void>(wave3d::prepare_sponge_profile(grid, 1.01)); },
        "outer damping above one must fail");
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(wave3d::prepare_sponge_profile(
                grid, std::numeric_limits<double>::quiet_NaN()));
        },
        "non-finite outer damping must fail");

    auto malformed = wave3d::prepare_sponge_profile(grid, 0.75);
    malformed.damping.pop_back();
    wave3d::ElasticWavefield wavefield(grid);
    expect_throws<std::invalid_argument>(
        [&] { wave3d::apply_sponge_to_stresses(wavefield, malformed); },
        "malformed sponge storage must fail");

    auto non_finite = wave3d::prepare_sponge_profile(grid, 0.75);
    wavefield.vx_m_s[0] = std::numeric_limits<float>::infinity();
    expect_throws<std::invalid_argument>(
        [&] { wave3d::apply_sponge_to_velocities(wavefield, non_finite); },
        "non-finite wavefield must fail before damping");
}

} // namespace

int main() {
    test_profile_geometry();
    test_separate_application_and_noop();
    test_rejection();
    if (failures != 0) {
        std::cerr << failures << " sponge test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D sponge tests passed\n";
    return 0;
}
