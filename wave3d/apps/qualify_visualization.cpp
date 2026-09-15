#include "wave3d/core/grid.hpp"
#include "wave3d/cuda/cuda_error.hpp"
#include "wave3d/cuda/elastic_propagator.hpp"
#include "wave3d/cuda/visualization.hpp"

#include <array>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>

namespace {

using Clock = std::chrono::steady_clock;

struct FieldCase {
    wave3d::cuda::VisualizationField field;
    const char* name;
};

} // namespace

int main() {
    try {
        const wave3d::Grid3D grid{
            200, 200, 187,
            25.0F, 25.0F, 25.0F,
            6,
            {20, 20}, {20, 20}, {0, 20}};
        wave3d::cuda::DeviceElasticWavefield wavefield(grid);
        wave3d::cuda::DeviceVisualizationVolume output(grid);
        const std::array<FieldCase, 6> cases{{
            {wave3d::cuda::VisualizationField::Vx, "vx"},
            {wave3d::cuda::VisualizationField::Vy, "vy"},
            {wave3d::cuda::VisualizationField::Vz, "vz"},
            {wave3d::cuda::VisualizationField::Speed, "speed"},
            {wave3d::cuda::VisualizationField::Divergence, "divergence"},
            {wave3d::cuda::VisualizationField::CurlMagnitude, "curl_magnitude"}}};
        constexpr int repetitions = 5;
        std::cout << "physical_cells=" << grid.physical_cell_count() << '\n'
                  << "output_bytes=" << output.bytes() << '\n';
        for (const auto& field_case : cases) {
            wave3d::cuda::extract_physical_visualization_volume(
                wavefield.const_view(), field_case.field, output);
            wave3d::cuda::synchronize();
            const auto start = Clock::now();
            for (int repetition = 0; repetition < repetitions; ++repetition) {
                wave3d::cuda::extract_physical_visualization_volume(
                    wavefield.const_view(), field_case.field, output);
                wave3d::cuda::synchronize();
            }
            const auto end = Clock::now();
            const auto milliseconds =
                std::chrono::duration<double, std::milli>(end - start).count() /
                repetitions;
            std::cout << std::fixed << std::setprecision(3)
                      << field_case.name << "_ms=" << milliseconds << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Wave3D visualization qualification failed: "
                  << error.what() << '\n';
        return 1;
    }
}
