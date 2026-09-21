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
    // q(t)=ds/dt is a moment-rate shape, so its amplitude has units s^-1.
    double peak_rate_s_inv{1.0};
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

// Aki-Richards double-couple convention. Strike is clockwise from north,
// dip is downward from horizontal, and rake is measured from strike toward
// down-dip. The published NED equations are mapped to Wave3D's END axes.
[[nodiscard]] inline SymmetricMomentTensor double_couple_from_strike_dip_rake(
    double scalar_moment_nm,
    double strike_deg,
    double dip_deg,
    double rake_deg) {
    const double values[]{scalar_moment_nm, strike_deg, dip_deg, rake_deg};
    for (const auto value : values) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "double-couple parameters must be finite");
        }
    }
    if (!(scalar_moment_nm > 0.0)) {
        throw std::invalid_argument(
            "double-couple scalar moment must be positive");
    }
    if (strike_deg < 0.0 || strike_deg >= 360.0) {
        throw std::invalid_argument(
            "double-couple strike must be in [0, 360) degrees");
    }
    if (dip_deg < 0.0 || dip_deg > 90.0) {
        throw std::invalid_argument(
            "double-couple dip must be in [0, 90] degrees");
    }
    if (rake_deg < -180.0 || rake_deg > 180.0) {
        throw std::invalid_argument(
            "double-couple rake must be in [-180, 180] degrees");
    }

    constexpr double pi = 3.141592653589793238462643383279502884;
    constexpr double degrees_to_radians = pi / 180.0;
    const double strike = strike_deg * degrees_to_radians;
    const double dip = dip_deg * degrees_to_radians;
    const double rake = rake_deg * degrees_to_radians;
    const double sin_strike = std::sin(strike);
    const double cos_strike = std::cos(strike);
    const double sin_dip = std::sin(dip);
    const double cos_dip = std::cos(dip);
    const double sin_rake = std::sin(rake);
    const double cos_rake = std::cos(rake);
    const double sin_2strike = std::sin(2.0 * strike);
    const double cos_2strike = std::cos(2.0 * strike);
    const double sin_2dip = std::sin(2.0 * dip);
    const double cos_2dip = std::cos(2.0 * dip);

    // North-east-down components from GFZ IS 3.9 equation (4), DN=0.
    const double m_nn = -scalar_moment_nm *
                        (sin_2strike * sin_dip * cos_rake +
                         sin_strike * sin_strike * sin_2dip * sin_rake);
    const double m_ee = scalar_moment_nm *
                        (sin_2strike * sin_dip * cos_rake -
                         cos_strike * cos_strike * sin_2dip * sin_rake);
    const double m_dd = scalar_moment_nm * sin_2dip * sin_rake;
    const double m_ne = scalar_moment_nm *
                        (cos_2strike * sin_dip * cos_rake +
                         0.5 * sin_2strike * sin_2dip * sin_rake);
    const double m_nd = -scalar_moment_nm *
                        (cos_strike * cos_dip * cos_rake +
                         sin_strike * cos_2dip * sin_rake);
    const double m_ed = -scalar_moment_nm *
                        (sin_strike * cos_dip * cos_rake -
                         cos_strike * cos_2dip * sin_rake);

    const SymmetricMomentTensor result{
        m_ee, m_nn, m_dd, m_ne, m_ed, m_nd};
    require_valid_moment_tensor(result);
    return result;
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
    if (!std::isfinite(wavelet.peak_rate_s_inv) ||
        wavelet.peak_rate_s_inv == 0.0) {
        throw std::invalid_argument(
            "Ricker peak moment rate must be finite and non-zero");
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
    return wavelet.peak_rate_s_inv * (1.0 - 2.0 * squared) *
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
           << "stress_sign=tension_positive\n"
           << "moment_body_force=f_i=-M_ij*s(t)*d_j_delta\n"
           << "moment_stress_rate=-M_ij*q(t)*delta\n"
           << "ricker_frequency_hz=" << source.wavelet.dominant_frequency_hz
           << '\n'
           << "ricker_peak_delay_s=" << source.wavelet.peak_delay_s << '\n'
           << "ricker_value_units=s^-1\n"
           << "ricker_peak_rate_s_inv=" << source.wavelet.peak_rate_s_inv;
    return output.str();
}

} // namespace wave3d
