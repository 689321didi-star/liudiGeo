#include "wave3d/desktop/forward_run_worker.hpp"

#include <QMutexLocker>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace wave3d::desktop {
namespace {

class ProductionForwardJob final : public ForwardJob {
public:
    explicit ProductionForwardJob(const QString& configuration_path)
        : job_(configuration_path.toStdString(), true) {}

    [[nodiscard]] std::size_t total_steps() const noexcept override {
        return job_.total_steps();
    }
    [[nodiscard]] std::size_t completed_steps() const noexcept override {
        return job_.completed_steps();
    }
    [[nodiscard]] bool finished() const noexcept override {
        return job_.finished();
    }
    [[nodiscard]] const Grid3D& grid() const override { return job_.grid(); }
    [[nodiscard]] double dt_s() const noexcept override { return job_.dt_s(); }
    void advance(std::size_t maximum_steps) override {
        job_.advance(maximum_steps);
    }
    [[nodiscard]] task::VisualizationDownloadTiming download_visualization(
        cuda::VisualizationField field,
        float* destination,
        std::size_t value_count) override {
        return job_.download_visualization(field, destination, value_count);
    }
    [[nodiscard]] task::CudaForwardRunReport finalize() override {
        return job_.finalize();
    }

private:
    task::CudaForwardJob job_;
};

bool signed_visualization_field(cuda::VisualizationField field) {
    return field == cuda::VisualizationField::Vx ||
           field == cuda::VisualizationField::Vy ||
           field == cuda::VisualizationField::Vz ||
           field == cuda::VisualizationField::Divergence;
}

void require_known_visualization_field(cuda::VisualizationField field) {
    switch (field) {
    case cuda::VisualizationField::Vx:
    case cuda::VisualizationField::Vy:
    case cuda::VisualizationField::Vz:
    case cuda::VisualizationField::Speed:
    case cuda::VisualizationField::Divergence:
    case cuda::VisualizationField::CurlMagnitude:
        return;
    }
    throw std::invalid_argument("unknown desktop visualization field");
}

} // namespace

LiveWavefieldScale normalize_live_wavefield_in_place(
    cuda::VisualizationField field,
    float* values,
    std::size_t value_count) {
    require_known_visualization_field(field);
    if (values == nullptr || value_count == 0) {
        throw std::invalid_argument(
            "live wavefield normalization requires nonempty storage");
    }
    const auto range = std::minmax_element(values, values + value_count);
    if (!std::isfinite(*range.first) || !std::isfinite(*range.second)) {
        throw std::runtime_error("live wavefield contains a non-finite value");
    }
    const bool signed_scale = signed_visualization_field(field);
    float physical_minimum = *range.first;
    float physical_maximum = *range.second;
    if (signed_scale) {
        const auto magnitude = std::max(
            std::abs(physical_minimum), std::abs(physical_maximum));
        physical_minimum = -magnitude;
        physical_maximum = magnitude;
        if (magnitude == 0.0F) {
            std::fill(values, values + value_count, 0.5F);
        } else {
            const auto inverse = 0.5F / magnitude;
            for (std::size_t index = 0; index < value_count; ++index) {
                values[index] = std::clamp(
                    0.5F + values[index] * inverse, 0.0F, 1.0F);
            }
        }
    } else {
        physical_minimum = 0.0F;
        physical_maximum = std::max(0.0F, physical_maximum);
        if (physical_maximum == 0.0F) {
            std::fill(values, values + value_count, 0.0F);
        } else {
            const auto inverse = 1.0F / physical_maximum;
            for (std::size_t index = 0; index < value_count; ++index) {
                values[index] =
                    std::clamp(values[index] * inverse, 0.0F, 1.0F);
            }
        }
    }
    return {physical_minimum, physical_maximum, signed_scale};
}

ForwardRunWorker::ForwardRunWorker(
    QString configuration_path,
    std::size_t batch_steps,
    QObject* parent)
    : ForwardRunWorker(
          [path = std::move(configuration_path)] {
              return std::make_unique<ProductionForwardJob>(path);
          },
          batch_steps,
          parent) {}

ForwardRunWorker::ForwardRunWorker(
    ForwardJobFactory factory,
    std::size_t batch_steps,
    QObject* parent)
    : QThread(parent),
      factory_(std::move(factory)),
      batch_steps_(batch_steps) {
    if (!factory_) {
        throw std::invalid_argument("forward worker requires a job factory");
    }
    if (batch_steps_ == 0) {
        throw std::invalid_argument("forward worker batch size must be positive");
    }
}

ForwardRunWorker::~ForwardRunWorker() {
    request_stop();
    wait();
}

void ForwardRunWorker::request_pause() {
    QMutexLocker lock(&mutex_);
    if (snapshot_.state == ForwardRunState::Running) {
        pause_requested_ = true;
    }
}

void ForwardRunWorker::request_resume() {
    QMutexLocker lock(&mutex_);
    pause_requested_ = false;
    resume_condition_.wakeAll();
}

void ForwardRunWorker::request_stop() {
    QMutexLocker lock(&mutex_);
    stop_requested_ = true;
    pause_requested_ = false;
    if (snapshot_.state == ForwardRunState::Running ||
        snapshot_.state == ForwardRunState::Paused ||
        snapshot_.state == ForwardRunState::Preparing) {
        snapshot_.state = ForwardRunState::Stopping;
    }
    resume_condition_.wakeAll();
}

void ForwardRunWorker::request_visualization_field(
    cuda::VisualizationField field) {
    require_known_visualization_field(field);
    QMutexLocker lock(&mutex_);
    if (visualization_field_ != field) {
        visualization_field_ = field;
        frame_requested_ = true;
    }
}

