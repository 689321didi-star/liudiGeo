#include "wave3d/acquisition/receiver.hpp"
#include "wave3d/acquisition/source.hpp"
#include "wave3d/io/hdf5.hpp"
#include "wave3d/io/overthrust_mat.hpp"
#include "wave3d/io/yaml_config.hpp"
#include "wave3d/model/derived_overthrust.hpp"
#include "wave3d/model/physical_model.hpp"
#include "wave3d/numerics/elastic_validation.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr wave3d::Grid3D audited_source_grid{
    801, 801, 187, 25.0F, 25.0F, 25.0F, 0, {}, {}, {}};

constexpr wave3d::PhysicalVolumeWindow3D audited_crop{
    154, 153, 0, 200, 200, 187};

constexpr wave3d::OutputStorageGeometry production_storage{
    6, {20, 20}, {20, 20}, {0, 20}};

// A fixed subset of the accepted crop, centered horizontally on the
// production source area and retaining the free surface.
constexpr wave3d::PhysicalVolumeWindow3D smoke_crop{
    222, 221, 0, 64, 64, 64};

constexpr wave3d::OutputStorageGeometry smoke_storage{
    6, {10, 10}, {10, 10}, {0, 10}};

[[nodiscard]] std::filesystem::path normalized_absolute(
    const std::filesystem::path& path) {
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(
        std::filesystem::absolute(path), error);
    if (error) {
        throw std::runtime_error(
            "cannot normalize path " + path.string() + ": " +
            error.message());
    }
    return normalized;
}

void require_distinct_paths(
    const std::filesystem::path& input,
    const std::filesystem::path& hdf5,
    const std::filesystem::path& yaml) {
    const auto normalized_input = normalized_absolute(input);
    const auto normalized_hdf5 = normalized_absolute(hdf5);
    const auto normalized_yaml = normalized_absolute(yaml);
    if (normalized_input == normalized_hdf5 ||
        normalized_input == normalized_yaml ||
        normalized_hdf5 == normalized_yaml) {
        throw std::invalid_argument(
            "input MAT, output HDF5, and output YAML paths must be distinct");
    }
}

[[nodiscard]] wave3d::MaterialExtrema extrema_for(
    const wave3d::PhysicalModel& model) {
    const auto extrema = wave3d::physical_model_extrema(model);
    return {
        extrema.minimum.vp_m_s,
        extrema.maximum.vp_m_s,
        extrema.minimum.vs_m_s,
        extrema.maximum.vs_m_s,
        extrema.minimum.density_kg_m3,
        extrema.maximum.density_kg_m3};
}

void require_audited_extrema(const wave3d::MaterialExtrema& extrema) {
    const wave3d::MaterialExtrema expected{
        2445.75927734375F,
        6000.0F,
        1412.059814453125F,
        3464.1015625F,
        2180.043212890625F,
        2728.346435546875F};
    if (extrema.min_vp_m_s != expected.min_vp_m_s ||
        extrema.max_vp_m_s != expected.max_vp_m_s ||
        extrema.min_vs_m_s != expected.min_vs_m_s ||
        extrema.max_vs_m_s != expected.max_vs_m_s ||
        extrema.min_density_kg_m3 != expected.min_density_kg_m3 ||
        extrema.max_density_kg_m3 != expected.max_density_kg_m3) {
        throw std::invalid_argument(
            "derived model extrema do not match the audited Overthrust oracle");
    }
}

[[nodiscard]] std::string model_path_from_yaml(
    const std::filesystem::path& hdf5_path,
    const std::filesystem::path& yaml_path) {
    const auto hdf5 = normalized_absolute(hdf5_path);
    const auto yaml_parent = normalized_absolute(yaml_path).parent_path();
    std::error_code error;
    const auto relative = std::filesystem::relative(hdf5, yaml_parent, error);
    return error ? hdf5.string() : relative.string();
}

