#pragma once

#include <cstddef>
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

[[nodiscard]] CudaForwardRunReport run_cuda_forward_from_yaml(
    const std::string& configuration_path);

} // namespace wave3d::task