void ForwardRunWorker::request_display_interval(
    std::size_t completed_step_interval) {
    if (completed_step_interval == 0) {
        throw std::invalid_argument("display interval must be positive");
    }
    QMutexLocker lock(&mutex_);
    display_interval_steps_ = completed_step_interval;
    frame_requested_ = true;
}

ForwardRunSnapshot ForwardRunWorker::snapshot() const {
    QMutexLocker lock(&mutex_);
    return snapshot_;
}

void ForwardRunWorker::set_progress(
    std::size_t completed,
    std::size_t total) {
    QMutexLocker lock(&mutex_);
    snapshot_.completed_steps = completed;
    snapshot_.total_steps = total;
}

void ForwardRunWorker::capture_frame(ForwardJob& job) {
    cuda::VisualizationField field{};
    {
        QMutexLocker lock(&mutex_);
        const bool interval_due =
            job.completed_steps() % display_interval_steps_ == 0;
        if (!frame_requested_ && !interval_due && !job.finished()) {
            return;
        }
        field = visualization_field_;
        frame_requested_ = false;
    }

    std::shared_ptr<cuda::PinnedHostBuffer<float>> buffer;
    for (auto& candidate : frame_buffers_) {
        if (!candidate) {
            candidate = std::make_shared<cuda::PinnedHostBuffer<float>>(
                job.grid().physical_cell_count());
        }
        if (candidate.use_count() == 1) {
            buffer = candidate;
            break;
        }
    }
    if (!buffer) {
        QMutexLocker lock(&mutex_);
        ++snapshot_.dropped_display_frames;
        return;
    }

    const auto timing =
        job.download_visualization(field, buffer->data(), buffer->size());
    const auto normalization_started = std::chrono::steady_clock::now();
    const auto scale = normalize_live_wavefield_in_place(
        field, buffer->data(), buffer->size());
    const auto normalization_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - normalization_started)
            .count();

    auto frame = std::make_shared<LiveWavefieldFrame>();
    frame->grid = job.grid();
    frame->field = field;
    frame->completed_steps = job.completed_steps();
    frame->velocity_time_s = job.completed_steps() * job.dt_s();
    frame->physical_minimum = scale.physical_minimum;
    frame->physical_maximum = scale.physical_maximum;
    frame->extraction_ms = timing.extraction_ms;
    frame->transfer_ms = timing.transfer_ms;
    frame->normalization_ms = normalization_ms;
    frame->staging_ms =
        timing.extraction_ms + timing.transfer_ms + normalization_ms;
    frame->signed_scale = scale.signed_scale;
    frame->normalized_values =
        std::shared_ptr<const float>(buffer, buffer->data());
    frame->value_count = buffer->size();
    {
        QMutexLocker lock(&mutex_);
        frame->sequence = next_frame_sequence_++;
        snapshot_.latest_frame = std::move(frame);
    }
}

void ForwardRunWorker::run() {
    {
        QMutexLocker lock(&mutex_);
        if (stop_requested_) {
            snapshot_.state = ForwardRunState::Cancelled;
            return;
        }
        snapshot_.state = ForwardRunState::Preparing;
    }
    try {
        auto job = factory_();
        if (!job) {
            throw std::runtime_error("forward job factory returned no job");
        }
        {
            QMutexLocker lock(&mutex_);
            snapshot_.completed_steps = job->completed_steps();
            snapshot_.total_steps = job->total_steps();
            if (stop_requested_) {
                snapshot_.state = ForwardRunState::Cancelled;
                return;
            }
            snapshot_.state = ForwardRunState::Running;
        }

        while (!job->finished()) {
            {
                QMutexLocker lock(&mutex_);
                if (stop_requested_) {
                    snapshot_.state = ForwardRunState::Cancelled;
                    return;
                }
                if (pause_requested_) {
                    snapshot_.state = ForwardRunState::Paused;
                    while (pause_requested_ && !stop_requested_) {
                        resume_condition_.wait(&mutex_);
                    }
                    if (stop_requested_) {
                        snapshot_.state = ForwardRunState::Cancelled;
                        return;
                    }
                    snapshot_.state = ForwardRunState::Running;
                }
            }
            job->advance(batch_steps_);
            set_progress(job->completed_steps(), job->total_steps());
            capture_frame(*job);
        }

        {
            QMutexLocker lock(&mutex_);
            if (stop_requested_) {
                snapshot_.state = ForwardRunState::Cancelled;
                return;
            }
            if (pause_requested_) {
                snapshot_.state = ForwardRunState::Paused;
                while (pause_requested_ && !stop_requested_) {
                    resume_condition_.wait(&mutex_);
                }
                if (stop_requested_) {
                    snapshot_.state = ForwardRunState::Cancelled;
                    return;
                }
            }
            snapshot_.state = ForwardRunState::Finalizing;
        }

        auto report = job->finalize();
        {
            QMutexLocker lock(&mutex_);
            snapshot_.report = std::move(report);
            snapshot_.state = ForwardRunState::Completed;
        }
    } catch (const std::exception& error) {
        QMutexLocker lock(&mutex_);
        snapshot_.diagnostic = QString::fromUtf8(error.what());
        snapshot_.state = ForwardRunState::Failed;
    } catch (...) {
        QMutexLocker lock(&mutex_);
        snapshot_.diagnostic = QStringLiteral("未知正演异常");
        snapshot_.state = ForwardRunState::Failed;
    }
}

} // namespace wave3d::desktop
