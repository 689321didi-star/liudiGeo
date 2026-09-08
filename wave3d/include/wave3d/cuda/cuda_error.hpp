#pragma once

#include <cuda_runtime_api.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace wave3d::cuda {

class CudaError : public std::runtime_error {
public:
    CudaError(cudaError_t code, std::string operation)
        : std::runtime_error(
              std::move(operation) + ": " + cudaGetErrorString(code)),
          code_(code) {}

    [[nodiscard]] cudaError_t code() const noexcept {
        return code_;
    }

private:
    cudaError_t code_;
};

inline void check(cudaError_t result, const char* operation) {
    if (result != cudaSuccess) {
        throw CudaError(result, operation);
    }
}

inline void check_last_launch(const char* operation) {
    check(cudaGetLastError(), operation);
}

inline void synchronize() {
    check(cudaDeviceSynchronize(), "cudaDeviceSynchronize");
}

} // namespace wave3d::cuda
