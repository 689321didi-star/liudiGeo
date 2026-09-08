#pragma once

#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/wave/elastic_wavefield.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace wave3d {

struct ElasticEnergy {
    double kinetic_j{0.0};
    double strain_j{0.0};
    double total_j{0.0};
};

[[nodiscard]] inline ElasticEnergy elastic_energy(
    const ElasticWavefield& wavefield,
    const ElasticCoefficients& coefficients) {
    require_valid_elastic_wavefield_layout(wavefield);
    require_valid_elastic_coefficient_layout(coefficients);
    if (!same_grid_geometry(wavefield.grid, coefficients.grid)) {
        throw std::invalid_argument(
            "energy diagnostic wavefield and coefficient grids must match");
    }

    double kinetic_density_sum = 0.0;
    double strain_density_sum = 0.0;
    for (std::size_t index = 0; index < wavefield.cell_count(); ++index) {
        const double vx = static_cast<double>(wavefield.vx_m_s[index]);
        const double vy = static_cast<double>(wavefield.vy_m_s[index]);
        const double vz = static_cast<double>(wavefield.vz_m_s[index]);
        const double sxx = static_cast<double>(wavefield.sxx_pa[index]);
        const double syy = static_cast<double>(wavefield.syy_pa[index]);
        const double szz = static_cast<double>(wavefield.szz_pa[index]);
        const double sxy = static_cast<double>(wavefield.sxy_pa[index]);
        const double sxz = static_cast<double>(wavefield.sxz_pa[index]);
        const double syz = static_cast<double>(wavefield.syz_pa[index]);
        const double buoyancy_x =
            static_cast<double>(coefficients.buoyancy_x_m3_kg[index]);
        const double buoyancy_y =
            static_cast<double>(coefficients.buoyancy_y_m3_kg[index]);
        const double buoyancy_z =
            static_cast<double>(coefficients.buoyancy_z_m3_kg[index]);
        const double mu =
            static_cast<double>(coefficients.shear_modulus_pa[index]);
        const double mu_xy =
            static_cast<double>(coefficients.shear_modulus_xy_pa[index]);
        const double mu_xz =
            static_cast<double>(coefficients.shear_modulus_xz_pa[index]);
        const double mu_yz =
            static_cast<double>(coefficients.shear_modulus_yz_pa[index]);
        const double bulk =
            static_cast<double>(coefficients.bulk_modulus_pa[index]);

        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz) ||
            !std::isfinite(sxx) || !std::isfinite(syy) ||
            !std::isfinite(szz) || !std::isfinite(sxy) ||
            !std::isfinite(sxz) || !std::isfinite(syz)) {
            throw std::invalid_argument(
                "energy diagnostic requires finite wavefields");
        }
        if (!std::isfinite(buoyancy_x) || !(buoyancy_x > 0.0) ||
            !std::isfinite(buoyancy_y) || !(buoyancy_y > 0.0) ||
            !std::isfinite(buoyancy_z) || !(buoyancy_z > 0.0) ||
            !std::isfinite(mu) || !(mu > 0.0) ||
            !std::isfinite(mu_xy) || !(mu_xy > 0.0) ||
            !std::isfinite(mu_xz) || !(mu_xz > 0.0) ||
            !std::isfinite(mu_yz) || !(mu_yz > 0.0) ||
            !std::isfinite(bulk) || !(bulk > 0.0)) {
            throw std::invalid_argument(
                "energy diagnostic requires positive finite coefficients");
        }

        kinetic_density_sum +=
            0.5 * (vx * vx / buoyancy_x + vy * vy / buoyancy_y +
                   vz * vz / buoyancy_z);

        const double trace = sxx + syy + szz;
        const double mean = trace / 3.0;
        const double dev_xx = sxx - mean;
        const double dev_yy = syy - mean;
        const double dev_zz = szz - mean;
        strain_density_sum +=
            (dev_xx * dev_xx + dev_yy * dev_yy + dev_zz * dev_zz) /
                (4.0 * mu) +
            trace * trace / (18.0 * bulk) +
            sxy * sxy / (2.0 * mu_xy) +
            sxz * sxz / (2.0 * mu_xz) +
            syz * syz / (2.0 * mu_yz);
    }

    const double cell_volume_m3 =
        static_cast<double>(wavefield.grid.dx_m) *
        static_cast<double>(wavefield.grid.dy_m) *
        static_cast<double>(wavefield.grid.dz_m);
    const double kinetic_j = kinetic_density_sum * cell_volume_m3;
    const double strain_j = strain_density_sum * cell_volume_m3;
    const double total_j = kinetic_j + strain_j;
    if (!std::isfinite(kinetic_j) || !std::isfinite(strain_j) ||
        !std::isfinite(total_j)) {
        throw std::overflow_error("elastic energy is not finite");
    }
    return {kinetic_j, strain_j, total_j};
}

} // namespace wave3d
