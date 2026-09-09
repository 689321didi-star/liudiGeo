#pragma once

#include "wave3d/io/data.hpp"
#include "wave3d/model/physical_model.hpp"

#include <string>

namespace wave3d::io {

void write_hdf5_model(const std::string& path, const PhysicalModel& model);
[[nodiscard]] PhysicalModel read_hdf5_model(const std::string& path);

void write_hdf5_traces(
    const std::string& path,
    const ThreeComponentTraces& traces);
[[nodiscard]] ThreeComponentTraces read_hdf5_traces(const std::string& path);

void write_hdf5_sparse_snapshot(
    const std::string& path,
    const SparseVelocitySnapshot& snapshot);
[[nodiscard]] SparseVelocitySnapshot read_hdf5_sparse_snapshot(
    const std::string& path);

} // namespace wave3d::io
