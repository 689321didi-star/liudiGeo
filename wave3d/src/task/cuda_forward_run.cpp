#include "wave3d/task/cuda_forward_run.hpp"

#include "wave3d/acquisition/moment_source_injector.hpp"
#include "wave3d/acquisition/receiver.hpp"
#include "wave3d/acquisition/receiver_sampler.hpp"
#include "wave3d/boundary/cpml.hpp"
#include "wave3d/boundary/free_surface.hpp"
#include "wave3d/core/checked_size.hpp"
#include "wave3d/core/forward_memory_plan.hpp"
#include "wave3d/cuda/device_info.hpp"
#include "wave3d/cuda/cuda_error.hpp"
#include "wave3d/cuda/forward_session.hpp"
#include "wave3d/io/hdf5.hpp"
#include "wave3d/io/segy.hpp"
#include "wave3d/io/yaml_config.hpp"
#include "wave3d/model/elastic_coefficients.hpp"
#include "wave3d/model/physical_model.hpp"
#include "wave3d/numerics/elastic_validation.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
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

[[nodiscard]] MaterialExtrema model_extrema(const PhysicalModel& model) {
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

class CudaForwardJob::Impl final {
public:
    Impl(
        const std::string& configuration_path,
        bool enable_visualization)
        : visualization_enabled_(enable_visualization) {
        const auto input_start = Clock::now();
        absolute_configuration_ =
            std::filesystem::absolute(configuration_path).lexically_normal();
        configuration_ =
            io::load_yaml_run_configuration(absolute_configuration_.string());
        const auto configuration_directory = absolute_configuration_.parent_path();
        model_path_ = resolve_from_configuration(
            configuration_directory, configuration_.model_hdf5_path);
        output_directory_ = resolve_from_configuration(
            configuration_directory, configuration_.output_directory);
        configuration_.model_hdf5_path = model_path_.string();
        configuration_.output_directory = output_directory_.string();

        const auto model = io::read_hdf5_model(model_path_.string());
        if (!same_grid_geometry(model.grid, configuration_.simulation.grid)) {
            throw std::invalid_argument(
                "YAML grid does not match the loaded HDF5 model grid");
        }
        const auto actual_extrema = model_extrema(model);
        require_matching_extrema(
            configuration_.simulation.material, actual_extrema);
        const auto numerical_errors =
            validate_staggered_elastic(configuration_.simulation);
        if (!numerical_errors.empty()) {
            throw std::invalid_argument(numerical_errors.front());
        }

        const auto steps = configuration_.simulation.time.step_count();
        io::require_segy_rev1_sample_axis(
            steps, configuration_.simulation.time.dt_s);
        const auto input_end = Clock::now();

        const auto device = cuda::query_device(0);
        ForwardMemoryPlanRequest memory_request{};
        memory_request.grid = model.grid;
        memory_request.receiver_count =
            configuration_.receiver_coordinates_m.size();
        memory_request.time_step_count = steps;
        if (visualization_enabled_) {
            memory_request.workspace_bytes = detail::checked_size_product(
                model.grid.physical_cell_count(),
                sizeof(float),
                "visualization workspace size overflow");
        }
        memory_request.available_device_bytes = device.free_memory_bytes;
        memory_request.boundary_kind =
            ForwardMemoryPlanRequest::BoundaryKind::Cpml;
        const auto memory_plan = make_elastic_forward_memory_plan(memory_request);
        memory_plan.require_fit();

        require_output_directory(output_directory_);
        output_paths_ = {
            output_directory_ / "record_vx.sgy",
            output_directory_ / "record_vy.sgy",
            output_directory_ / "record_vz.sgy"};
        temporary_output_paths_ = {
            output_directory_ / "record_vx.sgy.tmp",
            output_directory_ / "record_vy.sgy.tmp",
            output_directory_ / "record_vz.sgy.tmp"};
        for (const auto& output_path : output_paths_) {
            if (std::filesystem::exists(output_path)) {
                throw std::runtime_error(
                    "refusing to overwrite completed SEG-Y output: " +
                    output_path.string());
            }
        }
        std::error_code remove_error;
        for (const auto& temporary_path : temporary_output_paths_) {
            std::filesystem::remove(temporary_path, remove_error);
            remove_error.clear();
        }

        const auto setup_start = Clock::now();
        const auto coefficients = prepare_elastic_coefficients(model);
        const auto prepared_source = prepare_moment_tensor_source_stencils(
            model.grid, configuration_.source);
        const auto receiver_set = prepare_receiver_set(
            model.grid, configuration_.receiver_coordinates_m);
        const auto prepared_receivers = prepare_receiver_stencils(
            model.grid, receiver_set);
        const bool free_surface =
            configuration_.simulation.top_boundary == TopBoundary::FreeSurface;
        const CpmlParameters cpml_parameters{
            configuration_.simulation.time.dt_s,
            static_cast<double>(actual_extrema.max_vp_m_s),
            configuration_.source.wavelet.dominant_frequency_hz,
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
        session_ = std::make_unique<cuda::CudaForwardSession>(
            coefficients,
            prepared_source,
            prepared_receivers,
            cpml_profile,
            surface,
            steps);
        const auto setup_end = Clock::now();

        report_.configuration_path = absolute_configuration_.string();
        report_.model_hdf5_path = model_path_.string();
        report_.output_segy_paths = {
            output_paths_[0].string(),
            output_paths_[1].string(),
            output_paths_[2].string()};
        report_.device_name = device.name;
        report_.physical_cell_count = model.grid.physical_cell_count();
        report_.allocated_cell_count = model.grid.allocated_cell_count();
        report_.receiver_count = configuration_.receiver_coordinates_m.size();
        report_.sample_count = steps;
        report_.planned_required_bytes = memory_plan.required_bytes;
        report_.planned_budget_bytes = memory_plan.budget_bytes;
        report_.input_load_ms = milliseconds(input_end - input_start);
        report_.setup_ms = milliseconds(setup_end - setup_start);
    }

    ~Impl() {
        if (!published_) {
            std::error_code error;
            for (const auto& temporary_path : temporary_output_paths_) {
                std::filesystem::remove(temporary_path, error);
                error.clear();
            }
        }
    }

    io::ForwardRunConfiguration configuration_{};
    std::filesystem::path absolute_configuration_;
    std::filesystem::path model_path_;
    std::filesystem::path output_directory_;
    std::array<std::filesystem::path, 3> output_paths_;
    std::array<std::filesystem::path, 3> temporary_output_paths_;
    std::unique_ptr<cuda::CudaForwardSession> session_;
    std::unique_ptr<cuda::DeviceVisualizationVolume> visualization_volume_;
    CudaForwardRunReport report_{};
    Clock::duration propagation_duration_{};
    bool visualization_enabled_{false};
    bool finalized_{false};
    bool published_{false};
};

CudaForwardJob::CudaForwardJob(
    const std::string& configuration_path,
    bool enable_visualization)
    : impl_(std::make_unique<Impl>(
          configuration_path, enable_visualization)) {}

CudaForwardJob::~CudaForwardJob() = default;
CudaForwardJob::CudaForwardJob(CudaForwardJob&&) noexcept = default;
CudaForwardJob& CudaForwardJob::operator=(CudaForwardJob&&) noexcept = default;

std::size_t CudaForwardJob::total_steps() const noexcept {
    return impl_ ? impl_->session_->total_steps() : 0;
}

std::size_t CudaForwardJob::completed_steps() const noexcept {
    return impl_ ? impl_->session_->completed_steps() : 0;
}

bool CudaForwardJob::finished() const noexcept {
    return impl_ && impl_->session_->finished();
}

const Grid3D& CudaForwardJob::grid() const {
    if (!impl_) {
        throw std::logic_error("CUDA forward job has been moved from");
    }
    return impl_->session_->grid();
}

double CudaForwardJob::dt_s() const noexcept {
    return impl_ ? impl_->session_->dt_s() : 0.0;
}

void CudaForwardJob::advance(std::size_t maximum_steps) {
    if (!impl_ || impl_->finalized_) {
        throw std::logic_error("CUDA forward job is no longer advanceable");
    }
    const auto start = Clock::now();
    static_cast<void>(impl_->session_->advance(maximum_steps));
    impl_->propagation_duration_ += Clock::now() - start;
}

VisualizationDownloadTiming CudaForwardJob::download_visualization(
    cuda::VisualizationField field,
    float* destination,
    std::size_t value_count) {
    if (!impl_ || impl_->finalized_) {
        throw std::logic_error("CUDA forward job cannot provide visualization");
    }
    if (!impl_->visualization_enabled_) {
        throw std::logic_error(
            "CUDA forward visualization was not enabled during memory planning");
    }
    if (impl_->session_->completed_steps() == 0) {
        throw std::logic_error(
            "CUDA forward visualization requires a completed time step");
    }
    if (value_count != impl_->session_->grid().physical_cell_count()) {
        throw std::invalid_argument(
            "CUDA forward visualization destination has an incorrect size");
    }
    if (destination == nullptr) {
        throw std::invalid_argument(
            "CUDA forward visualization destination must not be null");
    }
    if (!impl_->visualization_volume_) {
        impl_->visualization_volume_ =
            std::make_unique<cuda::DeviceVisualizationVolume>(
                impl_->session_->grid());
    }
    const auto extraction_start = Clock::now();
    cuda::extract_physical_visualization_volume(
        impl_->session_->device_wavefield_view(),
        field,
        *impl_->visualization_volume_);
    cuda::synchronize();
    const auto extraction_end = Clock::now();
    impl_->visualization_volume_->download(destination, value_count);
    const auto transfer_end = Clock::now();
    return {
        milliseconds(extraction_end - extraction_start),
        milliseconds(transfer_end - extraction_end)};
}

CudaForwardRunReport CudaForwardJob::finalize() {
    if (!impl_ || impl_->finalized_) {
        throw std::logic_error("CUDA forward job has already been finalized");
    }
    if (!impl_->session_->finished()) {
        throw std::logic_error("CUDA forward job cannot finalize before completion");
    }

    const auto trace_value_count = detail::checked_size_product(
        impl_->report_.receiver_count,
        impl_->report_.sample_count,
        "production receiver trace size overflow");
    std::vector<float> vx(trace_value_count);
    std::vector<float> vy(trace_value_count);
    std::vector<float> vz(trace_value_count);
    const auto download_start = Clock::now();
    impl_->session_->download_receiver_traces(vx, vy, vz);
    const auto download_end = Clock::now();

    const io::ThreeComponentTraces host_traces{
        impl_->report_.receiver_count,
        impl_->report_.sample_count,
        impl_->configuration_.simulation.time.dt_s,
        impl_->configuration_.receiver_coordinates_m,
        impl_->configuration_.source,
        std::move(vx),
        std::move(vy),
        std::move(vz)};
    const auto write_start = Clock::now();
    std::size_t published_count = 0;
    try {
        const std::array components{
            io::SegyComponent::Vx,
            io::SegyComponent::Vy,
            io::SegyComponent::Vz};
        for (std::size_t index = 0; index < components.size(); ++index) {
            io::write_component_segy(
                impl_->temporary_output_paths_[index].string(),
                host_traces,
                components[index]);
            io::require_ieee_component_segy_layout(
                impl_->temporary_output_paths_[index].string(),
                impl_->report_.receiver_count,
                impl_->report_.sample_count,
                components[index]);
        }
        for (std::size_t index = 0; index < components.size(); ++index) {
            std::filesystem::rename(
                impl_->temporary_output_paths_[index],
                impl_->output_paths_[index]);
            ++published_count;
        }
        impl_->published_ = true;
    } catch (...) {
        std::error_code error;
        for (const auto& path : impl_->temporary_output_paths_) {
            std::filesystem::remove(path, error);
            error.clear();
        }
        for (std::size_t index = 0; index < published_count; ++index) {
            std::filesystem::remove(impl_->output_paths_[index], error);
            error.clear();
        }
        throw;
    }
    const auto write_end = Clock::now();

    impl_->report_.propagation_ms = milliseconds(impl_->propagation_duration_);
    impl_->report_.trace_download_ms =
        milliseconds(download_end - download_start);
    impl_->report_.segy_write_ms = milliseconds(write_end - write_start);
    impl_->finalized_ = true;
    return impl_->report_;
}

CudaForwardRunReport run_cuda_forward_from_yaml(
    const std::string& configuration_path) {
    CudaForwardJob job(configuration_path);
    while (!job.finished()) {
        job.advance(job.total_steps() - job.completed_steps());
    }
    return job.finalize();
}

} // namespace wave3d::task
