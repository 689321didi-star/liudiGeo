#include "wave3d/cuda/cuda_error.hpp"
#include "wave3d/cuda/device_buffer.hpp"
#include "wave3d/cuda/device_info.hpp"
#include "wave3d/cuda/fill.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_device_query() {
    expect(wave3d::cuda::device_count() > 0, "at least one CUDA device is required");
    const auto info = wave3d::cuda::query_device(0);
    expect(!info.name.empty(), "CUDA device name must not be empty");
    expect(
        info.name.find("RTX 5060") != std::string::npos,
        "target CUDA device must be an RTX 5060");
    expect(info.compute_major == 12, "RTX 5060 compute major must be 12");
    expect(info.compute_minor == 0, "RTX 5060 compute minor must be 0");
    expect(info.total_memory_bytes > 0, "total device memory must be positive");
    expect(
        info.free_memory_bytes <= info.total_memory_bytes,
        "free device memory cannot exceed total memory");
    expect(info.driver_version > 0, "CUDA driver version must be available");
    expect(info.runtime_version > 0, "CUDA runtime version must be available");
    expect(
        wave3d::cuda::format_cuda_version(13020) == "13.2",
        "encoded CUDA version must be formatted correctly");

    bool threw = false;
    try {
        static_cast<void>(wave3d::cuda::query_device(-1));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "negative CUDA device ordinal must fail");
}

void test_allocation_copy_fill_and_move() {
    static_assert(
        !std::is_copy_constructible<wave3d::cuda::DeviceBuffer<float>>::value,
        "device buffer must not be copy constructible");
    static_assert(
        std::is_nothrow_move_constructible<
            wave3d::cuda::DeviceBuffer<float>>::value,
        "device buffer move construction must be noexcept");

    constexpr std::size_t element_count = 4096;
    std::vector<float> host(element_count, 1.25F);
    wave3d::cuda::DeviceBuffer<float> buffer(element_count);
    expect(buffer.size() == element_count, "device buffer size must match request");
    expect(
        buffer.bytes() == element_count * sizeof(float),
        "device buffer byte count must match request");

    buffer.copy_from_host(host.data(), host.size());
    wave3d::cuda::fill(buffer, 3.25F);
    wave3d::cuda::synchronize();
    buffer.copy_to_host(host.data(), host.size());
    expect(
        std::all_of(host.begin(), host.end(), [](float value) {
            return value == 3.25F;
        }),
        "fill kernel result must round-trip through device memory");

    wave3d::cuda::DeviceBuffer<float> moved(std::move(buffer));
    expect(buffer.empty(), "moved-from device buffer must be empty");
    expect(moved.size() == element_count, "move construction must preserve size");

    wave3d::cuda::DeviceBuffer<float> assigned(1);
    assigned = std::move(moved);
    expect(moved.empty(), "move-assigned source must be empty");
    expect(assigned.size() == element_count, "move assignment must preserve size");

    assigned.zero();
    assigned.copy_to_host(host.data(), host.size());
    expect(
        std::all_of(host.begin(), host.end(), [](float value) {
            return value == 0.0F;
        }),
        "zeroed device buffer must contain zeroes");

    bool threw = false;
    try {
        assigned.copy_to_host(host.data(), host.size() + 1);
    } catch (const std::length_error&) {
        threw = true;
    }
    expect(threw, "oversized device-buffer copy must fail before CUDA access");

    assigned.clear();
    expect(assigned.empty(), "cleared device buffer must be empty");
}

void test_precondition_and_error_translation() {
    wave3d::cuda::DeviceBuffer<float> buffer(1);

    bool threw = false;
    try {
        buffer.copy_from_host(nullptr, 1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "null host pointer must fail before CUDA access");

    wave3d::cuda::DeviceBuffer<std::size_t> overflow_buffer;
    threw = false;
    try {
        overflow_buffer.allocate(std::numeric_limits<std::size_t>::max());
    } catch (const std::overflow_error&) {
        threw = true;
    }
    expect(threw, "device-buffer byte overflow must fail before allocation");

    threw = false;
    try {
        wave3d::cuda::check(cudaErrorInvalidValue, "intentional CUDA error");
    } catch (const wave3d::cuda::CudaError& error) {
        threw = error.code() == cudaErrorInvalidValue &&
                std::string(error.what()).find("intentional CUDA error") !=
                    std::string::npos;
    }
    expect(threw, "CUDA errors must preserve code and operation context");
}

} // namespace

int main() {
    try {
        test_device_query();
        test_allocation_copy_fill_and_move();
        test_precondition_and_error_translation();
    } catch (const std::exception& error) {
        std::cerr << "UNEXPECTED ERROR: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Wave3D CUDA foundation tests passed\n";
    return 0;
}
