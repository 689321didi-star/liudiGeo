#include "wave3d/io/yaml_config.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wave3d::io {
namespace {

[[nodiscard]] AxisBoundary parse_boundary(const YAML::Node& node) {
    return {
        node["lower_absorbing"].as<std::size_t>(),
        node["upper_absorbing"].as<std::size_t>()};
}

void emit_boundary(YAML::Emitter& output, const AxisBoundary& boundary) {
    output << YAML::BeginMap
           << YAML::Key << "lower_absorbing" << YAML::Value
           << boundary.lower_absorbing
           << YAML::Key << "upper_absorbing" << YAML::Value
           << boundary.upper_absorbing << YAML::EndMap;
}

} // namespace

ForwardRunConfiguration load_yaml_run_configuration(const std::string& path) {
    try {
        const YAML::Node root = YAML::LoadFile(path);
        if (!root["schema"] ||
            root["schema"].as<std::string>() != "wave3d.forward.v2") {
            throw std::invalid_argument(
                "YAML run configuration schema must be wave3d.forward.v2");
        }
        const auto grid_node = root["grid"];
        Grid3D grid{
            grid_node["nx"].as<std::size_t>(),
            grid_node["ny"].as<std::size_t>(),
            grid_node["nz"].as<std::size_t>(),
            grid_node["dx_m"].as<float>(),
            grid_node["dy_m"].as<float>(),
            grid_node["dz_m"].as<float>(),
            grid_node["halo"].as<std::size_t>(),
            parse_boundary(grid_node["x_boundary"]),
            parse_boundary(grid_node["y_boundary"]),
            parse_boundary(grid_node["z_boundary"])};

        const auto material_node = root["material_extrema"];
        const std::string top_text = root["top_boundary"].as<std::string>();
        TopBoundary top_boundary{};
        if (top_text == "free_surface") {
            top_boundary = TopBoundary::FreeSurface;
        } else if (top_text == "absorbing") {
            top_boundary = TopBoundary::Absorbing;
        } else {
            throw std::invalid_argument(
                "top_boundary must be free_surface or absorbing");
        }

        SimulationConfig simulation{};
        simulation.grid = grid;
        simulation.time = {
            root["time"]["dt_s"].as<double>(),
            root["time"]["total_time_s"].as<double>()};
        simulation.material = {
            material_node["min_vp_m_s"].as<float>(),
            material_node["max_vp_m_s"].as<float>(),
            material_node["min_vs_m_s"].as<float>(),
            material_node["max_vs_m_s"].as<float>(),
            material_node["min_density_kg_m3"].as<float>(),
            material_node["max_density_kg_m3"].as<float>()};
        simulation.top_boundary = top_boundary;
        simulation.numerics = {
            root["numerics"]["cfl_safety_factor"].as<double>(),
            root["numerics"]["design_frequency_hz"].as<double>()};

        const auto source_node = root["source"];
        const auto location = source_node["location_m"];
        const auto tensor = source_node["moment_tensor_nm"];
        const auto wavelet = source_node["ricker"];
        const auto source = prepare_moment_tensor_source(
            grid,
            {location[0].as<double>(),
             location[1].as<double>(),
             location[2].as<double>()},
            source_node["origin_time_s"].as<double>(),
            {tensor[0].as<double>(),
             tensor[1].as<double>(),
             tensor[2].as<double>(),
             tensor[3].as<double>(),
             tensor[4].as<double>(),
             tensor[5].as<double>()},
            {wavelet["dominant_frequency_hz"].as<double>(),
             wavelet["peak_delay_s"].as<double>(),
             wavelet["peak_rate_s_inv"].as<double>()});

        std::vector<PhysicalPoint3D> receivers;
        for (const auto& receiver : root["receivers_m"]) {
            if (!receiver.IsSequence() || receiver.size() != 3) {
                throw std::invalid_argument(
                    "each YAML receiver must contain x,y,z");
            }
            receivers.push_back({
                receiver[0].as<double>(),
                receiver[1].as<double>(),
                receiver[2].as<double>()});
        }
        ForwardRunConfiguration result{
            simulation,
            source,
            std::move(receivers),
            root["model_hdf5_path"].as<std::string>(),
            root["output_directory"].as<std::string>()};
        require_valid_run_configuration(result);
        return result;
    } catch (const YAML::Exception& error) {
        throw std::invalid_argument(
            "invalid YAML run configuration " + path + ": " + error.what());
    }
}

