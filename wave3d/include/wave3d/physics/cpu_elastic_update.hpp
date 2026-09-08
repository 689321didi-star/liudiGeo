#pragma once

#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/numerics/cpu_staggered_derivative.hpp"
#include "wave3d/wave/elastic_wavefield.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

namespace wave3d {

namespace detail {

inline void require_cpu_elastic_update_inputs(
    const ElasticWavefield& wavefield,
    const ElasticCoefficients& coefficients,
    double dt_s) {
    require_valid_elastic_wavefield_layout(wavefield);
    require_valid_elastic_coefficient_layout(coefficients);
    if (!same_grid_geometry(wavefield.grid, coefficients.grid)) {
        throw std::invalid_argument(
            "wavefield and elastic coefficients use different grids");
    }
    if (!std::isfinite(dt_s) || !(dt_s > 0.0)) {
        throw std::invalid_argument("elastic update time step must be positive");
    }
}

[[nodiscard]] inline float checked_wavefield_update(
    float current,
    double increment,
    const char* component) {
    const auto updated = static_cast<double>(current) + increment;
    constexpr auto maximum =
        static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(updated) || updated > maximum || updated < -maximum) {
        throw std::overflow_error(
            std::string(component) + " elastic update exceeds float32");
    }
    return static_cast<float>(updated);
}

} // namespace detail

