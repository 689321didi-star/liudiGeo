#pragma once

#include "wave3d/core/coordinates.hpp"
#include "wave3d/core/grid.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace wave3d {

struct SymmetricMomentTensor {
    double m_xx_nm{0.0};
    double m_yy_nm{0.0};
    double m_zz_nm{0.0};
    double m_xy_nm{0.0};
    double m_xz_nm{0.0};
    double m_yz_nm{0.0};
};

struct RickerWavelet {
    double dominant_frequency_hz{0.0};
    double peak_delay_s{0.0};
    double peak_amplitude{1.0};
};

struct MomentTensorSource {
    PhysicalPoint3D physical_location{};
    FractionalStorageCoordinate3D storage_location{};
    double origin_time_s{0.0};
    SymmetricMomentTensor moment{};
    RickerWavelet wavelet{};
};

inline void require_valid_moment_tensor(const SymmetricMomentTensor& moment) {
    const double components[] = {
        moment.m_xx_nm,
        moment.m_yy_nm,
        moment.m_zz_nm,
        moment.m_xy_nm,
        moment.m_xz_nm,
        moment.m_yz_nm};
    bool has_nonzero_component = false;
    for (const double component : components) {
        if (!std::isfinite(component)) {
            throw std::invalid_argument(
                "moment-tensor components must be finite");
        }
        has_nonzero_component = has_nonzero_component || component != 0.0;
    }
    if (!has_nonzero_component) {
        throw std::invalid_argument(
            "moment tensor must contain a non-zero component");
    }
}

[[nodiscard]] inline SymmetricMomentTensor isotropic_explosion(
    double scalar_moment_nm) {
    if (!std::isfinite(scalar_moment_nm) || !(scalar_moment_nm > 0.0)) {
        throw std::invalid_argument(
            "isotropic explosion moment must be finite and positive");
    }
    return {
        scalar_moment_nm,
        scalar_moment_nm,
        scalar_moment_nm,
        0.0,
        0.0,
        0.0};
}

inline void require_valid_ricker_wavelet(const RickerWavelet& wavelet) {
    if (!std::isfinite(wavelet.dominant_frequency_hz) ||
        !(wavelet.dominant_frequency_hz > 0.0)) {
        throw std::invalid_argument(
            "Ricker dominant frequency must be finite and positive");
    }
    if (!std::isfinite(wavelet.peak_delay_s) ||
        wavelet.peak_delay_s < 0.0) {
        throw std::invalid_argument(
            "Ricker peak delay must be finite and non-negative");
    }
    if (!std::isfinite(wavelet.peak_amplitude) ||
        wavelet.peak_amplitude == 0.0) {
        throw std::invalid_argument(
            "Ricker peak amplitude must be finite and non-zero");
    }
}

[[nodiscard]] inline double ricker_value(
    const RickerWavelet& wavelet,
    double elapsed_time_s) {
    require_valid_ricker_wavelet(wavelet);
    if (!std::isfinite(elapsed_time_s)) {
        throw std::invalid_argument("Ricker sample time must be finite");
    }
    constexpr double pi = 3.141592653589793238462643383279502884;
    const double tau = elapsed_time_s - wavelet.peak_delay_s;
    const double scaled = pi * wavelet.dominant_frequency_hz * tau;
    const double squared = scaled * scaled;
    if (!std::isfinite(squared) || squared > 350.0) {
        return 0.0;
    }
    return wavelet.peak_amplitude * (1.0 - 2.0 * squared) *
           std::exp(-squared);
}

[[nodiscard]] inline MomentTensorSource prepare_moment_tensor_source(
    const Grid3D& grid,
    const PhysicalPoint3D& location,
    double origin_time_s,
    const SymmetricMomentTensor& moment,
    const RickerWavelet& wavelet) {
    if (!std::isfinite(origin_time_s) || origin_time_s < 0.0) {
        throw std::invalid_argument(
            "source origin time must be finite and non-negative");
    }
    require_valid_moment_tensor(moment);
    require_valid_ricker_wavelet(wavelet);
    return {
        location,
        physical_to_storage_coordinate(grid, location),
        origin_time_s,
        moment,
        wavelet};
}

[[nodiscard]] inline double source_time_value(
    const MomentTensorSource& source,
    double absolute_time_s) {
    if (!std::isfinite(absolute_time_s)) {
        throw std::invalid_argument("source sample time must be finite");
    }
    if (absolute_time_s < source.origin_time_s) {
        return 0.0;
    }
    return ricker_value(source.wavelet, absolute_time_s - source.origin_time_s);
}

[[nodiscard]] inline std::string resolved_source_metadata(
    const MomentTensorSource& source) {
    std::ostringstream output;
    output << std::setprecision(17)
           << "coordinate_convention=" << coordinate_convention() << '\n'
           << "source_physical_m=" << source.physical_location.x_m << ','
           << source.physical_location.y_m << ','
           << source.physical_location.z_m << '\n'
           << "source_storage_index_fractional=" << source.storage_location.x
           << ',' << source.storage_location.y << ',' << source.storage_location.z
           << '\n'
           << "source_origin_time_s=" << source.origin_time_s << '\n'
           << "moment_tensor_units=N*m\n"
           << "moment_tensor_order=Mxx,Myy,Mzz,Mxy,Mxz,Myz\n"
           << "moment_tensor=" << source.moment.m_xx_nm << ','
           << source.moment.m_yy_nm << ',' << source.moment.m_zz_nm << ','
           << source.moment.m_xy_nm << ',' << source.moment.m_xz_nm << ','
           << source.moment.m_yz_nm << '\n'
           << "moment_injection_sign=provisional_until_increment_4\n"
           << "ricker_frequency_hz=" << source.wavelet.dominant_frequency_hz
           << '\n'
           << "ricker_peak_delay_s=" << source.wavelet.peak_delay_s << '\n'
           << "ricker_peak_amplitude=" << source.wavelet.peak_amplitude;
    return output.str();
}

} // namespace wave3d