std::string resolved_yaml(const ForwardRunConfiguration& configuration) {
    require_valid_run_configuration(configuration);
    const auto& simulation = configuration.simulation;
    const auto& grid = simulation.grid;
    const auto& source = configuration.source;
    YAML::Emitter output;
    output.SetDoublePrecision(17);
    output.SetFloatPrecision(9);
    output << YAML::BeginMap
           << YAML::Key << "schema" << YAML::Value << "wave3d.forward.v2"
           << YAML::Key << "coordinate_convention" << YAML::Value
           << std::string(coordinate_convention())
           << YAML::Key << "volume_axes" << YAML::Value << "z,y,x"
           << YAML::Key << "grid" << YAML::Value << YAML::BeginMap
           << YAML::Key << "nx" << YAML::Value << grid.nx
           << YAML::Key << "ny" << YAML::Value << grid.ny
           << YAML::Key << "nz" << YAML::Value << grid.nz
           << YAML::Key << "dx_m" << YAML::Value << grid.dx_m
           << YAML::Key << "dy_m" << YAML::Value << grid.dy_m
           << YAML::Key << "dz_m" << YAML::Value << grid.dz_m
           << YAML::Key << "halo" << YAML::Value << grid.halo
           << YAML::Key << "x_boundary" << YAML::Value;
    emit_boundary(output, grid.x_boundary);
    output << YAML::Key << "y_boundary" << YAML::Value;
    emit_boundary(output, grid.y_boundary);
    output << YAML::Key << "z_boundary" << YAML::Value;
    emit_boundary(output, grid.z_boundary);
    output << YAML::EndMap
           << YAML::Key << "time" << YAML::Value << YAML::BeginMap
           << YAML::Key << "dt_s" << YAML::Value << simulation.time.dt_s
           << YAML::Key << "total_time_s" << YAML::Value
           << simulation.time.total_time_s << YAML::EndMap
           << YAML::Key << "numerics" << YAML::Value << YAML::BeginMap
           << YAML::Key << "cfl_safety_factor" << YAML::Value
           << simulation.numerics.cfl_safety_factor
           << YAML::Key << "design_frequency_hz" << YAML::Value
           << simulation.numerics.design_frequency_hz << YAML::EndMap
           << YAML::Key << "top_boundary" << YAML::Value
           << (simulation.top_boundary == TopBoundary::FreeSurface
                   ? "free_surface"
                   : "absorbing")
           << YAML::Key << "material_extrema" << YAML::Value
           << YAML::BeginMap
           << YAML::Key << "min_vp_m_s" << YAML::Value
           << simulation.material.min_vp_m_s
           << YAML::Key << "max_vp_m_s" << YAML::Value
           << simulation.material.max_vp_m_s
           << YAML::Key << "min_vs_m_s" << YAML::Value
           << simulation.material.min_vs_m_s
           << YAML::Key << "max_vs_m_s" << YAML::Value
           << simulation.material.max_vs_m_s
           << YAML::Key << "min_density_kg_m3" << YAML::Value
           << simulation.material.min_density_kg_m3
           << YAML::Key << "max_density_kg_m3" << YAML::Value
           << simulation.material.max_density_kg_m3 << YAML::EndMap
           << YAML::Key << "source" << YAML::Value << YAML::BeginMap
           << YAML::Key << "location_m" << YAML::Value << YAML::Flow
           << YAML::BeginSeq << source.physical_location.x_m
           << source.physical_location.y_m << source.physical_location.z_m
           << YAML::EndSeq
           << YAML::Key << "origin_time_s" << YAML::Value
           << source.origin_time_s
           << YAML::Key << "moment_tensor_nm" << YAML::Value << YAML::Flow
           << YAML::BeginSeq << source.moment.m_xx_nm << source.moment.m_yy_nm
           << source.moment.m_zz_nm << source.moment.m_xy_nm
           << source.moment.m_xz_nm << source.moment.m_yz_nm << YAML::EndSeq
           << YAML::Key << "ricker" << YAML::Value << YAML::BeginMap
           << YAML::Key << "dominant_frequency_hz" << YAML::Value
           << source.wavelet.dominant_frequency_hz
           << YAML::Key << "peak_delay_s" << YAML::Value
           << source.wavelet.peak_delay_s
           << YAML::Key << "peak_rate_s_inv" << YAML::Value
           << source.wavelet.peak_rate_s_inv << YAML::EndMap << YAML::EndMap
           << YAML::Key << "receivers_m" << YAML::Value << YAML::BeginSeq;
    for (const auto& receiver : configuration.receiver_coordinates_m) {
        output << YAML::Flow << YAML::BeginSeq << receiver.x_m << receiver.y_m
               << receiver.z_m << YAML::EndSeq;
    }
    output << YAML::EndSeq
           << YAML::Key << "model_hdf5_path" << YAML::Value
           << configuration.model_hdf5_path
           << YAML::Key << "output_directory" << YAML::Value
           << configuration.output_directory << YAML::EndMap;
    if (!output.good()) {
        throw std::runtime_error("failed to emit resolved YAML configuration");
    }
    return output.c_str();
}

void write_resolved_yaml(
    const std::string& path,
    const ForwardRunConfiguration& configuration) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open resolved YAML output: " + path);
    }
    output << resolved_yaml(configuration) << '\n';
    if (!output) {
        throw std::runtime_error("failed while writing resolved YAML: " + path);
    }
}

} // namespace wave3d::io
