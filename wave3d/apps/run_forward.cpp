#include "wave3d/task/cuda_forward_run.hpp"

#include <exception>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: wave3d_run CONFIG.yaml\n";
        return 2;
    }
    try {
        const auto report =
            wave3d::task::run_cuda_forward_from_yaml(argv[1]);
        std::cout << std::fixed << std::setprecision(6)
                  << "configuration=" << report.configuration_path << '\n'
                  << "model_hdf5=" << report.model_hdf5_path << '\n'
                  << "output_segy_vx=" << report.output_segy_paths.vx << '\n'
                  << "output_segy_vy=" << report.output_segy_paths.vy << '\n'
                  << "output_segy_vz=" << report.output_segy_paths.vz << '\n'
                  << "device_name=" << report.device_name << '\n'
                  << "physical_cells=" << report.physical_cell_count << '\n'
                  << "allocated_cells=" << report.allocated_cell_count << '\n'
                  << "receivers=" << report.receiver_count << '\n'
                  << "samples=" << report.sample_count << '\n'
                  << "planned_required_bytes="
                  << report.planned_required_bytes << '\n'
                  << "planned_budget_bytes=" << report.planned_budget_bytes
                  << '\n'
                  << "input_load_ms=" << report.input_load_ms << '\n'
                  << "setup_ms=" << report.setup_ms << '\n'
                  << "propagation_ms=" << report.propagation_ms << '\n'
                  << "trace_download_ms=" << report.trace_download_ms << '\n'
                  << "segy_write_ms=" << report.segy_write_ms << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Wave3D forward run failed: " << error.what() << '\n';
        return 1;
    }
}
