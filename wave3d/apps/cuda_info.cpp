#include "wave3d/core/forward_memory_plan.hpp"
#include "wave3d/cuda/device_info.hpp"

#include <iomanip>
#include <iostream>

namespace {

double mebibytes(std::size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

} // namespace

int main() {
    try {
        const auto device = wave3d::cuda::query_device(0);

        const wave3d::Grid3D grid{
            200, 200, 200,
            10.0F, 10.0F, 10.0F,
            6,
            {20, 20}, {20, 20}, {0, 20}};
        wave3d::ForwardMemoryPlanRequest request{};
        request.grid = grid;
        request.receiver_count = 1000;
        request.time_step_count = 4000;
        request.workspace_bytes = 64ULL * 1024ULL * 1024ULL;
        request.available_device_bytes = device.free_memory_bytes;
        const auto plan = wave3d::make_elastic_forward_memory_plan(request);

        std::cout << "CUDA device " << device.ordinal << ": " << device.name << '\n'
                  << "compute capability: " << device.compute_major << '.'
                  << device.compute_minor << '\n'
                  << "driver/runtime: "
                  << wave3d::cuda::format_cuda_version(device.driver_version)
                  << " / "
                  << wave3d::cuda::format_cuda_version(device.runtime_version)
                  << '\n'
                  << std::fixed << std::setprecision(1)
                  << "CUDA allocatable total/free memory: "
                  << mebibytes(device.total_memory_bytes) << " / "
                  << mebibytes(device.free_memory_bytes) << " MiB\n"
                  << "physical grid: " << plan.grid.nx << " x " << plan.grid.ny
                  << " x " << plan.grid.nz << '\n'
                  << "allocated grid: " << plan.grid.allocated_nx() << " x "
                  << plan.grid.allocated_ny() << " x "
                  << plan.grid.allocated_nz() << '\n'
                  << "elastic forward memory fields:\n";

        for (const auto& field : plan.fields) {
            std::cout << "  [" << wave3d::memory_category_name(field.category)
                      << "] " << field.name << ": " << mebibytes(field.bytes)
                      << " MiB\n";
        }
        std::cout << "planned allocations: " << mebibytes(plan.allocation_bytes)
                  << " MiB\n"
                  << "runtime reserve: " << mebibytes(plan.runtime_reserve_bytes)
                  << " MiB\n"
                  << "required with reserve: " << mebibytes(plan.required_bytes)
                  << " MiB\n"
                  << "allowed budget: " << mebibytes(plan.budget_bytes) << " MiB\n"
                  << "plan status: " << (plan.fits() ? "fits" : "rejected") << '\n';
        if (plan.fits()) {
            std::cout << "budget headroom: "
                      << mebibytes(plan.budget_headroom_bytes()) << " MiB\n";
        } else {
            std::cout << "budget shortfall: "
                      << mebibytes(plan.budget_shortfall_bytes()) << " MiB\n";
        }
        plan.require_fit();
    } catch (const std::exception& error) {
        std::cerr << "CUDA foundation error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
