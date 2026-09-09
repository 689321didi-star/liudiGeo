#include "wave3d/io/hdf5.hpp"

#include <H5Cpp.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace wave3d::io {
namespace {

template <typename Object, typename Value>
void write_scalar_attribute(
    Object& object,
    const char* name,
    const H5::DataType& file_type,
    const H5::DataType& memory_type,
    const Value& value) {
    H5::DataSpace scalar(H5S_SCALAR);
    auto attribute = object.createAttribute(name, file_type, scalar);
    attribute.write(memory_type, &value);
}

template <typename Object, typename Value>
[[nodiscard]] Value read_scalar_attribute(
    Object& object,
    const char* name,
    const H5::DataType& memory_type) {
    Value value{};
    auto attribute = object.openAttribute(name);
    attribute.read(memory_type, &value);
    return value;
}

template <typename Object>
void write_string_attribute(
    Object& object,
    const char* name,
    const std::string& value) {
    H5::StrType type(H5::PredType::C_S1, H5T_VARIABLE);
    H5::DataSpace scalar(H5S_SCALAR);
    auto attribute = object.createAttribute(name, type, scalar);
    attribute.write(type, value);
}

template <typename Object>
[[nodiscard]] std::string read_string_attribute(
    Object& object,
    const char* name) {
    H5::StrType type(H5::PredType::C_S1, H5T_VARIABLE);
    std::string value;
    auto attribute = object.openAttribute(name);
    attribute.read(type, value);
    return value;
}

void write_grid_attributes(H5::H5File& file, const Grid3D& grid) {
    const std::array<std::pair<const char*, std::size_t>, 10> integers{{
        {"nx", grid.nx}, {"ny", grid.ny}, {"nz", grid.nz},
        {"halo", grid.halo},
        {"x_lower_absorbing", grid.x_boundary.lower_absorbing},
        {"x_upper_absorbing", grid.x_boundary.upper_absorbing},
        {"y_lower_absorbing", grid.y_boundary.lower_absorbing},
        {"y_upper_absorbing", grid.y_boundary.upper_absorbing},
        {"z_lower_absorbing", grid.z_boundary.lower_absorbing},
        {"z_upper_absorbing", grid.z_boundary.upper_absorbing}}};
    for (const auto& entry : integers) {
        const auto value = static_cast<unsigned long long>(entry.second);
        write_scalar_attribute(
            file,
            entry.first,
            H5::PredType::STD_U64LE,
            H5::PredType::NATIVE_ULLONG,
            value);
    }
    write_scalar_attribute(
        file, "dx_m", H5::PredType::IEEE_F32LE,
        H5::PredType::NATIVE_FLOAT, grid.dx_m);
    write_scalar_attribute(
        file, "dy_m", H5::PredType::IEEE_F32LE,
        H5::PredType::NATIVE_FLOAT, grid.dy_m);
    write_scalar_attribute(
        file, "dz_m", H5::PredType::IEEE_F32LE,
        H5::PredType::NATIVE_FLOAT, grid.dz_m);
    write_string_attribute(file, "volume_axes", "z,y,x");
    write_string_attribute(
        file, "coordinate_convention", std::string(coordinate_convention()));
}

[[nodiscard]] Grid3D read_grid_attributes(H5::H5File& file) {
    const auto read_size = [&](const char* name) {
        const auto value = read_scalar_attribute<
            H5::H5File, unsigned long long>(
            file, name, H5::PredType::NATIVE_ULLONG);
        if (value > static_cast<unsigned long long>(
                        std::numeric_limits<std::size_t>::max())) {
            throw std::overflow_error("HDF5 grid size exceeds size_t");
        }
        return static_cast<std::size_t>(value);
    };
    Grid3D grid{
        read_size("nx"), read_size("ny"), read_size("nz"),
        read_scalar_attribute<H5::H5File, float>(
            file, "dx_m", H5::PredType::NATIVE_FLOAT),
        read_scalar_attribute<H5::H5File, float>(
            file, "dy_m", H5::PredType::NATIVE_FLOAT),
        read_scalar_attribute<H5::H5File, float>(
            file, "dz_m", H5::PredType::NATIVE_FLOAT),
        read_size("halo"),
        {read_size("x_lower_absorbing"), read_size("x_upper_absorbing")},
        {read_size("y_lower_absorbing"), read_size("y_upper_absorbing")},
        {read_size("z_lower_absorbing"), read_size("z_upper_absorbing")}};
    require_valid_grid_geometry(grid);
    if (read_string_attribute(file, "volume_axes") != "z,y,x" ||
        read_string_attribute(file, "coordinate_convention") !=
            coordinate_convention()) {
        throw std::invalid_argument("HDF5 grid axes/convention is unsupported");
    }
    return grid;
}

void write_float_dataset(
    H5::H5File& file,
    const char* path,
    const std::vector<hsize_t>& dimensions,
    const std::vector<float>& values,
    const char* units) {
    H5::DataSpace space(
        static_cast<int>(dimensions.size()), dimensions.data());
    auto dataset = file.createDataSet(
        path, H5::PredType::IEEE_F32LE, space);
    dataset.write(values.data(), H5::PredType::NATIVE_FLOAT);
    write_string_attribute(dataset, "units", units);
}

[[nodiscard]] std::vector<float> read_float_dataset(
    H5::H5File& file,
    const char* path,
    const std::vector<hsize_t>& expected_dimensions,
    const char* expected_units) {
    auto dataset = file.openDataSet(path);
    auto space = dataset.getSpace();
    if (space.getSimpleExtentNdims() !=
        static_cast<int>(expected_dimensions.size())) {
        throw std::invalid_argument("HDF5 dataset rank mismatch");
    }
    std::vector<hsize_t> dimensions(expected_dimensions.size());
    space.getSimpleExtentDims(dimensions.data());
    if (dimensions != expected_dimensions ||
        read_string_attribute(dataset, "units") != expected_units) {
        throw std::invalid_argument("HDF5 dataset shape/units mismatch");
    }
    std::size_t count = 1;
    for (const hsize_t dimension : dimensions) {
        count = detail::checked_size_product(
            count,
            static_cast<std::size_t>(dimension),
            "HDF5 dataset size overflow");
    }
    std::vector<float> values(count);
    dataset.read(values.data(), H5::PredType::NATIVE_FLOAT);
    return values;
}

void write_source_attributes(H5::H5File& file, const MomentTensorSource& source) {
    const std::array<double, 3> location{{
        source.physical_location.x_m,
        source.physical_location.y_m,
        source.physical_location.z_m}};
    const std::array<double, 3> storage{{
        source.storage_location.x,
        source.storage_location.y,
        source.storage_location.z}};
    const std::array<double, 6> moment{{
        source.moment.m_xx_nm, source.moment.m_yy_nm,
        source.moment.m_zz_nm, source.moment.m_xy_nm,
        source.moment.m_xz_nm, source.moment.m_yz_nm}};
    const auto write_array = [&](const char* name, const double* values, hsize_t n) {
        H5::DataSpace space(1, &n);
        auto attribute = file.createAttribute(
            name, H5::PredType::IEEE_F64LE, space);
        attribute.write(H5::PredType::NATIVE_DOUBLE, values);
    };
    write_array("source_location_m", location.data(), location.size());
    write_array("source_storage_fractional", storage.data(), storage.size());
    write_array("moment_tensor_nm", moment.data(), moment.size());
    write_scalar_attribute(
        file, "source_origin_time_s", H5::PredType::IEEE_F64LE,
        H5::PredType::NATIVE_DOUBLE, source.origin_time_s);
    write_scalar_attribute(
        file, "ricker_frequency_hz", H5::PredType::IEEE_F64LE,
        H5::PredType::NATIVE_DOUBLE, source.wavelet.dominant_frequency_hz);
    write_scalar_attribute(
        file, "ricker_peak_delay_s", H5::PredType::IEEE_F64LE,
        H5::PredType::NATIVE_DOUBLE, source.wavelet.peak_delay_s);
    write_scalar_attribute(
        file, "ricker_peak_rate_s_inv", H5::PredType::IEEE_F64LE,
        H5::PredType::NATIVE_DOUBLE, source.wavelet.peak_rate_s_inv);
    write_string_attribute(file, "moment_tensor_order", "Mxx,Myy,Mzz,Mxy,Mxz,Myz");
    write_string_attribute(file, "stress_sign", "tension_positive");
}

[[nodiscard]] MomentTensorSource read_source_attributes(H5::H5File& file) {
    const auto read_array = [&](const char* name, double* values, hsize_t count) {
        auto attribute = file.openAttribute(name);
        auto space = attribute.getSpace();
        hsize_t dimension = 0;
        if (space.getSimpleExtentNdims() != 1 ||
            space.getSimpleExtentDims(&dimension) < 0 || dimension != count) {
            throw std::invalid_argument("HDF5 source attribute shape mismatch");
        }
        attribute.read(H5::PredType::NATIVE_DOUBLE, values);
    };
    std::array<double, 3> location{};
    std::array<double, 3> storage{};
    std::array<double, 6> moment{};
    read_array("source_location_m", location.data(), location.size());
    read_array("source_storage_fractional", storage.data(), storage.size());
    read_array("moment_tensor_nm", moment.data(), moment.size());
    if (read_string_attribute(file, "moment_tensor_order") !=
            "Mxx,Myy,Mzz,Mxy,Mxz,Myz" ||
        read_string_attribute(file, "stress_sign") != "tension_positive") {
        throw std::invalid_argument("HDF5 source convention is unsupported");
    }
    return {
        {location[0], location[1], location[2]},
        {storage[0], storage[1], storage[2]},
        read_scalar_attribute<H5::H5File, double>(
            file, "source_origin_time_s", H5::PredType::NATIVE_DOUBLE),
        {moment[0], moment[1], moment[2], moment[3], moment[4], moment[5]},
        {read_scalar_attribute<H5::H5File, double>(
             file, "ricker_frequency_hz", H5::PredType::NATIVE_DOUBLE),
         read_scalar_attribute<H5::H5File, double>(
             file, "ricker_peak_delay_s", H5::PredType::NATIVE_DOUBLE),
         read_scalar_attribute<H5::H5File, double>(
             file, "ricker_peak_rate_s_inv", H5::PredType::NATIVE_DOUBLE)}};
}

template <typename Function>
auto translate_hdf5_errors(const std::string& operation, Function&& function)
    -> decltype(function()) {
    H5::Exception::dontPrint();
    try {
        return function();
    } catch (const H5::Exception& error) {
        throw std::runtime_error(
            operation + ": " + error.getDetailMsg());
    }
}

} // namespace

