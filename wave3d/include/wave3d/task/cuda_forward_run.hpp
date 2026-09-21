#pragma once

#include "wave3d/core/grid.hpp"
#include "wave3d/cuda/visualization.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace wave3d::task {

struct CudaForwardRunReport {
    std::string configuration_path;
    std::string model_hdf5_path;
    std::string output_segy_path;
    std::string device_name;
    std::size_t physical_cell_count{0};
    std::size_t allocated_cell_count{0};
    std::size_t receiver_count{0};
    std::size_t sample_count{0};
    std::size_t planned_required_bytes{0};
    std::size_t planned_budget_bytes{0};
    double input_load_ms{0.0};
    double setup_ms{0.0};
    double propagation_ms{0.0};
    double trace_download_ms{0.0};
    double segy_write_ms{0.0};
};

struct VisualizationDownloadTiming {
    double extraction_ms{0.0};
    double transfer_ms{0.0};
};

class CudaForwardJob final {
public:
    explicit CudaForwardJob(
        const std::string& configuration_path,
        bool enable_visualization = false);
    ~CudaForwardJob();

    CudaForwardJob(const CudaForwardJob&) = delete;
    CudaForwardJob& operator=(const CudaForwardJob&) = delete;
    CudaForwardJob(CudaForwardJob&&) noexcept;
    CudaForwardJob& operator=(CudaForwardJob&&) noexcept;

    [[nodiscard]] std::size_t total_steps() const noexcept;
    [[nodiscard]] std::size_t completed_steps() const noexcept;
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] const Grid3D& grid() const;
    [[nodiscard]] double dt_s() const noexcept;
    void advance(std::size_t maximum_steps);
    [[nodiscard]] VisualizationDownloadTiming download_visualization(
        cuda::VisualizationField field,
        float* destination,
        std::size_t value_count);
    [[nodiscard]] CudaForwardRunReport finalize();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] CudaForwardRunReport run_cuda_forward_from_yaml(
    const std::string& configuration_path);

} // namespace wave3d::task
