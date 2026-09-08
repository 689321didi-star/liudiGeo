#include "wave3d/cuda/fill.hpp"

#include "wave3d/cuda/cuda_error.hpp"

#include <algorithm>
#include <cstddef>

namespace wave3d::cuda {
namespace {

__global__ void fill_kernel(float* values, std::size_t element_count, float value) {
    const auto index = static_cast<std::size_t>(blockIdx.x) * blockDim.x +
                       threadIdx.x;
    const auto stride = static_cast<std::size_t>(blockDim.x) * gridDim.x;
    for (auto i = index; i < element_count;) {
        values[i] = value;
        if (element_count - i <= stride) {
            break;
        }
        i += stride;
    }
}

} // namespace

void fill(DeviceBuffer<float>& buffer, float value) {
    if (buffer.empty()) {
        return;
    }

    constexpr unsigned int threads_per_block = 256;
    constexpr std::size_t max_blocks = 65535;
    const auto required_blocks =
        (buffer.size() - 1) / threads_per_block + 1;
    const auto block_count = static_cast<unsigned int>(
        std::min(required_blocks, max_blocks));
    fill_kernel<<<block_count, threads_per_block>>>(
        buffer.get(), buffer.size(), value);
    check_last_launch("fill_kernel launch");
}

} // namespace wave3d::cuda
