#include "wave3d/numerics/cpu_staggered_derivative.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

bool nearly_equal(double actual, double expected, double tolerance) {
    return std::abs(actual - expected) <= tolerance;
}

wave3d::Grid3D test_grid() {
    return {
        17, 18, 19,
        0.4F, 0.6F, 0.8F,
        0,
        {0, 0}, {0, 0}, {0, 0}};
}

constexpr std::array<wave3d::DerivativeAxis, 3> axes{{
    wave3d::DerivativeAxis::X,
    wave3d::DerivativeAxis::Y,
    wave3d::DerivativeAxis::Z}};

constexpr std::array<wave3d::StaggeredDerivativeMapping, 2> mappings{{
    wave3d::StaggeredDerivativeMapping::IntegerToHalf,
    wave3d::StaggeredDerivativeMapping::HalfToInteger}};

std::size_t axis_index(
    const wave3d::StorageIndex3D& index,
    wave3d::DerivativeAxis axis) {
    switch (axis) {
    case wave3d::DerivativeAxis::X:
        return index.x;
    case wave3d::DerivativeAxis::Y:
        return index.y;
    case wave3d::DerivativeAxis::Z:
        return index.z;
    }
    throw std::invalid_argument("unknown test axis");
}

double axis_spacing(
    const wave3d::Grid3D& grid,
    wave3d::DerivativeAxis axis) {
    switch (axis) {
    case wave3d::DerivativeAxis::X:
        return static_cast<double>(grid.dx_m);
    case wave3d::DerivativeAxis::Y:
        return static_cast<double>(grid.dy_m);
    case wave3d::DerivativeAxis::Z:
        return static_cast<double>(grid.dz_m);
    }
    throw std::invalid_argument("unknown test axis");
}

wave3d::StorageIndex3D shifted(
    wave3d::StorageIndex3D index,
    wave3d::DerivativeAxis axis,
    int offset) {
    auto* coordinate = &index.x;
    if (axis == wave3d::DerivativeAxis::Y) {
        coordinate = &index.y;
    } else if (axis == wave3d::DerivativeAxis::Z) {
        coordinate = &index.z;
    }
    if (offset < 0) {
        *coordinate -= static_cast<std::size_t>(-offset);
    } else {
        *coordinate += static_cast<std::size_t>(offset);
    }
    return index;
}

template <typename Function>
std::vector<double> make_axis_field(
    const wave3d::Grid3D& grid,
    wave3d::DerivativeAxis axis,
    Function function) {
    std::vector<double> field(grid.allocated_cell_count());
    for (std::size_t z = 0; z < grid.allocated_nz(); ++z) {
        for (std::size_t y = 0; y < grid.allocated_ny(); ++y) {
            for (std::size_t x = 0; x < grid.allocated_nx(); ++x) {
                const wave3d::StorageIndex3D index{x, y, z};
                field[grid.linear_index(x, y, z)] =
                    function(axis_index(index, axis));
            }
        }
    }
    return field;
}

void test_complete_stencil_ranges() {
    const auto grid = test_grid();
    const auto x_integer_to_half = wave3d::complete_stencil_target_range(
        grid,
        wave3d::DerivativeAxis::X,
        wave3d::StaggeredDerivativeMapping::IntegerToHalf);
    expect(
        x_integer_to_half.begin == 5 && x_integer_to_half.end == 11 &&
            x_integer_to_half.size() == 6,
        "I-to-H x range must encode i-5 through i+6");

    const auto x_half_to_integer = wave3d::complete_stencil_target_range(
        grid,
        wave3d::DerivativeAxis::X,
        wave3d::StaggeredDerivativeMapping::HalfToInteger);
    expect(
        x_half_to_integer.begin == 6 && x_half_to_integer.end == 12 &&
            x_half_to_integer.size() == 6,
        "H-to-I x range must encode i-6 through i+5");

    const auto z_integer_to_half = wave3d::complete_stencil_target_range(
        grid,
        wave3d::DerivativeAxis::Z,
        wave3d::StaggeredDerivativeMapping::IntegerToHalf);
    expect(
        z_integer_to_half.begin == 5 && z_integer_to_half.end == 13,
        "target range must use the selected allocated axis extent");
}

