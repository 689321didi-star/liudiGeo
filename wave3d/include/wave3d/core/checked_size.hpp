#pragma once

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace wave3d::detail {

[[nodiscard]] inline std::size_t checked_size_add(
    std::size_t left,
    std::size_t right,
    const char* message) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw std::overflow_error(message);
    }
    return left + right;
}

[[nodiscard]] inline std::size_t checked_size_product(
    std::size_t left,
    std::size_t right,
    const char* message) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        throw std::overflow_error(message);
    }
    return left * right;
}

} // namespace wave3d::detail
