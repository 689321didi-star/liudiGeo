#include "wave3d/core/simulation_config.hpp"

#include <iostream>

int main() {
    // This hard-coded smoke configuration is intentionally temporary. A YAML
    // loader will replace it after the numerical core has a tested foundation.
    wave3d::SimulationConfig config{};
    config.grid = {
        200, 200, 200,
        10.0F, 10.0F, 10.0F,
        6,
        {20, 20}, {20, 20}, {0, 20}};
    config.time = {0.0005F, 2.0F};
    config.material = {
        4000.0F, 4000.0F, 2300.0F, 2300.0F, 2500.0F, 2500.0F};
    config.top_boundary = wave3d::TopBoundary::FreeSurface;

    const auto errors = wave3d::validate(config);
    if (!errors.empty()) {
        for (const auto& error : errors) {
            std::cerr << "configuration error: " << error << '\n';
        }
        return 1;
    }

    std::cout << "Wave3D configuration is valid\n"
              << "physical cells: " << config.grid.physical_cell_count() << '\n'
              << "allocated grid: " << config.grid.allocated_nx() << " x "
              << config.grid.allocated_ny() << " x "
              << config.grid.allocated_nz() << '\n'
              << "time steps: " << config.time.step_count() << '\n'
              << "provisional dt limit: "
              << wave3d::provisional_dt_limit_s(config) << " s\n";
    return 0;
}
