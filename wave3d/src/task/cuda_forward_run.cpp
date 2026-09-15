#include "wave3d/task/cuda_forward_run.hpp"

#include "wave3d/acquisition/moment_source_injector.hpp"
#include "wave3d/acquisition/receiver.hpp"
#include "wave3d/acquisition/receiver_sampler.hpp"
#include "wave3d/boundary/cpml.hpp"
#include "wave3d/boundary/free_surface.hpp"
#include "wave3d/core/forward_memory_plan.hpp"
#include "wave3d/cuda/cpml.hpp"
#include "wave3d/cuda/cuda_error.hpp"
#include "wave3d/cuda/device_info.hpp"
#include "wave3d/cuda/forward_session.hpp"
#include "wave3d/io/hdf5.hpp"
#include "wave3d/io/segy.hpp"
#include "wave3d/io/yaml_config.hpp"
#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/model/physical_model.hpp"
#include "wave3d/numerics/elastic_validation.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace wave3d::task {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

[[nodiscard]] std::filesystem::path resolve_from_configuration(
    const std::filesystem::path& configuration_directory,
    const std::string& value) {
    const std::filesystem::path path(value);
    return (path.is_absolute() ? path : configuration_directory / path)
        .lexically_normal();
}

[[nodiscard]] MaterialExtrema model_extrema(
    const PhysicalModel& model) {
    const auto extrema = physical_model_extrema(model);
    return {
        extrema.minimum.vp_m_s,
        extrema.maximum.vp_m_s,
        extrema.minimum.vs_m_s,
        extrema.maximum.vs_m_s,
        extrema.minimum.density_kg_m3,
        extrema.maximum.density_kg_m3};
}

void require_matching_extrema(
    const MaterialExtrema& declared,
    const MaterialExtrema& actual) {
    if (declared.min_vp_m_s != actual.min_vp_m_s ||
        declared.max_vp_m_s != actual.max_vp_m_s ||
        declared.min_vs_m_s != actual.min_vs_m_s ||
        declared.max_vs_m_s != actual.max_vs_m_s ||
        declared.min_density_kg_m3 != actual.min_density_kg_m3 ||
        declared.max_density_kg_m3 != actual.max_density_kg_m3) {
        throw std::invalid_argument(
            "YAML material_extrema do not match the loaded HDF5 model");
    }
}

void require_output_directory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    const bool is_directory =
        !error && std::filesystem::is_directory(path, error);
    if (error || !is_directory) {
        throw std::runtime_error(
            "cannot create SEG-Y output directory: " + path.string());
    }
}

} // namespace

CudaForwardRunReport run_cuda_forward_from_yaml(
    const std::string& configuration_path) {
    const auto input_start = Clock::now();
    const auto absolute_configuration =
        std::filesystem::absolute(configuration_path).lexically_normal();
    auto configuration =
        io::load_yaml_run_configuration(absolute_configuration.string());
    const auto configuration_directory = absolute_configuration.parent_path();
    const auto model_path = resolve_from_configuration(
        configuration_directory, configuration.model_hdf5_path);
    const auto output_directory = resolve_from_configuration(
        configuration_directory, configuration.output_directory);
    configuration.model_hdf5_path = model_path.string();
    configuration.output_directory = output_directory.string();

    const auto model = io::read_hdf5_model(model_path.string());
    if (!same_grid_geometry(model.grid, configuration.simulation.grid)) {
        throw std::invalid_argument(
            "YAML grid does not match the loaded HDF5 model grid");
    }
    const auto actual_extrema = model_extrema(model);
    require_matching_extrema(
        configuration.simulation.material, actual_extrema);
    const auto numerical_errors =
        validate_staggered_elastic(configuration.simulation);
    if (!numerical_errors.empty()) {
        throw std::invalid_argument(numerical_errors.front());
    }

    const auto steps = configuration.simulation.time.step_count();
    io::require_segy_rev1_sample_axis(
        steps, configuration.simulation.time.dt_s);
    const auto input_end = Clock::now();

    const auto device = cuda::query_device(0);
    ForwardMemoryPlanRequest memory_request{};
    memory_request.grid = model.grid;
    memory_request.receiver_count = configuration.receiver_coordinates_m.size();
    memory_request.time_step_count = steps;
    memory_request.available_device_bytes = device.free_memory_bytes;
    memory_request.boundary_kind =
        ForwardMemoryPlanRequest::BoundaryKind::Cpml;
    const auto memory_plan =
        make_elastic_forward_memory_plan(memory_request);
    memory_plan.require_fit();

    require_output_directory(output_directory);
    const auto output_path = output_directory / "record.sgy";

    const auto setup_start = Clock::now();
    const auto coefficients = prepare_elastic_coefficients(model);
    const auto prepared_source = prepare_moment_tensor_source_stencils(
        model.grid, configuration.source);
    const auto receiver_set = prepare_receiver_set(
        model.grid, configuration.receiver_coordinates_m);
    const auto prepared_receivers = prepare_receiver_stencils(
        model.grid, receiver_set);
    const bool free_surface =
        configuration.simulation.top_boundary == TopBoundary::FreeSurface;
    const CpmlParameters cpml_parameters{
        configuration.simulation.time.dt_s,
        static_cast<double>(actual_extrema.max_vp_m_s),
        configuration.source.wavelet.dominant_frequency_hz,
        1.0e-3,
        2.0,
        1.0,
        {true, true, true, true, !free_surface, true}};
    const auto cpml_profile = prepare_cpml_profile(
        model.grid, cpml_parameters);

    std::optional<TractionFreeSurface> surface;
    if (free_surface) {
        surface = prepare_traction_free_surface(model.grid);
    }
    cuda::CudaForwardSession session(
        coefficients,
        prepared_source,
        prepared_receivers,
        cpml_profile,
        surface,
        steps);
    const auto setup_end = Clock::now();

    const auto propagation_start = Clock::now();
    static_cast<void>(session.advance_remaining());
    const auto propagation_end = Clock::now();

    const auto trace_value_count = detail::checked_size_product(
        configuration.receiver_coordinates_m.size(),
        steps,
        "production receiver trace size overflow");
    std::vector<float> vx(trace_value_count);
    std::vector<float> vy(trace_value_count);
    std::vector<float> vz(trace_value_count);
    const auto download_start = Clock::now();
    session.download_receiver_traces(vx, vy, vz);
    const auto download_end = Clock::now();

    const io::ThreeComponentTraces host_traces{
        configuration.receiver_coordinates_m.size(),
        steps,
        configuration.simulation.time.dt_s,
        configuration.receiver_coordinates_m,
        configuration.source,
        std::move(vx),
        std::move(vy),
        std::move(vz)};
    const auto write_start = Clock::now();
    io::write_segy(output_path.string(), host_traces);
    const auto write_end = Clock::now();

    return {
        absolute_configuration.string(),
        model_path.string(),
        output_path.string(),
        device.name,
        model.grid.physical_cell_count(),
        model.grid.allocated_cell_count(),
        configuration.receiver_coordinates_m.size(),
        steps,
        memory_plan.required_bytes,
        memory_plan.budget_bytes,
        milliseconds(input_end - input_start),
        milliseconds(setup_end - setup_start),
        milliseconds(propagation_end - propagation_start),
        milliseconds(download_end - download_start),
        milliseconds(write_end - write_start)};
}

} // namespace wave3d::task
