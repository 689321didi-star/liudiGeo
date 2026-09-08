#include "wave3d/cuda/device_info.hpp"

#include "wave3d/cuda/cuda_error.hpp"

#include <cuda_runtime_api.h>

#include <stdexcept>
#include <string>

namespace wave3d::cuda {

int device_count() {
    int count = 0;
    check(cudaGetDeviceCount(&count), "cudaGetDeviceCount");
    return count;
}

DeviceInfo query_device(int ordinal) {
    const int count = device_count();
    if (ordinal < 0 || ordinal >= count) {
        throw std::out_of_range("CUDA device ordinal is outside the available range");
    }

    cudaDeviceProp properties{};
    check(cudaGetDeviceProperties(&properties, ordinal), "cudaGetDeviceProperties");
    check(cudaSetDevice(ordinal), "cudaSetDevice");

    std::size_t free_bytes = 0;
    std::size_t total_bytes = 0;
    check(cudaMemGetInfo(&free_bytes, &total_bytes), "cudaMemGetInfo");

    int driver_version = 0;
    int runtime_version = 0;
    check(cudaDriverGetVersion(&driver_version), "cudaDriverGetVersion");
    check(cudaRuntimeGetVersion(&runtime_version), "cudaRuntimeGetVersion");

    return {
        ordinal,
        properties.name,
        properties.major,
        properties.minor,
        driver_version,
        runtime_version,
        total_bytes,
        free_bytes};
}

std::string format_cuda_version(int encoded_version) {
    if (encoded_version < 0) {
        throw std::invalid_argument("encoded CUDA version must be non-negative");
    }
    const int major = encoded_version / 1000;
    const int minor = (encoded_version % 1000) / 10;
    return std::to_string(major) + "." + std::to_string(minor);
}

} // namespace wave3d::cuda
