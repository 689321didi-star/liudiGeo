#include "wave3d/acquisition/moment_source_injector.hpp"
#include "wave3d/acquisition/receiver_sampler.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Function>
void expect_throws(Function&& function, const std::string& message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] bool nearly_equal(
    double actual,
    double expected,
    double relative_tolerance = 2.0e-6,
    double absolute_tolerance = 2.0e-6) {
    return std::abs(actual - expected) <=
           absolute_tolerance + relative_tolerance * std::abs(expected);
}

[[nodiscard]] wave3d::Grid3D test_grid() {
    return {
        9,
        10,
        11,
        2.0F,
        3.0F,
        4.0F,
        2,
        {},
        {},
        {}};
}

[[nodiscard]] wave3d::PhysicalPoint3D first_test_location() {
    return {4.4, 9.9, 17.6};
}

[[nodiscard]] wave3d::MomentTensorSource test_source(
    const wave3d::Grid3D& grid) {
    return wave3d::prepare_moment_tensor_source(
        grid,
        first_test_location(),
        0.4,
        {240.0, -480.0, 720.0, 960.0, -1200.0, 1440.0},
        {8.0, 0.1, 2.0});
}

void expect_weight_sum(
    const wave3d::Grid3D& grid,
    const wave3d::TrilinearStencil& stencil,
    wave3d::ElasticLattice expected_lattice,
    const std::string& name) {
    expect(stencil.lattice == expected_lattice, name + " lattice is wrong");
    wave3d::require_valid_trilinear_stencil(grid, stencil);
    expect(
        nearly_equal(wave3d::trilinear_weight_sum(stencil), 1.0, 0.0, 1.0e-14),
        name + " weights must sum to one");
    for (const auto& node : stencil.nodes) {
        expect(
            std::isfinite(node.weight) && node.weight >= 0.0 &&
                node.weight <= 1.0,
            name + " contains an invalid weight");
    }
}

void test_component_specific_stencil_preparation() {
    const auto grid = test_grid();
    const auto source = test_source(grid);
    const auto prepared =
        wave3d::prepare_moment_tensor_source_stencils(grid, source);

    expect_weight_sum(
        grid, prepared.sxx, wave3d::ElasticLattice::Integer, "sxx");
    expect_weight_sum(
        grid, prepared.syy, wave3d::ElasticLattice::Integer, "syy");
    expect_weight_sum(
        grid, prepared.szz, wave3d::ElasticLattice::Integer, "szz");
    expect_weight_sum(
        grid, prepared.sxy, wave3d::ElasticLattice::XYHalf, "sxy");
    expect_weight_sum(
        grid, prepared.sxz, wave3d::ElasticLattice::XZHalf, "sxz");
    expect_weight_sum(
        grid, prepared.syz, wave3d::ElasticLattice::YZHalf, "syz");

    expect(
        prepared.sxx.nodes.front().linear_index !=
            prepared.sxy.nodes.front().linear_index,
        "normal and xy stress must not reuse one array stencil");
    expect(
        prepared.sxx.nodes.front().linear_index !=
            prepared.sxz.nodes.front().linear_index,
        "normal and xz stress must not reuse one array stencil");
    expect(
        prepared.sxx.nodes.front().linear_index !=
            prepared.syz.nodes.front().linear_index,
        "normal and yz stress must not reuse one array stencil");

    const auto receiver_set = wave3d::prepare_receiver_set(
        grid,
        std::vector<wave3d::PhysicalPoint3D>{first_test_location()});
    const auto receivers = wave3d::prepare_receiver_stencils(grid, receiver_set);
    const auto& receiver = receivers.receivers.front();
    expect_weight_sum(
        grid, receiver.vx, wave3d::ElasticLattice::XHalf, "vx");
    expect_weight_sum(
        grid, receiver.vy, wave3d::ElasticLattice::YHalf, "vy");
    expect_weight_sum(
        grid, receiver.vz, wave3d::ElasticLattice::ZHalf, "vz");
    expect(
        receiver.vx.nodes.front().linear_index !=
            receiver.vy.nodes.front().linear_index &&
            receiver.vx.nodes.front().linear_index !=
                receiver.vz.nodes.front().linear_index &&
            receiver.vy.nodes.front().linear_index !=
                receiver.vz.nodes.front().linear_index,
        "vx, vy, and vz must use separate staggered stencils");
}

[[nodiscard]] double sum_field(const std::vector<float>& field) {
    double result = 0.0;
    for (const float value : field) {
        result += static_cast<double>(value);
    }
    return result;
}

