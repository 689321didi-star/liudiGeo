#include "wave3d/core/grid.hpp"
#include "wave3d/cuda/cuda_error.hpp"
#include "wave3d/cuda/elastic_propagator.hpp"
#include "wave3d/cuda/visualization.hpp"
#include "wave3d/wave/elastic_wavefield.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

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
        7, 6, 5,
        10.0F, 20.0F, 30.0F,
        6,
        {6, 7}, {7, 6}, {0, 8}};
}

[[nodiscard]] double affine_vx(double x, double y, double z) {
    return 1.0 + 0.01 * x + 0.02 * y + 0.03 * z;
}

[[nodiscard]] double affine_vy(double x, double y, double z) {
    return -2.0 + 0.04 * x - 0.01 * y + 0.02 * z;
}

[[nodiscard]] double affine_vz(double x, double y, double z) {
    return 0.5 - 0.02 * x + 0.03 * y - 0.04 * z;
}

[[nodiscard]] wave3d::ElasticWavefield affine_wavefield(
    const wave3d::Grid3D& geometry) {
    wave3d::ElasticWavefield result(geometry);
    const auto ox = static_cast<double>(geometry.physical_origin_x());
    const auto oy = static_cast<double>(geometry.physical_origin_y());
    const auto oz = static_cast<double>(geometry.physical_origin_z());
    for (std::size_t z = 0; z < geometry.allocated_nz(); ++z) {
        for (std::size_t y = 0; y < geometry.allocated_ny(); ++y) {
            for (std::size_t x = 0; x < geometry.allocated_nx(); ++x) {
                const auto index = geometry.linear_index(x, y, z);
                const auto integer_x =
                    (static_cast<double>(x) - ox) * geometry.dx_m;
                const auto integer_y =
                    (static_cast<double>(y) - oy) * geometry.dy_m;
                const auto integer_z =
                    (static_cast<double>(z) - oz) * geometry.dz_m;
                result.vx_m_s[index] = static_cast<float>(affine_vx(
                    integer_x + 0.5 * geometry.dx_m,
                    integer_y,
                    integer_z));
                result.vy_m_s[index] = static_cast<float>(affine_vy(
                    integer_x,
                    integer_y + 0.5 * geometry.dy_m,
                    integer_z));
                result.vz_m_s[index] = static_cast<float>(affine_vz(
                    integer_x,
                    integer_y,
                    integer_z + 0.5 * geometry.dz_m));
            }
        }
    }
    return result;
}

[[nodiscard]] bool approximately_equal(
    float actual,
    double expected,
    double tolerance = 2.0e-5) {
    return std::abs(static_cast<double>(actual) - expected) <= tolerance;
}

[[nodiscard]] double expected_value(
    wave3d::cuda::VisualizationField field,
    double x,
    double y,
    double z) {
    const double vx = affine_vx(x, y, z);
    const double vy = affine_vy(x, y, z);
    const double vz = affine_vz(x, y, z);
    switch (field) {
    case wave3d::cuda::VisualizationField::Vx:
        return vx;
    case wave3d::cuda::VisualizationField::Vy:
        return vy;
    case wave3d::cuda::VisualizationField::Vz:
        return vz;
    case wave3d::cuda::VisualizationField::Speed:
        return std::sqrt(vx * vx + vy * vy + vz * vz);
    case wave3d::cuda::VisualizationField::Divergence:
        return 0.01 - 0.01 - 0.04;
    case wave3d::cuda::VisualizationField::CurlMagnitude: {
        constexpr double curl_x = 0.03 - 0.02;
        constexpr double curl_y = 0.03 - (-0.02);
        constexpr double curl_z = 0.04 - 0.02;
        return std::sqrt(
            curl_x * curl_x + curl_y * curl_y + curl_z * curl_z);
    }
    }
    throw std::invalid_argument("test received an unknown field");
}

void expect_wavefield_equal(
    const wave3d::ElasticWavefield& actual,
    const wave3d::ElasticWavefield& expected) {
    const std::array<const std::vector<float>*, 9> actual_fields{{
        &actual.vx_m_s,
        &actual.vy_m_s,
        &actual.vz_m_s,
        &actual.sxx_pa,
        &actual.syy_pa,
        &actual.szz_pa,
        &actual.sxy_pa,
        &actual.sxz_pa,
        &actual.syz_pa}};
    const std::array<const std::vector<float>*, 9> expected_fields{{
        &expected.vx_m_s,
        &expected.vy_m_s,
        &expected.vz_m_s,
        &expected.sxx_pa,
        &expected.syy_pa,
        &expected.szz_pa,
        &expected.sxy_pa,
        &expected.sxz_pa,
        &expected.syz_pa}};
    for (std::size_t field = 0; field < actual_fields.size(); ++field) {
        expect(
            *actual_fields[field] == *expected_fields[field],
            "visualization extraction modified its source wavefield");
    }
}