void write_hdf5_model(const std::string& path, const PhysicalModel& model) {
    require_valid_physical_model(model);
    translate_hdf5_errors(path, [&] {
        H5::H5File file(path, H5F_ACC_TRUNC);
        write_string_attribute(file, "schema", "wave3d.model.v1");
        write_grid_attributes(file, model.grid);
        const std::vector<hsize_t> shape{{model.grid.nz, model.grid.ny, model.grid.nx}};
        write_float_dataset(file, "/vp", shape, model.vp_m_s, "m/s");
        write_float_dataset(file, "/vs", shape, model.vs_m_s, "m/s");
        write_float_dataset(
            file, "/rho", shape, model.density_kg_m3, "kg/m3");
    });
}

PhysicalModel read_hdf5_model(const std::string& path) {
    return translate_hdf5_errors(path, [&] {
        H5::H5File file(path, H5F_ACC_RDONLY);
        if (read_string_attribute(file, "schema") != "wave3d.model.v1") {
            throw std::invalid_argument("unsupported HDF5 model schema");
        }
        const auto grid = read_grid_attributes(file);
        const std::vector<hsize_t> shape{{grid.nz, grid.ny, grid.nx}};
        PhysicalModel model{
            grid,
            read_float_dataset(file, "/vp", shape, "m/s"),
            read_float_dataset(file, "/vs", shape, "m/s"),
            read_float_dataset(file, "/rho", shape, "kg/m3")};
        require_valid_physical_model(model);
        return model;
    });
}