[[nodiscard]] wave3d::io::ForwardRunConfiguration make_configuration(
    const wave3d::PhysicalModel& model,
    const std::filesystem::path& hdf5_path,
    const std::filesystem::path& yaml_path,
    bool smoke) {
    wave3d::SimulationConfig simulation{};
    simulation.grid = model.grid;
    simulation.time = {0.001, smoke ? 0.008 : 3.0};
    simulation.material = extrema_for(model);
    simulation.top_boundary = wave3d::TopBoundary::FreeSurface;
    simulation.numerics = {0.85, 9.0};

    const auto source = wave3d::prepare_moment_tensor_source(
        simulation.grid,
        smoke ? wave3d::PhysicalPoint3D{800.0, 800.0, 600.0}
              : wave3d::PhysicalPoint3D{2500.0, 2500.0, 1100.0},
        0.0,
        {0.0, 0.0, 0.0, 1.0e12, 0.0, 0.0},
        {3.0, 1.0 / 3.0, 1.0});
    const auto prepared_receivers = wave3d::make_regular_surface_receivers(
        simulation.grid,
        smoke ? wave3d::RegularSurfaceReceiverGrid{
                    400.0, 400.0, 400.0, 400.0, 3, 3}
              : wave3d::RegularSurfaceReceiverGrid{
                    500.0, 500.0, 400.0, 400.0, 11, 11});
    std::vector<wave3d::PhysicalPoint3D> receivers;
    receivers.reserve(prepared_receivers.receivers.size());
    for (const auto& receiver : prepared_receivers.receivers) {
        receivers.push_back(receiver.physical_location);
    }

    wave3d::io::ForwardRunConfiguration configuration{
        simulation,
        source,
        std::move(receivers),
        model_path_from_yaml(hdf5_path, yaml_path),
        smoke ? "overthrust_smoke_output" : "overthrust_output"};
    wave3d::io::require_valid_run_configuration(configuration);
    return configuration;
}

void require_model_round_trip(
    const wave3d::PhysicalModel& expected,
    const wave3d::PhysicalModel& actual) {
    if (!wave3d::same_grid_geometry(expected.grid, actual.grid) ||
        expected.vp_m_s != actual.vp_m_s ||
        expected.vs_m_s != actual.vs_m_s ||
        expected.density_kg_m3 != actual.density_kg_m3) {
        throw std::runtime_error(
            "canonical HDF5 round trip changed model geometry or values");
    }
}

void require_yaml_round_trip(
    const wave3d::io::ForwardRunConfiguration& expected,
    const wave3d::io::ForwardRunConfiguration& actual) {
    const auto& expected_material = expected.simulation.material;
    const auto& actual_material = actual.simulation.material;
    if (!wave3d::same_grid_geometry(
            expected.simulation.grid, actual.simulation.grid) ||
        expected.simulation.time.dt_s != actual.simulation.time.dt_s ||
        expected.simulation.time.total_time_s !=
            actual.simulation.time.total_time_s ||
        expected.simulation.numerics.cfl_safety_factor !=
            actual.simulation.numerics.cfl_safety_factor ||
        expected.simulation.numerics.design_frequency_hz !=
            actual.simulation.numerics.design_frequency_hz ||
        expected.simulation.top_boundary != actual.simulation.top_boundary ||
        expected_material.min_vp_m_s != actual_material.min_vp_m_s ||
        expected_material.max_vp_m_s != actual_material.max_vp_m_s ||
        expected_material.min_vs_m_s != actual_material.min_vs_m_s ||
        expected_material.max_vs_m_s != actual_material.max_vs_m_s ||
        expected_material.min_density_kg_m3 !=
            actual_material.min_density_kg_m3 ||
        expected_material.max_density_kg_m3 !=
            actual_material.max_density_kg_m3 ||
        expected.receiver_coordinates_m.size() !=
            actual.receiver_coordinates_m.size() ||
        expected.source.physical_location.x_m !=
            actual.source.physical_location.x_m ||
        expected.source.physical_location.y_m !=
            actual.source.physical_location.y_m ||
        expected.source.physical_location.z_m !=
            actual.source.physical_location.z_m ||
        expected.source.origin_time_s != actual.source.origin_time_s ||
        expected.source.moment.m_xx_nm != actual.source.moment.m_xx_nm ||
        expected.source.moment.m_yy_nm != actual.source.moment.m_yy_nm ||
        expected.source.moment.m_zz_nm != actual.source.moment.m_zz_nm ||
        expected.source.moment.m_xy_nm != actual.source.moment.m_xy_nm ||
        expected.source.moment.m_xz_nm != actual.source.moment.m_xz_nm ||
        expected.source.moment.m_yz_nm != actual.source.moment.m_yz_nm ||
        expected.source.wavelet.dominant_frequency_hz !=
            actual.source.wavelet.dominant_frequency_hz ||
        expected.source.wavelet.peak_delay_s !=
            actual.source.wavelet.peak_delay_s ||
        expected.source.wavelet.peak_rate_s_inv !=
            actual.source.wavelet.peak_rate_s_inv ||
        expected.model_hdf5_path != actual.model_hdf5_path ||
        expected.output_directory != actual.output_directory) {
        throw std::runtime_error(
            "resolved YAML round trip changed the production configuration");
    }
    for (std::size_t index = 0;
         index < expected.receiver_coordinates_m.size();
         ++index) {
        const auto& left = expected.receiver_coordinates_m[index];
        const auto& right = actual.receiver_coordinates_m[index];
        if (left.x_m != right.x_m || left.y_m != right.y_m ||
            left.z_m != right.z_m) {
            throw std::runtime_error(
                "resolved YAML round trip changed receiver coordinates");
        }
    }
}