void expect_integrated_increment(
    const std::vector<float>& field,
    double cell_volume_m3,
    double expected_pa_m3,
    const std::string& name) {
    const double actual = sum_field(field) * cell_volume_m3;
    expect(
        nearly_equal(actual, expected_pa_m3),
        name + " volume integral does not conserve -dt*Mij*q");
}

void test_source_conservation_sign_and_time() {
    const auto grid = test_grid();
    const auto source = test_source(grid);
    const auto prepared =
        wave3d::prepare_moment_tensor_source_stencils(grid, source);
    wave3d::ElasticWavefield wavefield(grid);

    constexpr double dt_s = 0.125;
    constexpr std::size_t step_index_n = 4;
    const double stress_time_s = static_cast<double>(step_index_n) * dt_s;
    expect(
        nearly_equal(
            stress_time_s,
            source.origin_time_s + source.wavelet.peak_delay_s,
            0.0,
            1.0e-15),
        "test source peak must coincide with t_n");
    const double q_s_inv = wave3d::source_time_value(source, stress_time_s);
    expect(
        q_s_inv == source.wavelet.peak_rate_s_inv,
        "source must be sampled at the requested stress time t_n");
    wave3d::inject_moment_tensor_source(
        wavefield,
        prepared,
        step_index_n,
        dt_s);

    const double cell_volume_m3 =
        static_cast<double>(grid.dx_m) * static_cast<double>(grid.dy_m) *
        static_cast<double>(grid.dz_m);
    expect_integrated_increment(
        wavefield.sxx_pa,
        cell_volume_m3,
        -dt_s * source.moment.m_xx_nm * q_s_inv,
        "sxx");
    expect_integrated_increment(
        wavefield.syy_pa,
        cell_volume_m3,
        -dt_s * source.moment.m_yy_nm * q_s_inv,
        "syy");
    expect_integrated_increment(
        wavefield.szz_pa,
        cell_volume_m3,
        -dt_s * source.moment.m_zz_nm * q_s_inv,
        "szz");
    expect_integrated_increment(
        wavefield.sxy_pa,
        cell_volume_m3,
        -dt_s * source.moment.m_xy_nm * q_s_inv,
        "sxy");
    expect_integrated_increment(
        wavefield.sxz_pa,
        cell_volume_m3,
        -dt_s * source.moment.m_xz_nm * q_s_inv,
        "sxz");
    expect_integrated_increment(
        wavefield.syz_pa,
        cell_volume_m3,
        -dt_s * source.moment.m_yz_nm * q_s_inv,
        "syz");

    for (const auto& node : prepared.sxx.nodes) {
        expect(
            wavefield.sxx_pa[node.linear_index] < 0.0F,
            "positive Mxx and q must produce tension-positive negative stress");
    }
    for (const auto& node : prepared.syy.nodes) {
        expect(
            wavefield.syy_pa[node.linear_index] > 0.0F,
            "negative Myy and positive q must produce positive stress");
    }
    expect(
        sum_field(wavefield.vx_m_s) == 0.0 &&
            sum_field(wavefield.vy_m_s) == 0.0 &&
            sum_field(wavefield.vz_m_s) == 0.0,
        "stress source injection must not change particle velocity");

    wave3d::ElasticWavefield before_origin(grid);
    wave3d::inject_moment_tensor_source(
        before_origin,
        prepared,
        0,
        dt_s);
    expect(
        sum_field(before_origin.sxx_pa) == 0.0 &&
            sum_field(before_origin.sxy_pa) == 0.0,
        "a source sampled before its origin time must inject zero");
}

void test_unavailable_support_is_rejected() {
    const wave3d::Grid3D grid{
        5, 5, 5, 1.0F, 1.0F, 1.0F, 0, {}, {}, {}};
    const auto source = wave3d::prepare_moment_tensor_source(
        grid,
        {0.0, 0.0, 0.0},
        0.0,
        wave3d::isotropic_explosion(1.0),
        {10.0, 0.1, 1.0});
    expect_throws<std::out_of_range>(
        [&] {
            static_cast<void>(
                wave3d::prepare_moment_tensor_source_stencils(grid, source));
        },
        "source preparation must reject unavailable staggered support");

    const auto receivers = wave3d::prepare_receiver_set(
        grid,
        std::vector<wave3d::PhysicalPoint3D>{{0.0, 0.0, 0.0}});
    expect_throws<std::out_of_range>(
        [&] {
            static_cast<void>(wave3d::prepare_receiver_stencils(grid, receivers));
        },
        "receiver preparation must reject unavailable staggered support");

    const auto upper_point = wave3d::physical_domain_max(grid);
    expect_throws<std::out_of_range>(
        [&] {
            static_cast<void>(wave3d::prepare_trilinear_stencil(
                grid,
                upper_point,
                wave3d::ElasticLattice::Integer));
        },
        "preparation must not clip or renormalize an upper-edge stencil");
}