void test_constants_and_affine_fields() {
    const auto grid = test_grid();
    const wave3d::StorageIndex3D target{8, 8, 8};
    const std::vector<double> constant(grid.allocated_cell_count(), 7.25);
    for (const auto axis : axes) {
        for (const auto mapping : mappings) {
            expect(
                wave3d::cpu_staggered_derivative_at(
                    constant, grid, target, axis, mapping) == 0.0,
                "a constant field derivative must be exactly zero");

            const auto spacing = axis_spacing(grid, axis);
            const auto source_offset =
                mapping == wave3d::StaggeredDerivativeMapping::IntegerToHalf
                    ? 0.0
                    : 0.5;
            const auto field = make_axis_field(
                grid, axis, [spacing, source_offset](std::size_t index) {
                    const auto coordinate =
                        (static_cast<double>(index) + source_offset) * spacing;
                    return 2.75 * coordinate - 1.25;
                });
            expect(
                nearly_equal(
                    wave3d::cpu_staggered_derivative_at(
                        field, grid, target, axis, mapping),
                    2.75,
                    2.0e-14),
                "every axis and lattice mapping must differentiate an affine field");
        }
    }
}

void test_polynomials_through_degree_twelve() {
    const auto grid = test_grid();
    const wave3d::StorageIndex3D target{8, 8, 8};
    for (const auto axis : axes) {
        const auto spacing = axis_spacing(grid, axis);
        for (const auto mapping : mappings) {
            const auto source_offset =
                mapping == wave3d::StaggeredDerivativeMapping::IntegerToHalf
                    ? 0.0
                    : 0.5;
            const auto target_offset =
                mapping == wave3d::StaggeredDerivativeMapping::IntegerToHalf
                    ? 0.5
                    : 0.0;
            const auto target_coordinate =
                (static_cast<double>(axis_index(target, axis)) + target_offset) *
                spacing;
            const auto scale =
                static_cast<double>(wave3d::staggered_fd_radius) * spacing;

            for (int degree = 0; degree <= 12; ++degree) {
                const auto field = make_axis_field(
                    grid,
                    axis,
                    [=](std::size_t index) {
                        const auto source_coordinate =
                            (static_cast<double>(index) + source_offset) * spacing;
                        const auto argument =
                            (source_coordinate - target_coordinate) / scale + 0.2;
                        return std::pow(argument, degree);
                    });
                const auto expected = degree == 0
                                          ? 0.0
                                          : static_cast<double>(degree) / scale *
                                                std::pow(0.2, degree - 1);
                const auto actual = wave3d::cpu_staggered_derivative_at(
                    field, grid, target, axis, mapping);
                expect(
                    nearly_equal(actual, expected, 2.0e-12),
                    "polynomials through degree twelve must differentiate to roundoff");
            }
        }
    }
}

void test_impulse_offsets_and_signs() {
    const auto grid = test_grid();
    const wave3d::StorageIndex3D target{8, 8, 8};
    for (const auto axis : axes) {
        const auto spacing = axis_spacing(grid, axis);
        for (const auto mapping : mappings) {
            for (std::size_t coefficient_index = 0;
                 coefficient_index < wave3d::staggered_fd_radius;
                 ++coefficient_index) {
                const auto positive_offset =
                    mapping == wave3d::StaggeredDerivativeMapping::IntegerToHalf
                        ? static_cast<int>(coefficient_index + 1)
                        : static_cast<int>(coefficient_index);
                const auto negative_offset =
                    mapping == wave3d::StaggeredDerivativeMapping::IntegerToHalf
                        ? -static_cast<int>(coefficient_index)
                        : -static_cast<int>(coefficient_index + 1);

                std::vector<double> impulse(grid.allocated_cell_count(), 0.0);
                const auto positive = shifted(target, axis, positive_offset);
                impulse[grid.linear_index(positive.x, positive.y, positive.z)] = 1.0;
                expect(
                    nearly_equal(
                        wave3d::cpu_staggered_derivative_at(
                            impulse, grid, target, axis, mapping),
                        wave3d::staggered_first_derivative_coefficients
                                [coefficient_index] /
                            spacing,
                        1.0e-15),
                    "positive impulse must select the correct staggered coefficient");

                std::fill(impulse.begin(), impulse.end(), 0.0);
                const auto negative = shifted(target, axis, negative_offset);
                impulse[grid.linear_index(negative.x, negative.y, negative.z)] = 1.0;
                expect(
                    nearly_equal(
                        wave3d::cpu_staggered_derivative_at(
                            impulse, grid, target, axis, mapping),
                        -wave3d::staggered_first_derivative_coefficients
                                 [coefficient_index] /
                            spacing,
                        1.0e-15),
                    "negative impulse must select the correct staggered coefficient");
            }
        }
    }
}

