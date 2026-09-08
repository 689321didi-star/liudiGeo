#pragma once

#include "wave3d/cuda/device_buffer.hpp"

namespace wave3d::cuda {

void fill(DeviceBuffer<float>& buffer, float value);

} // namespace wave3d::cuda