void write_hdf5_traces(
    const std::string& path,
    const ThreeComponentTraces& traces) {
    require_valid_traces(traces);
    translate_hdf5_errors(path, [&] {
        H5::H5File file(path, H5F_ACC_TRUNC);
        write_string_attribute(file, "schema", "wave3d.traces.v1");
        write_string_attribute(file, "trace_axes", "source,receiver,time");
        write_string_attribute(file, "component_orientation", "x=east,y=north,z=down");
        write_scalar_attribute(
            file, "dt_s", H5::PredType::IEEE_F64LE,
            H5::PredType::NATIVE_DOUBLE, traces.dt_s);
        write_source_attributes(file, traces.source);
        const std::vector<hsize_t> shape{{
            1, traces.receiver_count, traces.sample_count}};
        write_float_dataset(file, "/vx", shape, traces.vx_m_s, "m/s");
        write_float_dataset(file, "/vy", shape, traces.vy_m_s, "m/s");
        write_float_dataset(file, "/vz", shape, traces.vz_m_s, "m/s");
        std::vector<double> coordinates(traces.receiver_count * 3);
        for (std::size_t receiver = 0; receiver < traces.receiver_count; ++receiver) {
            coordinates[receiver * 3] = traces.receiver_coordinates_m[receiver].x_m;
            coordinates[receiver * 3 + 1] = traces.receiver_coordinates_m[receiver].y_m;
            coordinates[receiver * 3 + 2] = traces.receiver_coordinates_m[receiver].z_m;
        }
        const hsize_t coordinate_shape[2] = {traces.receiver_count, 3};
        H5::DataSpace coordinate_space(2, coordinate_shape);
        auto dataset = file.createDataSet(
            "/receiver_coordinates_m",
            H5::PredType::IEEE_F64LE,
            coordinate_space);
        dataset.write(coordinates.data(), H5::PredType::NATIVE_DOUBLE);
        write_string_attribute(dataset, "axes", "receiver,xyz");
        write_string_attribute(dataset, "units", "m");
    });
}

