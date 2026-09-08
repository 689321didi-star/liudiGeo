#pragma once

#include <cstddef>
#include <limits>
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
        return checked_product(checked_product(nx, ny), nz);
    }

    [[nodiscard]] std::size_t allocated_cell_count() const {
        return checked_product(
            checked_product(allocated_nx(), allocated_ny()), allocated_nz());
    }

    // Storage order is [z][y][x], so x is the contiguous dimension.
    [[nodiscard]] std::size_t linear_index(
        std::size_t x, std::size_t y, std::size_t z) const {
        const auto ax = allocated_nx();
        const auto ay = allocated_ny();
        const auto az = allocated_nz();
        // Prove that every intermediate in the indexing expression fits.
        static_cast<void>(checked_product(checked_product(ax, ay), az));
        if (x >= ax || y >= ay || z >= az) {
            throw std::out_of_range("Wave3D grid index is outside allocated storage");
        }
        return x + ax * (y + ay * z);
    }

    [[nodiscard]] std::size_t physical_origin_x() const {
        return checked_add(halo, x_boundary.lower_absorbing);
    }

    [[nodiscard]] std::size_t physical_origin_y() const {
        return checked_add(halo, y_boundary.lower_absorbing);
    }

    [[nodiscard]] std::size_t physical_origin_z() const {
        return checked_add(halo, z_boundary.lower_absorbing);
    }

private:
    static std::size_t checked_product(std::size_t a, std::size_t b) {
        if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
            throw std::overflow_error("Wave3D grid size overflows size_t");
        }
        return a * b;
    }

    static std::size_t checked_add(std::size_t a, std::size_t b) {
        if (b > std::numeric_limits<std::size_t>::max() - a) {
            throw std::overflow_error("Wave3D grid size overflows size_t");
        }
        return a + b;
    }

    static std::size_t checked_sum(
        std::size_t physical,
        std::size_t halo_width,
        const AxisBoundary& boundary) {
        auto total = checked_add(physical, boundary.lower_absorbing);
        total = checked_add(total, boundary.upper_absorbing);
        total = checked_add(total, checked_product(2, halo_width));
        return total;
    }
};

} // namespace wave3d