struct AffineFunction {
    double x_coefficient{0.0};
    double y_coefficient{0.0};
    double z_coefficient{0.0};
    double constant{0.0};
};

[[nodiscard]] double evaluate(
    const AffineFunction& function,
    const wave3d::PhysicalPoint3D& point) {
    return function.x_coefficient * point.x_m +
           function.y_coefficient * point.y_m +
           function.z_coefficient * point.z_m + function.constant;
}

void fill_affine_lattice(
    std::vector<float>& field,
    const wave3d::Grid3D& grid,
    wave3d::ElasticLattice lattice,
    const AffineFunction& function) {
    const auto offset = wave3d::lattice_offset(lattice);
    for (std::size_t z = 0; z < grid.allocated_nz(); ++z) {
        const double z_m =
            (static_cast<double>(z) -
                 static_cast<double>(grid.physical_origin_z()) +
             offset.z) *
            static_cast<double>(grid.dz_m);
        for (std::size_t y = 0; y < grid.allocated_ny(); ++y) {
            const double y_m =
                (static_cast<double>(y) -
                     static_cast<double>(grid.physical_origin_y()) +
                 offset.y) *
                static_cast<double>(grid.dy_m);
            for (std::size_t x = 0; x < grid.allocated_nx(); ++x) {
                const double x_m =
                    (static_cast<double>(x) -
                         static_cast<double>(grid.physical_origin_x()) +
                     offset.x) *
                    static_cast<double>(grid.dx_m);
                field[grid.linear_index(x, y, z)] = static_cast<float>(
                    evaluate(function, {x_m, y_m, z_m}));
            }
        }
    }
}

void test_receiver_interpolation_and_time_label() {
    const auto grid = test_grid();
    const std::vector<wave3d::PhysicalPoint3D> locations{
        first_test_location(),
        {11.4, 15.6, 27.2}};
    const auto receiver_set = wave3d::prepare_receiver_set(grid, locations);
    const auto prepared = wave3d::prepare_receiver_stencils(grid, receiver_set);
    wave3d::ElasticWavefield wavefield(grid);

    const AffineFunction vx{2.0, 3.0, 4.0, 1.0};
    const AffineFunction vy{0.5, -1.5, 2.5, -5.0};
    const AffineFunction vz{-3.0, 4.0, -0.25, 7.0};
    fill_affine_lattice(
        wavefield.vx_m_s,
        grid,
        wave3d::ElasticLattice::XHalf,
        vx);
    fill_affine_lattice(
        wavefield.vy_m_s,
        grid,
        wave3d::ElasticLattice::YHalf,
        vy);
    fill_affine_lattice(
        wavefield.vz_m_s,
        grid,
        wave3d::ElasticLattice::ZHalf,
        vz);

    std::vector<wave3d::ReceiverVelocitySample> samples(locations.size());
    const auto* const original_data = samples.data();
    const auto original_capacity = samples.capacity();
    constexpr std::size_t step_index_n = 4;
    constexpr double dt_s = 0.002;
    wave3d::sample_receivers_after_velocity_step(
        wavefield,
        prepared,
        step_index_n,
        dt_s,
        samples);

    expect(
        samples.data() == original_data && samples.capacity() == original_capacity,
        "receiver sampling must not allocate or resize its output");
    for (std::size_t receiver_number = 0;
         receiver_number < locations.size();
         ++receiver_number) {
        const auto& sample = samples[receiver_number];
        expect(
            nearly_equal(sample.time_s, 0.010, 0.0, 1.0e-15),
            "receiver sample after step n must be labeled (n+1)*dt");
        expect(
            nearly_equal(sample.vx_m_s, evaluate(vx, locations[receiver_number])),
            "vx trilinear interpolation did not recover an affine field");
        expect(
            nearly_equal(sample.vy_m_s, evaluate(vy, locations[receiver_number])),
            "vy trilinear interpolation did not recover an affine field");
        expect(
            nearly_equal(sample.vz_m_s, evaluate(vz, locations[receiver_number])),
            "vz trilinear interpolation did not recover an affine field");
    }
}

