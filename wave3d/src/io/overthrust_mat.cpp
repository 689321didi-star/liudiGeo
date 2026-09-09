#include "wave3d/io/overthrust_mat.hpp"

#include "wave3d/core/checked_size.hpp"
#include "wave3d/core/coordinates.hpp"

#include <matio.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace wave3d::io {
namespace {

struct MatCloser {
    void operator()(mat_t* file) const noexcept {
        if (file != nullptr) {
            static_cast<void>(Mat_Close(file));
        }
    }
};

struct MatVariableDeleter {
    void operator()(matvar_t* variable) const noexcept {
        Mat_VarFree(variable);
    }
};

using MatFile = std::unique_ptr<mat_t, MatCloser>;
using MatVariable = std::unique_ptr<matvar_t, MatVariableDeleter>;

[[nodiscard]] MatVariable read_variable(mat_t* file, const char* name) {
    MatVariable variable(Mat_VarRead(file, name));
    if (!variable) {
        throw std::invalid_argument(
            std::string("Overthrust MAT variable is missing: ") + name);
    }
    return variable;
}

[[nodiscard]] MatVariable read_variable_info(mat_t* file, const char* name) {
    MatVariable variable(Mat_VarReadInfo(file, name));
    if (!variable) {
        throw std::invalid_argument(
            std::string("Overthrust MAT variable is missing: ") + name);
    }
    return variable;
}

void require_real_double(
    const matvar_t& variable,
    const char* name,
    bool metadata_only = false) {
    const bool unloaded_metadata = metadata_only &&
        variable.data_type == MAT_T_UNKNOWN && variable.data_size == 0;
    if (variable.class_type != MAT_C_DOUBLE ||
        (!unloaded_metadata &&
         (variable.data_type != MAT_T_DOUBLE || variable.data_size != 8)) ||
        variable.isComplex != 0 || variable.isLogical != 0) {
        throw std::invalid_argument(
            std::string("Overthrust MAT variable must be real double: ") +
            name + " (class=" + std::to_string(variable.class_type) +
            ", type=" + std::to_string(variable.data_type) +
            ", bytes=" + std::to_string(variable.data_size) +
            ", complex=" + std::to_string(variable.isComplex) +
            ", logical=" + std::to_string(variable.isLogical) + ")");
    }
}

[[nodiscard]] std::array<double, 3> read_row_vector(
    mat_t* file,
    const char* name) {
    auto variable = read_variable(file, name);
    require_real_double(*variable, name);
    if (variable->rank != 2 || variable->dims == nullptr ||
        variable->dims[0] != 1 || variable->dims[1] != 3 ||
        variable->data == nullptr) {
        throw std::invalid_argument(
            std::string("Overthrust MAT variable must have shape [1,3]: ") +
            name);
    }
    const auto* values = static_cast<const double*>(variable->data);
    return {values[0], values[1], values[2]};
}

void require_physical_source_descriptor(const Grid3D& grid) {
    require_valid_grid_geometry(grid);
    if (grid.halo != 0 || grid.x_boundary.lower_absorbing != 0 ||
        grid.x_boundary.upper_absorbing != 0 ||
        grid.y_boundary.lower_absorbing != 0 ||
        grid.y_boundary.upper_absorbing != 0 ||
        grid.z_boundary.lower_absorbing != 0 ||
        grid.z_boundary.upper_absorbing != 0) {
        throw std::invalid_argument(
            "Overthrust MAT source descriptor must contain only physical cells");
    }
}

void require_window_inside(
    const Grid3D& source,
    const PhysicalVolumeWindow3D& window) {
    if (window.nx == 0 || window.ny == 0 || window.nz == 0) {
        throw std::invalid_argument(
            "Overthrust MAT crop dimensions must be positive");
    }
    const auto x_end = detail::checked_size_add(
        window.x_begin, window.nx, "Overthrust MAT crop x range overflow");
    const auto y_end = detail::checked_size_add(
        window.y_begin, window.ny, "Overthrust MAT crop y range overflow");
    const auto z_end = detail::checked_size_add(
        window.z_begin, window.nz, "Overthrust MAT crop z range overflow");
    if (x_end > source.nx || y_end > source.ny || z_end > source.nz) {
        throw std::out_of_range(
            "Overthrust MAT crop is outside the expected source grid");
    }
}

[[nodiscard]] int matio_index(std::size_t value) {
    if (value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::overflow_error(
            "Overthrust MAT hyperslab index exceeds MatIO int range");
    }
    return static_cast<int>(value);
}

} // namespace

