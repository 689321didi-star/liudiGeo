#pragma once

#include "wave3d/core/checked_size.hpp"
#include "wave3d/cuda/cuda_error.hpp"

#include <cuda_runtime_api.h>

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace wave3d::cuda {

template <typename T>
class PinnedHostBuffer {
    static_assert(
        std::is_trivially_copyable<T>::value,
        "PinnedHostBuffer requires a trivially copyable element type");

public:
    PinnedHostBuffer() noexcept = default;
    explicit PinnedHostBuffer(std::size_t element_count) {
        allocate(element_count);
    }
    ~PinnedHostBuffer() { release_noexcept(); }

    PinnedHostBuffer(const PinnedHostBuffer&) = delete;
    PinnedHostBuffer& operator=(const PinnedHostBuffer&) = delete;

    PinnedHostBuffer(PinnedHostBuffer&& other) noexcept
        : pointer_(std::exchange(other.pointer_, nullptr)),
          element_count_(std::exchange(other.element_count_, 0)) {}

    PinnedHostBuffer& operator=(PinnedHostBuffer&& other) noexcept {
        if (this != &other) {
            release_noexcept();
            pointer_ = std::exchange(other.pointer_, nullptr);
            element_count_ = std::exchange(other.element_count_, 0);
        }
        return *this;
    }

    void allocate(std::size_t element_count) {
        if (element_count == 0) {
            clear();
            return;
        }
        const auto byte_count = detail::checked_size_product(
            element_count,
            sizeof(T),
            "Wave3D pinned-host allocation size overflows size_t");
        void* allocation = nullptr;
        check(
            cudaHostAlloc(&allocation, byte_count, cudaHostAllocDefault),
            "cudaHostAlloc");
        release_noexcept();
        pointer_ = static_cast<T*>(allocation);
        element_count_ = element_count;
    }

    void clear() {
        if (pointer_ == nullptr) {
            element_count_ = 0;
            return;
        }
        auto* old_pointer = std::exchange(pointer_, nullptr);
        element_count_ = 0;
        check(cudaFreeHost(old_pointer), "cudaFreeHost");
    }

    [[nodiscard]] T* data() noexcept { return pointer_; }
    [[nodiscard]] const T* data() const noexcept { return pointer_; }
    [[nodiscard]] std::size_t size() const noexcept { return element_count_; }
    [[nodiscard]] bool empty() const noexcept { return element_count_ == 0; }
    [[nodiscard]] std::size_t bytes() const {
        return detail::checked_size_product(
            element_count_,
            sizeof(T),
            "Wave3D pinned-host byte count overflows size_t");
    }

private:
    void release_noexcept() noexcept {
        if (pointer_ != nullptr) {
            static_cast<void>(cudaFreeHost(pointer_));
            pointer_ = nullptr;
        }
        element_count_ = 0;
    }

    T* pointer_{nullptr};
    std::size_t element_count_{0};
};

} // namespace wave3d::cuda