ThreeComponentTraces read_hdf5_traces(const std::string& path) {
    return translate_hdf5_errors(path, [&] {
        H5::H5File file(path, H5F_ACC_RDONLY);
        if (read_string_attribute(file, "schema") != "wave3d.traces.v1" ||
            read_string_attribute(file, "trace_axes") != "source,receiver,time" ||
            read_string_attribute(file, "component_orientation") !=
                "x=east,y=north,z=down") {
            throw std::invalid_argument("unsupported HDF5 trace convention");
        }
        auto coordinate_dataset = file.openDataSet("/receiver_coordinates_m");
        auto coordinate_space = coordinate_dataset.getSpace();
        hsize_t coordinate_shape[2]{};
        if (coordinate_space.getSimpleExtentNdims() != 2) {
            throw std::invalid_argument("HDF5 receiver coordinate rank mismatch");
        }
        coordinate_space.getSimpleExtentDims(coordinate_shape);
        if (coordinate_shape[0] == 0 || coordinate_shape[1] != 3 ||
            read_string_attribute(coordinate_dataset, "axes") != "receiver,xyz" ||
            read_string_attribute(coordinate_dataset, "units") != "m") {
            throw std::invalid_argument("HDF5 receiver coordinate shape/units mismatch");
        }
        const auto receiver_count = static_cast<std::size_t>(coordinate_shape[0]);
        auto vx_dataset = file.openDataSet("/vx");
        auto vx_space = vx_dataset.getSpace();
        hsize_t trace_shape[3]{};
        if (vx_space.getSimpleExtentNdims() != 3) {
            throw std::invalid_argument("HDF5 trace rank mismatch");
        }
        vx_space.getSimpleExtentDims(trace_shape);
        if (trace_shape[0] != 1 || trace_shape[1] != receiver_count ||
            trace_shape[2] == 0) {
            throw std::invalid_argument("HDF5 trace shape mismatch");
        }
        const auto sample_count = static_cast<std::size_t>(trace_shape[2]);
        const std::vector<hsize_t> shape{{1, receiver_count, sample_count}};
        std::vector<double> coordinates(receiver_count * 3);
        coordinate_dataset.read(
            coordinates.data(), H5::PredType::NATIVE_DOUBLE);
        std::vector<PhysicalPoint3D> receivers(receiver_count);
        for (std::size_t receiver = 0; receiver < receiver_count; ++receiver) {
            receivers[receiver] = {
                coordinates[receiver * 3],
                coordinates[receiver * 3 + 1],
                coordinates[receiver * 3 + 2]};
        }
        ThreeComponentTraces traces{
            receiver_count,
            sample_count,
            read_scalar_attribute<H5::H5File, double>(
                file, "dt_s", H5::PredType::NATIVE_DOUBLE),
            std::move(receivers),
            read_source_attributes(file),
            read_float_dataset(file, "/vx", shape, "m/s"),
            read_float_dataset(file, "/vy", shape, "m/s"),
            read_float_dataset(file, "/vz", shape, "m/s")};
        require_valid_traces(traces);
        return traces;
    });
}