CanonicalVpCrop read_overthrust_mat_v5_vp_crop(
    const std::string& path,
    const Grid3D& expected_source_grid,
    const PhysicalVolumeWindow3D& window) {
    require_physical_source_descriptor(expected_source_grid);
    require_window_inside(expected_source_grid, window);

    MatFile file(Mat_Open(path.c_str(), MAT_ACC_RDONLY));
    if (!file) {
        throw std::runtime_error("cannot open Overthrust MAT file: " + path);
    }
    if (Mat_GetVersion(file.get()) != MAT_FT_MAT5) {
        throw std::invalid_argument(
            "Overthrust input must be a MATLAB v5 MAT file");
    }

    const auto spacing = read_row_vector(file.get(), "d");
    const std::array<double, 3> expected_spacing{{
        static_cast<double>(expected_source_grid.dz_m),
        static_cast<double>(expected_source_grid.dy_m),
        static_cast<double>(expected_source_grid.dx_m)}};
    if (spacing != expected_spacing) {
        throw std::invalid_argument(
            "Overthrust MAT d must exactly match [dz,dy,dx]");
    }

    const auto dimensions = read_row_vector(file.get(), "n");
    const std::array<double, 3> expected_dimensions{{
        static_cast<double>(expected_source_grid.nz),
        static_cast<double>(expected_source_grid.ny),
        static_cast<double>(expected_source_grid.nx)}};
    if (dimensions != expected_dimensions) {
        throw std::invalid_argument(
            "Overthrust MAT n must exactly match [nz,ny,nx]");
    }

    auto data = read_variable_info(file.get(), "data");
    // MatIO 1.5 reports MAT_T_UNKNOWN/data_size=0 for an unloaded MAT5
    // numeric variable while preserving its MATLAB class and flags.
    require_real_double(*data, "data", true);
    if (data->rank != 3 || data->dims == nullptr ||
        data->dims[0] != expected_source_grid.nz ||
        data->dims[1] != expected_source_grid.ny ||
        data->dims[2] != expected_source_grid.nx) {
        throw std::invalid_argument(
            "Overthrust MAT data must have shape [nz,ny,nx]");
    }

    const Grid3D crop_grid{
        window.nx,
        window.ny,
        window.nz,
        expected_source_grid.dx_m,
        expected_source_grid.dy_m,
        expected_source_grid.dz_m,
        0,
        {},
        {},
        {}};
    require_valid_grid_geometry(crop_grid);
    const auto crop_cells = crop_grid.physical_cell_count();
    std::vector<double> matlab_values(crop_cells);
    std::array<int, 3> start{{
        matio_index(window.z_begin),
        matio_index(window.y_begin),
        matio_index(window.x_begin)}};
    std::array<int, 3> stride{{1, 1, 1}};
    std::array<int, 3> edge{{
        matio_index(window.nz),
        matio_index(window.ny),
        matio_index(window.nx)}};
    if (Mat_VarReadData(
            file.get(),
            data.get(),
            matlab_values.data(),
            start.data(),
            stride.data(),
            edge.data()) != MATIO_E_NO_ERROR) {
        throw std::runtime_error(
            "failed to decode the Overthrust MAT data crop");
    }

    std::vector<float> canonical_values(crop_cells);
    for (std::size_t x = 0; x < window.nx; ++x) {
        for (std::size_t y = 0; y < window.ny; ++y) {
            for (std::size_t z = 0; z < window.nz; ++z) {
                const auto matlab_index =
                    z + window.nz * (y + window.ny * x);
                const double value = matlab_values[matlab_index];
                const float converted = static_cast<float>(value);
                if (!std::isfinite(value) || !(value > 0.0) ||
                    !std::isfinite(converted) ||
                    static_cast<double>(converted) != value) {
                    throw std::invalid_argument(
                        "Overthrust MAT Vp must be positive and exactly float32");
                }
                canonical_values[crop_grid.physical_linear_index(x, y, z)] =
                    converted;
            }
        }
    }
    return {crop_grid, std::move(canonical_values)};
}

} // namespace wave3d::io
