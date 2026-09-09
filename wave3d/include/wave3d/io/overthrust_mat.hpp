#pragma once

#include "wave3d/core/grid.hpp"
#include "wave3d/model/derived_overthrust.hpp"

#include <string>
#include <vector>

namespace wave3d::io {

struct CanonicalVpCrop {
    Grid3D grid{};
    std::vector<float> vp_m_s;
};

// Reads the audited MATLAB v5 contract: d=[dz,dy,dx], n=[nz,ny,nx], and
// data[nz,ny,nx] stored in MATLAB column-major order. Only the requested
// hyperslab is decoded, then it is reordered to Wave3D [z][y][x], x-fastest.
[[nodiscard]] CanonicalVpCrop read_overthrust_mat_v5_vp_crop(
    const std::string& path,
    const Grid3D& expected_source_grid,
    const PhysicalVolumeWindow3D& window);

} // namespace wave3d::io
