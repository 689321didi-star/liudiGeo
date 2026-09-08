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
class DeviceBuffer {
    static_assert(
        std::is_trivially_copyable<T>::value,
        "DeviceBuffer requires a trivially copyable element type");

public:
    DeviceBuffer() noexcept = default;

    explicit DeviceBuffer(std::size_t element_count) {
        allocate(element_count);
    }

    ~DeviceBuffer() {
        release_noexcept();
    }

    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;

    DeviceBuffer(DeviceBuffer&& other) noexcept
        : pointer_(std::exchange(other.pointer_, nullptr)),
          element_count_(std::exchange(other.element_count_, 0)) {}

    DeviceBuffer& operator=(DeviceBuffer&& other) noexcept {
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

        const auto allocation_bytes = detail::checked_size_product(
            element_count,
            sizeof(T),
            "Wave3D device allocation size overflows size_t");
        void* new_pointer = nullptr;
        check(cudaMalloc(&new_pointer, allocation_bytes), "cudaMalloc");

        release_noexcept();
        pointer_ = static_cast<T*>(new_pointer);
        element_count_ = element_count;
    }

    void clear() {
        if (pointer_ == nullptr) {
            element_count_ = 0;
            return;
        }
        T* old_pointer = std::exchange(pointer_, nullptr);
        element_count_ = 0;
        // Ownership is surrendered before reporting an error so a failed or
        // deferred runtime status cannot cause a second cudaFree in destruction.
        check(cudaFree(old_pointer), "cudaFree");
    }

    void copy_from_host(const T* source, std::size_t element_count) {
        validate_copy(source, element_count);
        if (element_count == 0) {
            return;
        }
        check(
            cudaMemcpy(
                pointer_, source, byte_count(element_count), cudaMemcpyHostToDevice),
            "cudaMemcpy host to device");
    }

    void copy_to_host(T* destination, std::size_t element_count) const {
        validate_copy(destination, element_count);
        if (element_count == 0) {
            return;
        }
        check(
            cudaMemcpy(
                destination,
                pointer_,
                byte_count(element_count),
                cudaMemcpyDeviceToHost),
            "cudaMemcpy device to host");
    }

    void zero() {
        if (pointer_ != nullptr) {
            check(cudaMemset(pointer_, 0, bytes()), "cudaMemset");
        }
    }

    [[nodiscard]] T* get() noexcept {
        return pointer_;
    }

    [[nodiscard]] const T* get() const noexcept {
        return pointer_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return element_count_;
    }

    [[nodiscard]] std::size_t bytes() const {
        return byte_count(element_count_);
    }

    [[nodiscard]] bool empty() const noexcept {
        return element_count_ == 0;
    }

private:
    template <typename Pointer>
    void validate_copy(Pointer pointer, std::size_t element_count) const {
        if (element_count > element_count_) {
            throw std::length_error("copy exceeds Wave3D device-buffer size");
        }
        if (element_count != 0 && pointer == nullptr) {
            throw std::invalid_argument("copy pointer must not be null");
        }
    }

    [[nodiscard]] static std::size_t byte_count(std::size_t element_count) {
        return detail::checked_size_product(
            element_count,
            sizeof(T),
            "Wave3D device-buffer byte count overflows size_t");
    }

    void release_noexcept() noexcept {
        if (pointer_ != nullptr) {
            static_cast<void>(cudaFree(pointer_));
            pointer_ = nullptr;
        }
        element_count_ = 0;
    }

    T* pointer_{nullptr};
    std::size_t element_count_{0};
};

} // namespace wave3d::cuda
