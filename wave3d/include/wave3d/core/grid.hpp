#pragma once

#include "wave3d/core/checked_size.hpp"

#include <cstddef>
#include <stdexcept>

namespace wave3d {

struct AxisBoundary {
    std::size_t lower_absorbing{0};
    std::size_t upper_absorbing{0};
};

struct Grid3D {
    std::size_t nx{0};
    std::size_t ny{0};
    std::size_t nz{0};

    float dx_m{0.0F};
    float dy_m{0.0F};
    float dz_m{0.0F};

    std::size_t halo{0};
    AxisBoundary x_boundary{};
    AxisBoundary y_boundary{};
    AxisBoundary z_boundary{};

    [[nodiscard]] std::size_t allocated_nx() const {
        return checked_sum(nx, halo, x_boundary);
    }

    [[nodiscard]] std::size_t allocated_ny() const {
        return checked_sum(ny, halo, y_boundary);
    }

    [[nodiscard]] std::size_t allocated_nz() const {
        return checked_sum(nz, halo, z_boundary);
    }

    [[nodiscard]] std::size_t physical_cell_count() const {
        return detail::checked_size_product(
            detail::checked_size_product(
                nx, ny, "Wave3D physical grid size overflows size_t"),
            nz,
            "Wave3D physical grid size overflows size_t");
    }

    [[nodiscard]] std::size_t allocated_cell_count() const {
        return detail::checked_size_product(
            detail::checked_size_product(
                allocated_nx(),
                allocated_ny(),
                "Wave3D allocated grid size overflows size_t"),
            allocated_nz(),
            "Wave3D allocated grid size overflows size_t");
    }

    [[nodiscard]] std::size_t physical_linear_index(
        std::size_t x, std::size_t y, std::size_t z) const {
        static_cast<void>(physical_cell_count());
        if (x >= nx || y >= ny || z >= nz) {
            throw std::out_of_range("Wave3D grid index is outside physical storage");
        }
        return x + nx * (y + ny * z);
    }

    // Storage order is [z][y][x], so x is the contiguous dimension.
    [[nodiscard]] std::size_t linear_index(
        std::size_t x, std::size_t y, std::size_t z) const {
        const auto ax = allocated_nx();
        const auto ay = allocated_ny();
        const auto az = allocated_nz();
        // Prove that every intermediate in the indexing expression fits.
        static_cast<void>(detail::checked_size_product(
            detail::checked_size_product(
                ax, ay, "Wave3D allocated grid size overflows size_t"),
            az,
            "Wave3D allocated grid size overflows size_t"));
        if (x >= ax || y >= ay || z >= az) {
            throw std::out_of_range("Wave3D grid index is outside allocated storage");
        }
        return x + ax * (y + ay * z);
    }

    [[nodiscard]] std::size_t physical_origin_x() const {
        return detail::checked_size_add(
            halo,
            x_boundary.lower_absorbing,
            "Wave3D physical grid origin overflows size_t");
    }

    [[nodiscard]] std::size_t physical_origin_y() const {
        return detail::checked_size_add(
            halo,
            y_boundary.lower_absorbing,
            "Wave3D physical grid origin overflows size_t");
    }

    [[nodiscard]] std::size_t physical_origin_z() const {
        return detail::checked_size_add(
            halo,
            z_boundary.lower_absorbing,
            "Wave3D physical grid origin overflows size_t");
    }

private:
    static std::size_t checked_sum(
        std::size_t physical,
        std::size_t halo_width,
        const AxisBoundary& boundary) {
        constexpr auto message = "Wave3D allocated grid dimension overflows size_t";
        auto total = detail::checked_size_add(
            physical, boundary.lower_absorbing, message);
        total = detail::checked_size_add(total, boundary.upper_absorbing, message);
        total = detail::checked_size_add(
            total, detail::checked_size_product(2, halo_width, message), message);
        return total;
    }
};

} // namespace wave3d
