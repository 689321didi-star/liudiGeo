#include "wave3d/cuda/pinned_host_buffer.hpp"
#include "wave3d/desktop/forward_run_worker.hpp"
#include "wave3d/desktop/static_model_scene.hpp"
#include "wave3d/desktop/volume_viewport.hpp"
#include "wave3d/task/cuda_forward_run.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QSurfaceFormat>
#include <QThread>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

double mean(const std::vector<double>& values) {
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

void configure_surface_format() {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
}

bool wait_until(
    QApplication& application,
    const std::function<bool()>& predicate,
    int timeout_ms) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout_ms) {
        application.processEvents();
        QThread::msleep(2);
    }
    application.processEvents();
    return predicate();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: wave3d_live_display_benchmark CONFIG.yaml [steps]\n";
        return 2;
    }
    try {
        const auto steps = argc == 3 ? std::stoul(argv[2]) : 5UL;
        if (steps == 0) {
            throw std::invalid_argument("benchmark steps must be positive");
        }
        configure_surface_format();
        QApplication application(argc, argv);

        const auto setup_start = Clock::now();
        wave3d::task::CudaForwardJob job(argv[1], true);
        const auto setup_ms = milliseconds(Clock::now() - setup_start);
        if (steps > job.total_steps()) {
            throw std::invalid_argument("benchmark steps exceed the run length");
        }
        auto buffer = std::make_shared<wave3d::cuda::PinnedHostBuffer<float>>(
            job.grid().physical_cell_count());
        std::vector<double> propagation;
        std::vector<double> extraction;
        std::vector<double> transfer;
        std::vector<double> normalization;
        wave3d::desktop::LiveWavefieldScale scale;
        for (std::size_t step = 0; step < steps; ++step) {
            const auto propagation_start = Clock::now();
            job.advance(1);
            propagation.push_back(
                milliseconds(Clock::now() - propagation_start));
            const auto timing = job.download_visualization(
                wave3d::cuda::VisualizationField::Speed,
                buffer->data(),
                buffer->size());
            extraction.push_back(timing.extraction_ms);
            transfer.push_back(timing.transfer_ms);
            const auto normalization_start = Clock::now();
            scale = wave3d::desktop::normalize_live_wavefield_in_place(
                wave3d::cuda::VisualizationField::Speed,
                buffer->data(),
                buffer->size());
            normalization.push_back(
                milliseconds(Clock::now() - normalization_start));
        }

        const auto& grid = job.grid();
        const std::array<float, 3> extents{
            (grid.nx > 1 ? static_cast<float>(grid.nx - 1) : 1.0F) * grid.dx_m,
            (grid.ny > 1 ? static_cast<float>(grid.ny - 1) : 1.0F) * grid.dy_m,
            (grid.nz > 1 ? static_cast<float>(grid.nz - 1) : 1.0F) * grid.dz_m};
        const auto maximum_extent =
            *std::max_element(extents.begin(), extents.end());
        const std::array<float, 3> aspect{
            extents[0] / maximum_extent,
            extents[1] / maximum_extent,
            extents[2] / maximum_extent};

        wave3d::desktop::VolumeViewport viewport;
        viewport.resize(800, 600);
        viewport.set_volume(
            {grid.nx,
             grid.ny,
             grid.nz,
             std::vector<float>(grid.physical_cell_count(), 0.0F),
             aspect},
            QStringLiteral("benchmark-base"));
        viewport.show();
        if (!wait_until(
                application,
                [&] {
                    return viewport.property("volumeTextureReady").toBool() &&
                           viewport.property("volumeFrameReady").toBool();
                },
                10000)) {
            throw std::runtime_error(
                "OpenGL base volume did not produce a frame");
        }
        viewport.set_live_volume(
            {grid.nx,
             grid.ny,
             grid.nz,
             buffer->size(),
             std::shared_ptr<const float>(buffer, buffer->data()),
             aspect,
             scale.signed_scale,
             1},
            QStringLiteral("speed"));
        if (!wait_until(
                application,
                [&] {
                    return viewport.property("liveWavefieldReady").toBool() &&
                           viewport.property("volumeFrameReady").toBool();
                },
                10000)) {
            throw std::runtime_error(
                "OpenGL live volume did not produce a frame");
        }
        const auto upload_ms =
            viewport.property("liveWavefieldUploadMs").toDouble();
        const auto propagation_mean = mean(propagation);
        const auto display_mean = mean(extraction) + mean(transfer) +
                                  mean(normalization) + upload_ms;
        const auto recommended_interval = std::max<std::size_t>(
            1,
            static_cast<std::size_t>(
                std::ceil(display_mean / (0.1 * propagation_mean))));

        std::cout << std::fixed << std::setprecision(3)
                  << "grid=" << grid.nx << 'x' << grid.ny << 'x' << grid.nz
                  << '\n'
                  << "physical_cells=" << grid.physical_cell_count() << '\n'
                  << "steps=" << steps << '\n'
                  << "setup_ms=" << setup_ms << '\n'
                  << "propagation_step_mean_ms=" << propagation_mean << '\n'
                  << "extraction_mean_ms=" << mean(extraction) << '\n'
                  << "pinned_transfer_mean_ms=" << mean(transfer) << '\n'
                  << "normalization_mean_ms=" << mean(normalization) << '\n'
                  << "opengl_upload_ms=" << upload_ms << '\n'
                  << "display_pipeline_mean_ms=" << display_mean << '\n'
                  << "recommended_interval_steps_10pct="
                  << recommended_interval << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "live display benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
