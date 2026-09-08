#pragma once

#include <cstddef>
#include <string>

namespace wave3d::cuda {

struct DeviceInfo {
    int ordinal{0};
    std::string name;
    int compute_major{0};
    int compute_minor{0};
    int driver_version{0};
    int runtime_version{0};
    std::size_t total_memory_bytes{0};
    std::size_t free_memory_bytes{0};
};

[[nodiscard]] int device_count();
[[nodiscard]] DeviceInfo query_device(int ordinal);
[[nodiscard]] std::string format_cuda_version(int encoded_version);

} // namespace wave3d::cuda