void write_hdf5_sparse_snapshot(
    const std::string& path,
    const SparseVelocitySnapshot& snapshot) {
    require_valid_sparse_snapshot(snapshot);
    translate_hdf5_errors(path, [&] {
        H5::H5File file(path, H5F_ACC_TRUNC);
        write_string_attribute(file, "schema", "wave3d.sparse_velocity.v1");
        write_grid_attributes(file, snapshot.grid);
        const auto step = static_cast<unsigned long long>(snapshot.step_index);
        write_scalar_attribute(
            file, "step_index", H5::PredType::STD_U64LE,
            H5::PredType::NATIVE_ULLONG, step);
        write_scalar_attribute(
            file, "time_s", H5::PredType::IEEE_F64LE,
            H5::PredType::NATIVE_DOUBLE, snapshot.time_s);
        std::vector<unsigned long long> indices(snapshot.storage_indices.size() * 3);
        for (std::size_t point = 0; point < snapshot.storage_indices.size(); ++point) {
            indices[point * 3] = snapshot.storage_indices[point].x;
            indices[point * 3 + 1] = snapshot.storage_indices[point].y;
            indices[point * 3 + 2] = snapshot.storage_indices[point].z;
        }
        const hsize_t index_shape[2] = {snapshot.storage_indices.size(), 3};
        H5::DataSpace index_space(2, index_shape);
        auto index_dataset = file.createDataSet(
            "/storage_indices_xyz", H5::PredType::STD_U64LE, index_space);
        index_dataset.write(indices.data(), H5::PredType::NATIVE_ULLONG);
        write_string_attribute(index_dataset, "axes", "point,xyz");
        const std::vector<hsize_t> shape{{snapshot.storage_indices.size()}};
        write_float_dataset(file, "/vx", shape, snapshot.vx_m_s, "m/s");
        write_float_dataset(file, "/vy", shape, snapshot.vy_m_s, "m/s");
        write_float_dataset(file, "/vz", shape, snapshot.vz_m_s, "m/s");
    });
}

SparseVelocitySnapshot read_hdf5_sparse_snapshot(const std::string& path) {
    return translate_hdf5_errors(path, [&] {
        H5::H5File file(path, H5F_ACC_RDONLY);
        if (read_string_attribute(file, "schema") !=
            "wave3d.sparse_velocity.v1") {
            throw std::invalid_argument("unsupported HDF5 snapshot schema");
        }
        const auto grid = read_grid_attributes(file);
        auto index_dataset = file.openDataSet("/storage_indices_xyz");
        auto index_space = index_dataset.getSpace();
        hsize_t shape[2]{};
        if (index_space.getSimpleExtentNdims() != 2) {
            throw std::invalid_argument("HDF5 snapshot index rank mismatch");
        }
        index_space.getSimpleExtentDims(shape);
        if (shape[0] == 0 || shape[1] != 3 ||
            read_string_attribute(index_dataset, "axes") != "point,xyz") {
            throw std::invalid_argument("HDF5 snapshot index shape mismatch");
        }
        const auto point_count = static_cast<std::size_t>(shape[0]);
        std::vector<unsigned long long> raw_indices(point_count * 3);
        index_dataset.read(raw_indices.data(), H5::PredType::NATIVE_ULLONG);
        std::vector<StorageIndex3D> indices(point_count);
        for (std::size_t point = 0; point < point_count; ++point) {
            indices[point] = {
                static_cast<std::size_t>(raw_indices[point * 3]),
                static_cast<std::size_t>(raw_indices[point * 3 + 1]),
                static_cast<std::size_t>(raw_indices[point * 3 + 2])};
        }
        const auto step = read_scalar_attribute<H5::H5File, unsigned long long>(
            file, "step_index", H5::PredType::NATIVE_ULLONG);
        const std::vector<hsize_t> value_shape{{point_count}};
        SparseVelocitySnapshot snapshot{
            grid,
            static_cast<std::size_t>(step),
            read_scalar_attribute<H5::H5File, double>(
                file, "time_s", H5::PredType::NATIVE_DOUBLE),
            std::move(indices),
            read_float_dataset(file, "/vx", value_shape, "m/s"),
            read_float_dataset(file, "/vy", value_shape, "m/s"),
            read_float_dataset(file, "/vz", value_shape, "m/s")};
        require_valid_sparse_snapshot(snapshot);
        return snapshot;
    });
}

} // namespace wave3d::io
