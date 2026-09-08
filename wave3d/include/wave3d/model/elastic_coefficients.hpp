#pragma once

#include "wave3d/core/coordinates.hpp"
#include "wave3d/model/physical_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace wave3d {

struct ElasticModuli {
    double lambda_pa{0.0};
    double shear_modulus_pa{0.0};
    double bulk_modulus_pa{0.0};
};

struct ElasticCoefficients {
    Grid3D grid{};
    std::vector<float> lambda_pa;
    std::vector<float> shear_modulus_pa;
    std::vector<float> bulk_modulus_pa;
    std::vector<float> buoyancy_x_m3_kg;
    std::vector<float> buoyancy_y_m3_kg;
    std::vector<float> buoyancy_z_m3_kg;
    std::vector<float> shear_modulus_xy_pa;
    std::vector<float> shear_modulus_xz_pa;
    std::vector<float> shear_modulus_yz_pa;

    [[nodiscard]] std::size_t cell_count() const noexcept {
        return lambda_pa.size();
    }
};

[[nodiscard]] inline ElasticModuli elastic_moduli(
    const ElasticMaterial& material) {
    require_valid_elastic_material(material);
    const auto density = static_cast<double>(material.density_kg_m3);
    const auto vp = static_cast<double>(material.vp_m_s);
    const auto vs = static_cast<double>(material.vs_m_s);
    const auto shear_modulus = density * vs * vs;
    const auto bulk_modulus = density * (vp * vp - (4.0 / 3.0) * vs * vs);
    const auto lambda = density * (vp * vp - 2.0 * vs * vs);
    if (!std::isfinite(lambda) || !std::isfinite(shear_modulus) ||
        !std::isfinite(bulk_modulus) || !(shear_modulus > 0.0) ||
        !(bulk_modulus > 0.0)) {
        throw std::overflow_error("elastic modulus conversion is not finite");
    }
    return {lambda, shear_modulus, bulk_modulus};
}

namespace detail {

[[nodiscard]] inline float checked_coefficient_float(
    double value,
    const char* name) {
    constexpr auto maximum = static_cast<double>(
        std::numeric_limits<float>::max());
    if (!std::isfinite(value) || value > maximum || value < -maximum) {
        throw std::overflow_error(
            std::string(name) + " cannot be represented as float32");
    }
    const auto converted = static_cast<float>(value);
    if (!std::isfinite(converted) || (value != 0.0 && converted == 0.0F)) {
        throw std::overflow_error(
            std::string(name) + " cannot be represented as float32");
    }
    return converted;
}

[[nodiscard]] inline std::size_t clamped_physical_axis_index(
    std::size_t storage_index,
    std::size_t physical_origin,
    std::size_t physical_count) noexcept {
    if (storage_index <= physical_origin) {
        return 0;
    }
    return std::min(storage_index - physical_origin, physical_count - 1);
}

[[nodiscard]] inline std::size_t next_storage_index(
    std::size_t index,
    std::size_t count) noexcept {
    return index == count - 1 ? index : index + 1;
}

[[nodiscard]] inline double harmonic_mean_four(
    double first,
    double second,
    double third,
    double fourth) noexcept {
    return 4.0 / (1.0 / first + 1.0 / second + 1.0 / third + 1.0 / fourth);
}

} // namespace detail