void test_affine_staggered_extraction() {
    const auto geometry = grid();
    const auto source = affine_wavefield(geometry);
    wave3d::cuda::DeviceElasticWavefield device_wavefield(geometry);
    device_wavefield.upload(source);
    wave3d::cuda::DeviceVisualizationVolume output(geometry);
    const auto* allocation = output.data();
    expect(
        output.value_count() == geometry.physical_cell_count() &&
            output.bytes() ==
                geometry.physical_cell_count() * sizeof(float),
        "visualization output size is not the physical grid size");

    const std::array<wave3d::cuda::VisualizationField, 6> fields{{
        wave3d::cuda::VisualizationField::Vx,
        wave3d::cuda::VisualizationField::Vy,
        wave3d::cuda::VisualizationField::Vz,
        wave3d::cuda::VisualizationField::Speed,
        wave3d::cuda::VisualizationField::Divergence,
        wave3d::cuda::VisualizationField::CurlMagnitude}};
    std::vector<float> host(geometry.physical_cell_count());
    for (const auto field : fields) {
        wave3d::cuda::extract_physical_visualization_volume(
            device_wavefield.const_view(), field, output);
        wave3d::cuda::synchronize();
        output.download(host);
        expect(
            output.data() == allocation,
            "visualization extraction replaced its reusable allocation");
        for (std::size_t z = 0; z < geometry.nz; ++z) {
            for (std::size_t y = 0; y < geometry.ny; ++y) {
                for (std::size_t x = 0; x < geometry.nx; ++x) {
                    const auto index = geometry.physical_linear_index(x, y, z);
                    const double physical_x = x * geometry.dx_m;
                    const double physical_y = y * geometry.dy_m;
                    const double physical_z = z * geometry.dz_m;
                    expect(
                        approximately_equal(
                            host[index],
                            expected_value(
                                field,
                                physical_x,
                                physical_y,
                                physical_z)),
                        "visualization value does not match affine reference");
                }
            }
        }
    }

    wave3d::ElasticWavefield after(geometry);
    device_wavefield.download(after);
    expect_wavefield_equal(after, source);
}

void test_invalid_extraction_inputs() {
    const auto geometry = grid();
    const auto source = affine_wavefield(geometry);
    wave3d::cuda::DeviceElasticWavefield device_wavefield(geometry);
    device_wavefield.upload(source);
    auto other_grid = geometry;
    ++other_grid.nx;
    wave3d::cuda::DeviceVisualizationVolume wrong_output(other_grid);
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::cuda::extract_physical_visualization_volume(
                device_wavefield.const_view(),
                wave3d::cuda::VisualizationField::Vx,
                wrong_output);
        },
        "visualization extraction accepted a mismatched output grid");

    wave3d::cuda::DeviceVisualizationVolume output(geometry);
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::cuda::extract_physical_visualization_volume(
                device_wavefield.const_view(),
                static_cast<wave3d::cuda::VisualizationField>(999),
                output);
        },
        "visualization extraction accepted an unknown field");
    std::vector<float> wrong_host(output.value_count() - 1);
    expect_throws<std::invalid_argument>(
        [&] { output.download(wrong_host); },
        "visualization download accepted an incorrect host size");
}

} // namespace

int main() {
    static_assert(
        !std::is_copy_constructible<
            wave3d::cuda::DeviceVisualizationVolume>::value,
        "visualization volume must not copy device ownership");
    static_assert(
        std::is_move_constructible<
            wave3d::cuda::DeviceVisualizationVolume>::value,
        "visualization volumes must support buffered ownership transfer");

    try {
        test_affine_staggered_extraction();
        test_invalid_extraction_inputs();
    } catch (const std::exception& error) {
        std::cerr << "CUDA visualization test failure: " << error.what()
                  << '\n';
        ++failures;
    }
    if (failures != 0) {
        std::cerr << failures << " CUDA visualization test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D CUDA visualization extraction tests passed\n";
    return 0;
}
