#include "wave3d/acquisition/receiver.hpp"
#include "wave3d/acquisition/source.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near(double left, double right, double tolerance = 1.0e-12) {
    return std::abs(left - right) <= tolerance;
}

wave3d::Grid3D test_grid() {
    return {
        11, 9, 6,
        10.0F, 20.0F, 5.0F,
        2,
        {3, 4}, {5, 6}, {0, 7}};
}

double scalar_moment(const wave3d::SymmetricMomentTensor& moment) {
    return std::sqrt(0.5 *
                     (moment.m_xx_nm * moment.m_xx_nm +
                      moment.m_yy_nm * moment.m_yy_nm +
                      moment.m_zz_nm * moment.m_zz_nm +
                      2.0 * moment.m_xy_nm * moment.m_xy_nm +
                      2.0 * moment.m_xz_nm * moment.m_xz_nm +
                      2.0 * moment.m_yz_nm * moment.m_yz_nm));
}

void test_double_couple_conversion() {
    constexpr double moment_nm = 2.0e12;
    const auto strike_slip =
        wave3d::double_couple_from_strike_dip_rake(
            moment_nm, 0.0, 90.0, 0.0);
    expect(
        near(strike_slip.m_xy_nm, moment_nm, 1.0e-3) &&
            near(strike_slip.m_xx_nm, 0.0, 1.0e-3) &&
            near(strike_slip.m_yy_nm, 0.0, 1.0e-3) &&
            near(strike_slip.m_zz_nm, 0.0, 1.0e-3),
        "north-striking vertical strike-slip tensor is incorrect");

    const auto horizontal =
        wave3d::double_couple_from_strike_dip_rake(
            moment_nm, 0.0, 0.0, 0.0);
    expect(
        near(horizontal.m_yz_nm, -moment_nm, 1.0e-3) &&
            near(horizontal.m_xz_nm, 0.0, 1.0e-3),
        "NED north-down component did not map to Wave3D Myz");

    const auto reverse = wave3d::double_couple_from_strike_dip_rake(
        moment_nm, 0.0, 45.0, 90.0);
    const auto normal = wave3d::double_couple_from_strike_dip_rake(
        moment_nm, 0.0, 45.0, -90.0);
    expect(
        near(reverse.m_xx_nm, -moment_nm, 1.0e-3) &&
            near(reverse.m_yy_nm, 0.0, 1.0e-3) &&
            near(reverse.m_zz_nm, moment_nm, 1.0e-3),
        "north-striking reverse-fault tensor is incorrect");
    expect(
        near(normal.m_xx_nm, moment_nm, 1.0e-3) &&
            near(normal.m_zz_nm, -moment_nm, 1.0e-3),
        "north-striking normal-fault tensor is incorrect");

    const auto oblique = wave3d::double_couple_from_strike_dip_rake(
        moment_nm, 123.0, 38.0, -47.0);
    expect(
        near(
            oblique.m_xx_nm + oblique.m_yy_nm + oblique.m_zz_nm,
            0.0,
            1.0e-3),
        "double-couple tensor must be trace free");
    expect(
        near(scalar_moment(oblique), moment_nm, 1.0e-3),
        "double-couple tensor must preserve scalar moment");

    for (const auto& invalid : std::vector<std::vector<double>>{
             {0.0, 0.0, 45.0, 0.0},
             {moment_nm, 360.0, 45.0, 0.0},
             {moment_nm, 0.0, 91.0, 0.0},
             {moment_nm, 0.0, 45.0, 181.0},
             {moment_nm,
              0.0,
              std::numeric_limits<double>::quiet_NaN(),
              0.0}}) {
        bool threw = false;
        try {
            static_cast<void>(
                wave3d::double_couple_from_strike_dip_rake(
                    invalid[0], invalid[1], invalid[2], invalid[3]));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        expect(threw, "invalid double-couple parameter must fail");
    }
}

void test_ricker_wavelet() {
    const wave3d::RickerWavelet wavelet{20.0, 0.05, 2.0};
    expect(
        near(wave3d::ricker_value(wavelet, 0.05), 2.0),
        "Ricker value at peak delay must equal peak moment rate");
    expect(
        near(
            wave3d::ricker_value(wavelet, 0.04),
            wave3d::ricker_value(wavelet, 0.06)),
        "Ricker wavelet must be symmetric about its peak");

    constexpr double pi = 3.141592653589793238462643383279502884;
    const double zero_crossing = 0.05 + 1.0 / (std::sqrt(2.0) * pi * 20.0);
    expect(
        std::abs(wave3d::ricker_value(wavelet, zero_crossing)) < 1.0e-14,
        "Ricker analytical zero crossing must be reproduced");
    expect(
        wave3d::ricker_value(wavelet, 1.0e300) == 0.0,
        "far-tail Ricker evaluation must remain finite");

    bool threw = false;
    try {
        static_cast<void>(wave3d::ricker_value({0.0, 0.05, 1.0}, 0.0));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "non-positive Ricker frequency must fail");

    threw = false;
    try {
        static_cast<void>(wave3d::ricker_value({20.0, 0.05, 0.0}, 0.0));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "zero Ricker peak moment rate must fail");
}

void test_source_preparation() {
    const auto moment = wave3d::isotropic_explosion(1.0e12);
    expect(
        moment.m_xx_nm == 1.0e12 && moment.m_yy_nm == 1.0e12 &&
            moment.m_zz_nm == 1.0e12 && moment.m_xy_nm == 0.0 &&
            moment.m_xz_nm == 0.0 && moment.m_yz_nm == 0.0,
        "isotropic explosion must have equal diagonal tensor components");

    const auto source = wave3d::prepare_moment_tensor_source(
        test_grid(), {20.0, 40.0, 10.0}, 0.1, moment, {25.0, 0.04, 3.0});
    expect(
        near(source.storage_location.x, 7.0) &&
            near(source.storage_location.y, 9.0) &&
            near(source.storage_location.z, 4.0),
        "source must be mapped to padded storage coordinates once");
    expect(
        source_time_value(source, 0.099) == 0.0,
        "source must be silent before origin time");
    expect(
        near(source_time_value(source, 0.14), 3.0),
        "source peak must include origin time and Ricker delay");

    const std::string metadata = wave3d::resolved_source_metadata(source);
    expect(
        metadata.find("x=east") != std::string::npos &&
            metadata.find("moment_tensor_order=Mxx,Myy,Mzz,Mxy,Mxz,Myz") !=
                std::string::npos &&
            metadata.find("stress_sign=tension_positive") != std::string::npos &&
            metadata.find("moment_body_force=f_i=-M_ij*s(t)*d_j_delta") !=
                std::string::npos &&
            metadata.find("moment_stress_rate=-M_ij*q(t)*delta") !=
                std::string::npos &&
            metadata.find("ricker_value_units=s^-1") != std::string::npos,
        "source metadata must expose coordinates, sign, and moment-rate units");

    bool threw = false;
    try {
        static_cast<void>(wave3d::isotropic_explosion(0.0));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "zero isotropic moment must fail");

    threw = false;
    try {
        static_cast<void>(wave3d::isotropic_explosion(-1.0));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "negative isotropic explosion moment must fail");

    threw = false;
    try {
        static_cast<void>(wave3d::prepare_moment_tensor_source(
            test_grid(), {101.0, 0.0, 0.0}, 0.0, moment, {20.0, 0.05, 1.0}));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "source outside the physical grid must fail");

    threw = false;
    try {
        static_cast<void>(wave3d::prepare_moment_tensor_source(
            test_grid(),
            {20.0, 40.0, 10.0},
            std::numeric_limits<double>::quiet_NaN(),
            moment,
            {20.0, 0.05, 1.0}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "non-finite source origin time must fail");
}

void test_receiver_preparation() {
    const auto receivers = wave3d::make_regular_surface_receivers(
        test_grid(), {10.0, 20.0, 20.0, 20.0, 3, 2});
    expect(receivers.receivers.size() == 6, "regular receiver count must match");
    expect(
        near(receivers.receivers[0].physical_location.x_m, 10.0) &&
            near(receivers.receivers[1].physical_location.x_m, 30.0) &&
            near(receivers.receivers[2].physical_location.x_m, 50.0) &&
            near(receivers.receivers[3].physical_location.x_m, 10.0) &&
            near(receivers.receivers[3].physical_location.y_m, 40.0),
        "regular receiver order must be x-fastest then y");
    expect(
        near(receivers.receivers[0].storage_location.x, 6.0) &&
            near(receivers.receivers[0].storage_location.y, 8.0) &&
            near(receivers.receivers[0].storage_location.z, 2.0),
        "surface receiver must map to the physical storage origin");

    const std::string metadata = wave3d::resolved_receiver_metadata(receivers);
    expect(
        metadata.find("receiver_components=vx,vy,vz") != std::string::npos &&
            metadata.find("receiver_count=6") != std::string::npos &&
            metadata.find("x_fastest") != std::string::npos,
        "receiver metadata must expose components, count, and ordering");

    bool threw = false;
    try {
        static_cast<void>(
            wave3d::prepare_receiver_set(test_grid(), std::vector<wave3d::PhysicalPoint3D>{}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "empty receiver set must fail");

    threw = false;
    try {
        static_cast<void>(wave3d::make_regular_surface_receivers(
            test_grid(), {0.0, 0.0, 10.0, 10.0, 0, 2}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "zero receiver count must fail");

    threw = false;
    try {
        static_cast<void>(wave3d::make_regular_surface_receivers(
            test_grid(), {0.0, 0.0, 0.0, 10.0, 2, 2}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "zero spacing on a multi-receiver axis must fail");

    threw = false;
    try {
        static_cast<void>(wave3d::make_regular_surface_receivers(
            test_grid(), {80.0, 0.0, 21.0, 10.0, 2, 2}));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "regular receiver grid extending outside the domain must fail");
}

} // namespace

int main() {
    test_double_couple_conversion();
    test_ricker_wavelet();
    test_source_preparation();
    test_receiver_preparation();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Wave3D acquisition tests passed\n";
    return 0;
}