[[nodiscard]] inline ElasticCoefficients prepare_elastic_coefficients(
    const PhysicalModel& model) {
    require_valid_physical_model(model);
    const auto& grid = model.grid;
    const auto ax = grid.allocated_nx();
    const auto ay = grid.allocated_ny();
    const auto az = grid.allocated_nz();
    const auto cells = grid.allocated_cell_count();

    ElasticCoefficients coefficients{
        grid,
        std::vector<float>(cells),
        std::vector<float>(cells),
        std::vector<float>(cells),
        std::vector<float>(cells),
        std::vector<float>(cells),
        std::vector<float>(cells),
        std::vector<float>(cells),
        std::vector<float>(cells),
        std::vector<float>(cells)};

    std::vector<std::size_t> physical_x(ax);
    std::vector<std::size_t> physical_y(ay);
    std::vector<std::size_t> physical_z(az);
    for (std::size_t x = 0; x < ax; ++x) {
        physical_x[x] = detail::clamped_physical_axis_index(
            x, grid.physical_origin_x(), grid.nx);
    }
    for (std::size_t y = 0; y < ay; ++y) {
        physical_y[y] = detail::clamped_physical_axis_index(
            y, grid.physical_origin_y(), grid.ny);
    }
    for (std::size_t z = 0; z < az; ++z) {
        physical_z[z] = detail::clamped_physical_axis_index(
            z, grid.physical_origin_z(), grid.nz);
    }

    const auto material_at = [&model, &grid](
                                 std::size_t x,
                                 std::size_t y,
                                 std::size_t z) {
        const auto index = grid.physical_linear_index(x, y, z);
        return ElasticMaterial{
            model.vp_m_s[index],
            model.vs_m_s[index],
            model.density_kg_m3[index]};
    };
    const auto mu_at = [&material_at](
                           std::size_t x,
                           std::size_t y,
                           std::size_t z) {
        return elastic_moduli(material_at(x, y, z)).shear_modulus_pa;
    };

    for (std::size_t z = 0; z < az; ++z) {
        const auto pz = physical_z[z];
        const auto pz_next = physical_z[detail::next_storage_index(z, az)];
        for (std::size_t y = 0; y < ay; ++y) {
            const auto py = physical_y[y];
            const auto py_next = physical_y[detail::next_storage_index(y, ay)];
            for (std::size_t x = 0; x < ax; ++x) {
                const auto px = physical_x[x];
                const auto px_next =
                    physical_x[detail::next_storage_index(x, ax)];
                const auto index = grid.linear_index(x, y, z);

                const auto center = material_at(px, py, pz);
                const auto moduli = elastic_moduli(center);
                coefficients.lambda_pa[index] =
                    detail::checked_coefficient_float(
                        moduli.lambda_pa, "lambda");
                coefficients.shear_modulus_pa[index] =
                    detail::checked_coefficient_float(
                        moduli.shear_modulus_pa, "shear modulus");
                coefficients.bulk_modulus_pa[index] =
                    detail::checked_coefficient_float(
                        moduli.bulk_modulus_pa, "bulk modulus");

                const auto density =
                    static_cast<double>(center.density_kg_m3);
                const auto density_x = static_cast<double>(
                    material_at(px_next, py, pz).density_kg_m3);
                const auto density_y = static_cast<double>(
                    material_at(px, py_next, pz).density_kg_m3);
                const auto density_z = static_cast<double>(
                    material_at(px, py, pz_next).density_kg_m3);
                coefficients.buoyancy_x_m3_kg[index] =
                    detail::checked_coefficient_float(
                        2.0 / (density + density_x), "x buoyancy");
                coefficients.buoyancy_y_m3_kg[index] =
                    detail::checked_coefficient_float(
                        2.0 / (density + density_y), "y buoyancy");
                coefficients.buoyancy_z_m3_kg[index] =
                    detail::checked_coefficient_float(
                        2.0 / (density + density_z), "z buoyancy");

                const auto mu_center = moduli.shear_modulus_pa;
                const auto mu_x = mu_at(px_next, py, pz);
                const auto mu_y = mu_at(px, py_next, pz);
                const auto mu_z = mu_at(px, py, pz_next);
                coefficients.shear_modulus_xy_pa[index] =
                    detail::checked_coefficient_float(
                        detail::harmonic_mean_four(
                            mu_center,
                            mu_x,
                            mu_y,
                            mu_at(px_next, py_next, pz)),
                        "xy shear modulus");
                coefficients.shear_modulus_xz_pa[index] =
                    detail::checked_coefficient_float(
                        detail::harmonic_mean_four(
                            mu_center,
                            mu_x,
                            mu_z,
                            mu_at(px_next, py, pz_next)),
                        "xz shear modulus");
                coefficients.shear_modulus_yz_pa[index] =
                    detail::checked_coefficient_float(
                        detail::harmonic_mean_four(
                            mu_center,
                            mu_y,
                            mu_z,
                            mu_at(px, py_next, pz_next)),
                        "yz shear modulus");
            }
        }
    }
    return coefficients;
}

} // namespace wave3d