inline void cpu_update_elastic_stresses(
    ElasticWavefield& wavefield,
    const ElasticCoefficients& coefficients,
    double dt_s) {
    detail::require_cpu_elastic_update_inputs(wavefield, coefficients, dt_s);
    const auto& grid = wavefield.grid;
    constexpr auto integer_to_half =
        StaggeredDerivativeMapping::IntegerToHalf;
    constexpr auto half_to_integer =
        StaggeredDerivativeMapping::HalfToInteger;

    const auto normal_x = complete_stencil_target_range(
        grid, DerivativeAxis::X, half_to_integer);
    const auto normal_y = complete_stencil_target_range(
        grid, DerivativeAxis::Y, half_to_integer);
    const auto normal_z = complete_stencil_target_range(
        grid, DerivativeAxis::Z, half_to_integer);
    for (std::size_t z = normal_z.begin; z < normal_z.end; ++z) {
        for (std::size_t y = normal_y.begin; y < normal_y.end; ++y) {
            for (std::size_t x = normal_x.begin; x < normal_x.end; ++x) {
                const StorageIndex3D target{x, y, z};
                const auto index = grid.linear_index(x, y, z);
                const auto dvx_dx = cpu_staggered_derivative_at(
                    wavefield.vx_m_s,
                    grid,
                    target,
                    DerivativeAxis::X,
                    half_to_integer);
                const auto dvy_dy = cpu_staggered_derivative_at(
                    wavefield.vy_m_s,
                    grid,
                    target,
                    DerivativeAxis::Y,
                    half_to_integer);
                const auto dvz_dz = cpu_staggered_derivative_at(
                    wavefield.vz_m_s,
                    grid,
                    target,
                    DerivativeAxis::Z,
                    half_to_integer);
                const auto lambda =
                    static_cast<double>(coefficients.lambda_pa[index]);
                const auto mu = static_cast<double>(
                    coefficients.shear_modulus_pa[index]);

                const auto next_sxx = detail::checked_wavefield_update(
                    wavefield.sxx_pa[index],
                    dt_s * ((lambda + 2.0 * mu) * dvx_dx +
                            lambda * (dvy_dy + dvz_dz)),
                    "sxx");
                const auto next_syy = detail::checked_wavefield_update(
                    wavefield.syy_pa[index],
                    dt_s * ((lambda + 2.0 * mu) * dvy_dy +
                            lambda * (dvx_dx + dvz_dz)),
                    "syy");
                const auto next_szz = detail::checked_wavefield_update(
                    wavefield.szz_pa[index],
                    dt_s * ((lambda + 2.0 * mu) * dvz_dz +
                            lambda * (dvx_dx + dvy_dy)),
                    "szz");
                wavefield.sxx_pa[index] = next_sxx;
                wavefield.syy_pa[index] = next_syy;
                wavefield.szz_pa[index] = next_szz;
            }
        }
    }

    const auto shear_x = complete_stencil_target_range(
        grid, DerivativeAxis::X, integer_to_half);
    const auto shear_y = complete_stencil_target_range(
        grid, DerivativeAxis::Y, integer_to_half);
    for (std::size_t z = 0; z < grid.allocated_nz(); ++z) {
        for (std::size_t y = shear_y.begin; y < shear_y.end; ++y) {
            for (std::size_t x = shear_x.begin; x < shear_x.end; ++x) {
                const StorageIndex3D target{x, y, z};
                const auto index = grid.linear_index(x, y, z);
                const auto dvx_dy = cpu_staggered_derivative_at(
                    wavefield.vx_m_s,
                    grid,
                    target,
                    DerivativeAxis::Y,
                    integer_to_half);
                const auto dvy_dx = cpu_staggered_derivative_at(
                    wavefield.vy_m_s,
                    grid,
                    target,
                    DerivativeAxis::X,
                    integer_to_half);
                wavefield.sxy_pa[index] = detail::checked_wavefield_update(
                    wavefield.sxy_pa[index],
                    dt_s *
                        static_cast<double>(
                            coefficients.shear_modulus_xy_pa[index]) *
                        (dvx_dy + dvy_dx),
                    "sxy");
            }
        }
    }

    const auto shear_z = complete_stencil_target_range(
        grid, DerivativeAxis::Z, integer_to_half);
    for (std::size_t z = shear_z.begin; z < shear_z.end; ++z) {
        for (std::size_t y = 0; y < grid.allocated_ny(); ++y) {
            for (std::size_t x = shear_x.begin; x < shear_x.end; ++x) {
                const StorageIndex3D target{x, y, z};
                const auto index = grid.linear_index(x, y, z);
                const auto dvx_dz = cpu_staggered_derivative_at(
                    wavefield.vx_m_s,
                    grid,
                    target,
                    DerivativeAxis::Z,
                    integer_to_half);
                const auto dvz_dx = cpu_staggered_derivative_at(
                    wavefield.vz_m_s,
                    grid,
                    target,
                    DerivativeAxis::X,
                    integer_to_half);
                wavefield.sxz_pa[index] = detail::checked_wavefield_update(
                    wavefield.sxz_pa[index],
                    dt_s *
                        static_cast<double>(
                            coefficients.shear_modulus_xz_pa[index]) *
                        (dvx_dz + dvz_dx),
                    "sxz");
            }
        }
    }

    for (std::size_t z = shear_z.begin; z < shear_z.end; ++z) {
        for (std::size_t y = shear_y.begin; y < shear_y.end; ++y) {
            for (std::size_t x = 0; x < grid.allocated_nx(); ++x) {
                const StorageIndex3D target{x, y, z};
                const auto index = grid.linear_index(x, y, z);
                const auto dvy_dz = cpu_staggered_derivative_at(
                    wavefield.vy_m_s,
                    grid,
                    target,
                    DerivativeAxis::Z,
                    integer_to_half);
                const auto dvz_dy = cpu_staggered_derivative_at(
                    wavefield.vz_m_s,
                    grid,
                    target,
                    DerivativeAxis::Y,
                    integer_to_half);
                wavefield.syz_pa[index] = detail::checked_wavefield_update(
                    wavefield.syz_pa[index],
                    dt_s *
                        static_cast<double>(
                            coefficients.shear_modulus_yz_pa[index]) *
                        (dvy_dz + dvz_dy),
                    "syz");
            }
        }
    }
}

