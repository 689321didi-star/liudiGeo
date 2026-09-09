#include "wave3d/io/overthrust_mat.hpp"

#include <matio.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct TemporaryMat {
    std::filesystem::path path;

    ~TemporaryMat() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

[[nodiscard]] TemporaryMat temporary_mat() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return {{std::filesystem::temp_directory_path() /
             ("wave3d_overthrust_" + std::to_string(stamp) + ".mat")}};
}

void write_variable(
    mat_t* file,
    const char* name,
    int rank,
    std::size_t* shape,
    std::vector<double>& values) {
    matvar_t* variable = Mat_VarCreate(
        name,
        MAT_C_DOUBLE,
        MAT_T_DOUBLE,
        rank,
        shape,
        values.data(),
        0);
    if (variable == nullptr) {
        throw std::runtime_error("failed to create test MAT variable");
    }
    const int status = Mat_VarWrite(file, variable, MAT_COMPRESSION_NONE);
    Mat_VarFree(variable);
    if (status != MATIO_E_NO_ERROR) {
        throw std::runtime_error("failed to write test MAT variable");
    }
}

void write_mat(
    const std::filesystem::path& path,
    std::vector<double> spacing,
    std::vector<double> dimensions,
    std::array<std::size_t, 3> data_shape,
    std::vector<double> data,
    bool include_data = true) {
    mat_t* file = Mat_CreateVer(
        path.string().c_str(), "Wave3D synthetic adapter test", MAT_FT_MAT5);
    if (file == nullptr) {
        throw std::runtime_error("failed to create test MAT file");
    }
    try {
        std::size_t vector_shape[2] = {1, 3};
        write_variable(file, "d", 2, vector_shape, spacing);
        write_variable(file, "n", 2, vector_shape, dimensions);
        if (include_data) {
            write_variable(file, "data", 3, data_shape.data(), data);
        }
    } catch (...) {
        static_cast<void>(Mat_Close(file));
        throw;
    }
    if (Mat_Close(file) != MATIO_E_NO_ERROR) {
        throw std::runtime_error("failed to close test MAT file");
    }
}

[[nodiscard]] wave3d::Grid3D source_grid() {
    return {4, 3, 5, 25.0F, 25.0F, 25.0F, 0, {}, {}, {}};
}

[[nodiscard]] std::vector<double> uniquely_indexed_values() {
    const auto grid = source_grid();
    std::vector<double> values(grid.physical_cell_count());
    for (std::size_t x = 0; x < grid.nx; ++x) {
        for (std::size_t y = 0; y < grid.ny; ++y) {
            for (std::size_t z = 0; z < grid.nz; ++z) {
                values[z + grid.nz * (y + grid.ny * x)] =
                    2000.0 + static_cast<double>(x) +
                    10.0 * static_cast<double>(y) +
                    100.0 * static_cast<double>(z);
            }
        }
    }
    return values;
}

void test_crop_and_axis_mapping() {
    auto temporary = temporary_mat();
    write_mat(
        temporary.path,
        {25.0, 25.0, 25.0},
        {5.0, 3.0, 4.0},
        {5, 3, 4},
        uniquely_indexed_values());
    const wave3d::PhysicalVolumeWindow3D window{1, 1, 1, 2, 2, 3};
    const auto actual = wave3d::io::read_overthrust_mat_v5_vp_crop(
        temporary.path.string(), source_grid(), window);
    expect(
        actual.grid.nx == 2 && actual.grid.ny == 2 && actual.grid.nz == 3 &&
            actual.grid.dx_m == 25.0F && actual.grid.halo == 0,
        "MAT crop grid/spacing changed");
    expect(actual.vp_m_s.size() == 12, "MAT crop size changed");
    for (std::size_t z = 0; z < window.nz; ++z) {
        for (std::size_t y = 0; y < window.ny; ++y) {
            for (std::size_t x = 0; x < window.nx; ++x) {
                const float expected = static_cast<float>(
                    2000 + (window.x_begin + x) +
                    10 * (window.y_begin + y) +
                    100 * (window.z_begin + z));
                expect(
                    actual.vp_m_s[actual.grid.physical_linear_index(x, y, z)] ==
                        expected,
                    "MATLAB column-major axes were not mapped to x-fastest");
            }
        }
    }
}

template <typename Function>
void expect_rejected(Function&& function, const char* message) {
    bool threw = false;
    try {
        function();
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, message);
}

void test_contract_rejection() {
    {
        auto temporary = temporary_mat();
        write_mat(
            temporary.path,
            {20.0, 25.0, 25.0},
            {5.0, 3.0, 4.0},
            {5, 3, 4},
            uniquely_indexed_values());
        expect_rejected(
            [&] {
                static_cast<void>(wave3d::io::read_overthrust_mat_v5_vp_crop(
                    temporary.path.string(),
                    source_grid(),
                    {0, 0, 0, 4, 3, 5}));
            },
            "mismatched d spacing must fail");
    }
    {
        auto temporary = temporary_mat();
        write_mat(
            temporary.path,
            {25.0, 25.0, 25.0},
            {5.0, 3.0, 4.0},
            {5, 3, 3},
            std::vector<double>(45, 2000.0));
        expect_rejected(
            [&] {
                static_cast<void>(wave3d::io::read_overthrust_mat_v5_vp_crop(
                    temporary.path.string(),
                    source_grid(),
                    {0, 0, 0, 4, 3, 5}));
            },
            "mismatched data shape must fail");
    }
    {
        auto temporary = temporary_mat();
        auto values = uniquely_indexed_values();
        values[0] = 2000.1;
        write_mat(
            temporary.path,
            {25.0, 25.0, 25.0},
            {5.0, 3.0, 4.0},
            {5, 3, 4},
            std::move(values));
        expect_rejected(
            [&] {
                static_cast<void>(wave3d::io::read_overthrust_mat_v5_vp_crop(
                    temporary.path.string(),
                    source_grid(),
                    {0, 0, 0, 4, 3, 5}));
            },
            "non-binary32 source Vp must fail");
    }
    {
        auto temporary = temporary_mat();
        write_mat(
            temporary.path,
            {25.0, 25.0, 25.0},
            {5.0, 3.0, 4.0},
            {5, 3, 4},
            uniquely_indexed_values(),
            false);
        expect_rejected(
            [&] {
                static_cast<void>(wave3d::io::read_overthrust_mat_v5_vp_crop(
                    temporary.path.string(),
                    source_grid(),
                    {0, 0, 0, 4, 3, 5}));
            },
            "missing data variable must fail");
    }
}

} // namespace

int main() {
    test_crop_and_axis_mapping();
    test_contract_rejection();
    if (failures != 0) {
        std::cerr << failures << " Overthrust MAT adapter test(s) failed\n";
        return 1;
    }
    std::cout << "Wave3D Overthrust MAT adapter tests passed\n";
    return 0;
}