double sinusoid_error(
    float spacing,
    wave3d::StaggeredDerivativeMapping mapping) {
    const wave3d::Grid3D grid{
        20, 1, 1,
        spacing, 1.0F, 1.0F,
        0,
        {0, 0}, {0, 0}, {0, 0}};
    const wave3d::StorageIndex3D target{10, 0, 0};
    constexpr double wavenumber = 3.0;
    constexpr double phase = 0.37;
    const auto source_offset =
        mapping == wave3d::StaggeredDerivativeMapping::IntegerToHalf
            ? 0.0
            : 0.5;
    const auto target_offset =
        mapping == wave3d::StaggeredDerivativeMapping::IntegerToHalf
            ? 0.5
            : 0.0;
    const auto h = static_cast<double>(spacing);
    const auto target_coordinate =
        (static_cast<double>(target.x) + target_offset) * h;
    const auto field = make_axis_field(
        grid,
        wave3d::DerivativeAxis::X,
        [=](std::size_t index) {
            const auto source_coordinate =
                (static_cast<double>(index) + source_offset) * h;
            return std::sin(
                wavenumber * (source_coordinate - target_coordinate) + phase);
        });
    const auto actual = wave3d::cpu_staggered_derivative_at(
        field, grid, target, wave3d::DerivativeAxis::X, mapping);
    return std::abs(actual - wavenumber * std::cos(phase));
}

void test_twelfth_order_convergence() {
    for (const auto mapping : mappings) {
        const auto coarse_error = sinusoid_error(0.2F, mapping);
        const auto fine_error = sinusoid_error(0.1F, mapping);
        const auto observed_order =
            std::log(coarse_error / fine_error) / std::log(2.0);
        expect(
            observed_order > 11.7 && observed_order < 12.2,
            "smooth sinusoid refinement must exhibit twelfth-order convergence");
    }
}

void test_float_input_and_rejections() {
    const auto grid = test_grid();
    const wave3d::StorageIndex3D target{8, 8, 8};
    std::vector<float> field(grid.allocated_cell_count());
    for (std::size_t z = 0; z < grid.allocated_nz(); ++z) {
        for (std::size_t y = 0; y < grid.allocated_ny(); ++y) {
            for (std::size_t x = 0; x < grid.allocated_nx(); ++x) {
                field[grid.linear_index(x, y, z)] =
                    1.5F * static_cast<float>(x) - 2.0F;
            }
        }
    }
    expect(
        nearly_equal(
            wave3d::cpu_staggered_derivative_at(
                field,
                grid,
                target,
                wave3d::DerivativeAxis::X,
                wave3d::StaggeredDerivativeMapping::IntegerToHalf),
            1.5 / static_cast<double>(grid.dx_m),
            2.0e-7),
        "the CPU reference must accept float32 propagation fields");

    field.pop_back();
    bool threw = false;
    try {
        static_cast<void>(wave3d::cpu_staggered_derivative_at(
            field,
            grid,
            target,
            wave3d::DerivativeAxis::X,
            wave3d::StaggeredDerivativeMapping::IntegerToHalf));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "a mismatched field size must fail");

    const std::vector<double> valid_field(grid.allocated_cell_count(), 1.0);
    threw = false;
    try {
        static_cast<void>(wave3d::cpu_staggered_derivative_at(
            valid_field,
            grid,
            {4, 8, 8},
            wave3d::DerivativeAxis::X,
            wave3d::StaggeredDerivativeMapping::IntegerToHalf));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "a target without a complete stencil must fail");

    threw = false;
    try {
        static_cast<void>(wave3d::cpu_staggered_derivative_at(
            static_cast<const double*>(nullptr),
            grid.allocated_cell_count(),
            grid,
            target,
            wave3d::DerivativeAxis::X,
            wave3d::StaggeredDerivativeMapping::IntegerToHalf));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "a null field must fail");

    auto short_grid = grid;
    short_grid.nx = 11;
    threw = false;
    try {
        static_cast<void>(wave3d::complete_stencil_target_range(
            short_grid,
            wave3d::DerivativeAxis::X,
            wave3d::StaggeredDerivativeMapping::IntegerToHalf));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "an axis shorter than the full stencil must fail");
}

} // namespace

int main() {
    test_complete_stencil_ranges();
    test_constants_and_affine_fields();
    test_polynomials_through_degree_twelve();
    test_impulse_offsets_and_signs();
    test_twelfth_order_convergence();
    test_float_input_and_rejections();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Wave3D CPU staggered-derivative tests passed\n";
    return 0;
}