inline void cpu_update_elastic_velocities(
    ElasticWavefield& wavefield,
    const ElasticCoefficients& coefficients,
    double dt_s) {
    detail::require_cpu_elastic_update_inputs(wavefield, coefficients, dt_s);
    const auto& grid = wavefield.grid;
    constexpr auto integer_to_half =
        StaggeredDerivativeMapping::IntegerToHalf;
    constexpr auto half_to_integer =
        StaggeredDerivativeMapping::HalfToInteger;

    const auto integer_to_half_x = complete_stencil_target_range(
        grid, DerivativeAxis::X, integer_to_half);
    const auto integer_to_half_y = complete_stencil_target_range(
        grid, DerivativeAxis::Y, integer_to_half);
    const auto integer_to_half_z = complete_stencil_target_range(
        grid, DerivativeAxis::Z, integer_to_half);
    const auto half_to_integer_x = complete_stencil_target_range(
        grid, DerivativeAxis::X, half_to_integer);
    const auto half_to_integer_y = complete_stencil_target_range(
        grid, DerivativeAxis::Y, half_to_integer);
    const auto half_to_integer_z = complete_stencil_target_range(
        grid, DerivativeAxis::Z, half_to_integer);

    for (std::size_t z = half_to_integer_z.begin;
         z < half_to_integer_z.end;
         ++z) {
        for (std::size_t y = half_to_integer_y.begin;
             y < half_to_integer_y.end;
             ++y) {
            for (std::size_t x = integer_to_half_x.begin;
                 x < integer_to_half_x.end;
                 ++x) {
                const StorageIndex3D target{x, y, z};
                const auto index = grid.linear_index(x, y, z);
                const auto dsxx_dx = cpu_staggered_derivative_at(
                    wavefield.sxx_pa,
                    grid,
                    target,
                    DerivativeAxis::X,
                    integer_to_half);
                const auto dsxy_dy = cpu_staggered_derivative_at(
                    wavefield.sxy_pa,
                    grid,
                    target,
                    DerivativeAxis::Y,
                    half_to_integer);
                const auto dsxz_dz = cpu_staggered_derivative_at(
                    wavefield.sxz_pa,
                    grid,
                    target,
                    DerivativeAxis::Z,
                    half_to_integer);
                wavefield.vx_m_s[index] = detail::checked_wavefield_update(
                    wavefield.vx_m_s[index],
                    dt_s *
                        static_cast<double>(
                            coefficients.buoyancy_x_m3_kg[index]) *
                        (dsxx_dx + dsxy_dy + dsxz_dz),
                    "vx");
            }
        }
    }

    for (std::size_t z = half_to_integer_z.begin;
         z < half_to_integer_z.end;
         ++z) {
        for (std::size_t y = integer_to_half_y.begin;
             y < integer_to_half_y.end;
             ++y) {
            for (std::size_t x = half_to_integer_x.begin;
                 x < half_to_integer_x.end;
                 ++x) {
                const StorageIndex3D target{x, y, z};
                const auto index = grid.linear_index(x, y, z);
                const auto dsxy_dx = cpu_staggered_derivative_at(
                    wavefield.sxy_pa,
                    grid,
                    target,
                    DerivativeAxis::X,
                    half_to_integer);
                const auto dsyy_dy = cpu_staggered_derivative_at(
                    wavefield.syy_pa,
                    grid,
                    target,
                    DerivativeAxis::Y,
                    integer_to_half);
                const auto dsyz_dz = cpu_staggered_derivative_at(
                    wavefield.syz_pa,
                    grid,
                    target,
                    DerivativeAxis::Z,
                    half_to_integer);
                wavefield.vy_m_s[index] = detail::checked_wavefield_update(
                    wavefield.vy_m_s[index],
                    dt_s *
                        static_cast<double>(
                            coefficients.buoyancy_y_m3_kg[index]) *
                        (dsxy_dx + dsyy_dy + dsyz_dz),
                    "vy");
            }
        }
    }

    for (std::size_t z = integer_to_half_z.begin;
         z < integer_to_half_z.end;
         ++z) {
        for (std::size_t y = half_to_integer_y.begin;
             y < half_to_integer_y.end;
             ++y) {
            for (std::size_t x = half_to_integer_x.begin;
                 x < half_to_integer_x.end;
                 ++x) {
                const StorageIndex3D target{x, y, z};
                const auto index = grid.linear_index(x, y, z);
                const auto dsxz_dx = cpu_staggered_derivative_at(
                    wavefield.sxz_pa,
                    grid,
                    target,
                    DerivativeAxis::X,
                    half_to_integer);
                const auto dsyz_dy = cpu_staggered_derivative_at(
                    wavefield.syz_pa,
                    grid,
                    target,
                    DerivativeAxis::Y,
                    half_to_integer);
                const auto dszz_dz = cpu_staggered_derivative_at(
                    wavefield.szz_pa,
                    grid,
                    target,
                    DerivativeAxis::Z,
                    integer_to_half);
                wavefield.vz_m_s[index] = detail::checked_wavefield_update(
                    wavefield.vz_m_s[index],
                    dt_s *
                        static_cast<double>(
                            coefficients.buoyancy_z_m3_kg[index]) *
                        (dsxz_dx + dsyz_dy + dszz_dz),
                    "vz");
            }
        }
    }
}

} // namespace wave3d