void create_parent(const std::filesystem::path& path) {
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

} // namespace

int main(int argc, char** argv) {
    const bool smoke = argc == 5 && std::string(argv[1]) == "--smoke";
    if ((!smoke && argc != 4) || (argc == 5 && !smoke)) {
        std::cerr << "usage: wave3d_prepare_overthrust [--smoke] "
                     "INPUT.mat OUTPUT.h5 OUTPUT.yaml\n";
        return 2;
    }
    try {
        const int path_offset = smoke ? 2 : 1;
        const std::filesystem::path input_path(argv[path_offset]);
        const std::filesystem::path hdf5_path(argv[path_offset + 1]);
        const std::filesystem::path yaml_path(argv[path_offset + 2]);
        require_distinct_paths(input_path, hdf5_path, yaml_path);

        const auto decoded = wave3d::io::read_overthrust_mat_v5_vp_crop(
            input_path.string(),
            audited_source_grid,
            smoke ? smoke_crop : audited_crop);
        const auto model = wave3d::make_derived_overthrust_elastic_model(
            decoded.grid,
            decoded.vp_m_s,
            {0, 0, 0, decoded.grid.nx, decoded.grid.ny, decoded.grid.nz},
            smoke ? smoke_storage : production_storage);
        const auto extrema = extrema_for(model);
        if (!smoke) {
            require_audited_extrema(extrema);
        }
        const auto configuration =
            make_configuration(model, hdf5_path, yaml_path, smoke);
        const auto numerical =
            wave3d::elastic_numerical_report(configuration.simulation);

        // Materialize serialization before opening either output, so all
        // model/configuration validation precedes truncating an existing file.
        static_cast<void>(wave3d::io::resolved_yaml(configuration));
        create_parent(hdf5_path);
        create_parent(yaml_path);
        wave3d::io::write_hdf5_model(hdf5_path.string(), model);
        require_model_round_trip(
            model, wave3d::io::read_hdf5_model(hdf5_path.string()));
        wave3d::io::write_resolved_yaml(yaml_path.string(), configuration);
        require_yaml_round_trip(
            configuration,
            wave3d::io::load_yaml_run_configuration(yaml_path.string()));

        std::cout << std::setprecision(12)
                  << "prepared derived SEG/EAGE 3-D Overthrust "
                  << (smoke ? "smoke" : "production") << " benchmark\n"
                  << "shape_zyx=" << model.grid.nz << ',' << model.grid.ny
                  << ',' << model.grid.nx << '\n'
                  << "spacing_m=" << model.grid.dz_m << ',' << model.grid.dy_m
                  << ',' << model.grid.dx_m << '\n'
                  << "vp_m_s=" << extrema.min_vp_m_s << ','
                  << extrema.max_vp_m_s << '\n'
                  << "vs_m_s=" << extrema.min_vs_m_s << ','
                  << extrema.max_vs_m_s << '\n'
                  << "rho_kg_m3=" << extrema.min_density_kg_m3 << ','
                  << extrema.max_density_kg_m3 << '\n'
                  << "receivers="
                  << configuration.receiver_coordinates_m.size() << '\n'
                  << "steps=" << configuration.simulation.time.step_count()
                  << '\n'
                  << "cfl_dt_limit_s=" << numerical.cfl_dt_limit_s << '\n'
                  << "cfl_fraction=" << numerical.cfl_fraction << '\n'
                  << "minimum_shear_points_per_wavelength="
                  << *std::min_element(
                         numerical.shear_points_per_wavelength.begin(),
                         numerical.shear_points_per_wavelength.end())
                  << '\n'
                  << "hdf5=" << hdf5_path.string() << '\n'
                  << "yaml=" << yaml_path.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Overthrust preparation failed: " << error.what() << '\n';
        return 1;
    }
}