void test_runtime_validation() {
    const auto grid = test_grid();
    const auto source = test_source(grid);
    auto prepared_source =
        wave3d::prepare_moment_tensor_source_stencils(grid, source);
    wave3d::ElasticWavefield wavefield(grid);

    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::inject_moment_tensor_source(
                wavefield, prepared_source, 0, 0.0);
        },
        "source injection must reject a zero time step");
    auto malformed_source = prepared_source;
    malformed_source.sxy.lattice = wave3d::ElasticLattice::Integer;
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::inject_moment_tensor_source(
                wavefield, malformed_source, 0, 0.001);
        },
        "source injection must reject a wrong component lattice");

    auto duplicate_node_source = prepared_source;
    duplicate_node_source.sxx.nodes[1] = duplicate_node_source.sxx.nodes[0];
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::inject_moment_tensor_source(
                wavefield, duplicate_node_source, 0, 0.001);
        },
        "source injection must reject a duplicate interpolation node");

    wave3d::ElasticWavefield malformed_wavefield(grid);
    malformed_wavefield.szz_pa.pop_back();
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::inject_moment_tensor_source(
                malformed_wavefield, prepared_source, 0, 0.001);
        },
        "source injection must reject a malformed wavefield layout");

    expect_throws<std::overflow_error>(
        [&] {
            wave3d::inject_moment_tensor_source(
                wavefield,
                prepared_source,
                std::numeric_limits<std::size_t>::max(),
                std::numeric_limits<double>::max());
        },
        "source injection must reject a non-finite t_n");

    const auto overflowing_source = wave3d::prepare_moment_tensor_source(
        grid,
        first_test_location(),
        0.4,
        {240.0, 0.0, 0.0, 0.0, 0.0, -1.0e42},
        {8.0, 0.1, 2.0});
    const auto overflowing_prepared =
        wave3d::prepare_moment_tensor_source_stencils(
            grid,
            overflowing_source);
    wave3d::ElasticWavefield transactional_wavefield(grid);
    expect_throws<std::overflow_error>(
        [&] {
            wave3d::inject_moment_tensor_source(
                transactional_wavefield,
                overflowing_prepared,
                4,
                0.125);
        },
        "source injection must reject a float32 overflow");
    expect(
        sum_field(transactional_wavefield.sxx_pa) == 0.0 &&
            sum_field(transactional_wavefield.syz_pa) == 0.0,
        "failed source injection must not partially modify stress fields");

    auto other_grid = grid;
    other_grid.dx_m = 2.5F;
    wave3d::ElasticWavefield other_wavefield(other_grid);
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::inject_moment_tensor_source(
                other_wavefield, prepared_source, 0, 0.001);
        },
        "source injection must reject a wavefield grid mismatch");

    const auto receiver_set = wave3d::prepare_receiver_set(
        grid,
        std::vector<wave3d::PhysicalPoint3D>{first_test_location()});
    const auto receivers = wave3d::prepare_receiver_stencils(grid, receiver_set);
    std::vector<wave3d::ReceiverVelocitySample> wrong_size;
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::sample_receivers_after_velocity_step(
                wavefield, receivers, 0, 0.001, wrong_size);
        },
        "receiver sampling must require preallocated output");

    std::vector<wave3d::ReceiverVelocitySample> samples(1);
    expect_throws<std::overflow_error>(
        [&] {
            wave3d::sample_receivers_after_velocity_step(
                wavefield,
                receivers,
                std::numeric_limits<std::size_t>::max(),
                0.001,
                samples);
        },
        "receiver sampling must reject step-index overflow");
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::sample_receivers_after_velocity_step(
                wavefield,
                receivers,
                0,
                std::numeric_limits<double>::quiet_NaN(),
                samples);
        },
        "receiver sampling must reject a non-finite time step");

    wave3d::ElasticWavefield malformed_receiver_wavefield(grid);
    malformed_receiver_wavefield.vy_m_s.pop_back();
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::sample_receivers_after_velocity_step(
                malformed_receiver_wavefield,
                receivers,
                0,
                0.001,
                samples);
        },
        "receiver sampling must reject a malformed wavefield layout");

    const auto sampled_index =
        receivers.receivers.front().vx.nodes.front().linear_index;
    wavefield.vx_m_s[sampled_index] =
        std::numeric_limits<float>::quiet_NaN();
    expect_throws<std::invalid_argument>(
        [&] {
            wave3d::sample_receivers_after_velocity_step(
                wavefield, receivers, 0, 0.001, samples);
        },
        "receiver sampling must reject non-finite wavefield input");
}

} // namespace

int main() {
    try {
        test_component_specific_stencil_preparation();
        test_source_conservation_sign_and_time();
        test_unavailable_support_is_rejected();
        test_receiver_interpolation_and_time_label();
        test_runtime_validation();
    } catch (const std::exception& error) {
        std::cerr << "staggered acquisition test failure: " << error.what()
                  << '\n';
        return 1;
    }

    std::cout << "staggered acquisition tests passed\n";
    return 0;
}
