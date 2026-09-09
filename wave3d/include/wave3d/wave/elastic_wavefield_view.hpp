#pragma once

#include "wave3d/wave/elastic_wavefield.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace wave3d {

class ReadOnlyFloatView {
public:
    ReadOnlyFloatView() = default;

    explicit ReadOnlyFloatView(const std::vector<float>& values) noexcept
        : data_(values.data()), size_(values.size()) {}

    [[nodiscard]] const float* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] const float& operator[](std::size_t index) const {
        if (index >= size_) {
            throw std::out_of_range("read-only float view index is outside field");
        }
        return data_[index];
    }

    [[nodiscard]] const float* begin() const noexcept { return data_; }
    [[nodiscard]] const float* end() const noexcept {
        return size_ == 0 ? data_ : data_ + size_;
    }

private:
    const float* data_{nullptr};
    std::size_t size_{0};
};

struct ElasticWavefieldConstView {
    Grid3D grid{};
    ReadOnlyFloatView vx_m_s;
    ReadOnlyFloatView vy_m_s;
    ReadOnlyFloatView vz_m_s;
    ReadOnlyFloatView sxx_pa;
    ReadOnlyFloatView syy_pa;
    ReadOnlyFloatView szz_pa;
    ReadOnlyFloatView sxy_pa;
    ReadOnlyFloatView sxz_pa;
    ReadOnlyFloatView syz_pa;

    [[nodiscard]] std::size_t cell_count() const noexcept {
        return vx_m_s.size();
    }
};

inline void require_valid_elastic_wavefield_view(
    const ElasticWavefieldConstView& view) {
    require_valid_grid_geometry(view.grid);
    const auto cells = view.grid.allocated_cell_count();
    for (const auto size : {
             view.vx_m_s.size(), view.vy_m_s.size(), view.vz_m_s.size(),
             view.sxx_pa.size(), view.syy_pa.size(), view.szz_pa.size(),
             view.sxy_pa.size(), view.sxz_pa.size(), view.syz_pa.size()}) {
        if (size != cells) {
            throw std::invalid_argument(
                "read-only elastic field size does not match its grid");
        }
    }
}

[[nodiscard]] inline ElasticWavefieldConstView elastic_wavefield_view(
    const ElasticWavefield& wavefield) {
    require_valid_elastic_wavefield_layout(wavefield);
    return {
        wavefield.grid,
        ReadOnlyFloatView(wavefield.vx_m_s),
        ReadOnlyFloatView(wavefield.vy_m_s),
        ReadOnlyFloatView(wavefield.vz_m_s),
        ReadOnlyFloatView(wavefield.sxx_pa),
        ReadOnlyFloatView(wavefield.syy_pa),
        ReadOnlyFloatView(wavefield.szz_pa),
        ReadOnlyFloatView(wavefield.sxy_pa),
        ReadOnlyFloatView(wavefield.sxz_pa),
        ReadOnlyFloatView(wavefield.syz_pa)};
}

} // namespace wave3d
