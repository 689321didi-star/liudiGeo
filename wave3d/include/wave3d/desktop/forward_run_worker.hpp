#pragma once

#include "wave3d/cuda/pinned_host_buffer.hpp"
#include "wave3d/task/cuda_forward_run.hpp"

#include <QMutex>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace wave3d::desktop {

enum class ForwardRunState {
    Idle,
    Preparing,
    Running,
    Paused,
    Stopping,
    Finalizing,
    Completed,
    Cancelled,
    Failed
};

struct LiveWavefieldFrame {
    Grid3D grid{};
    cuda::VisualizationField field{cuda::VisualizationField::Speed};
    std::uint64_t sequence{0};
    std::size_t completed_steps{0};
    double velocity_time_s{0.0};
    float physical_minimum{0.0F};
    float physical_maximum{0.0F};
    double extraction_ms{0.0};
    double transfer_ms{0.0};
    double normalization_ms{0.0};
    double staging_ms{0.0};
    bool signed_scale{false};
    std::shared_ptr<const float> normalized_values;
    std::size_t value_count{0};
};

struct LiveWavefieldScale {
    float physical_minimum{0.0F};
    float physical_maximum{0.0F};
    bool signed_scale{false};
};

[[nodiscard]] LiveWavefieldScale normalize_live_wavefield_in_place(
    cuda::VisualizationField field,
    float* values,
    std::size_t value_count);

struct ForwardRunSnapshot {
    ForwardRunState state{ForwardRunState::Idle};
    std::size_t completed_steps{0};
    std::size_t total_steps{0};
    QString diagnostic;
    std::optional<task::CudaForwardRunReport> report;
    std::shared_ptr<const LiveWavefieldFrame> latest_frame;
    std::size_t dropped_display_frames{0};
};

class ForwardJob {
public:
    virtual ~ForwardJob() = default;
    [[nodiscard]] virtual std::size_t total_steps() const noexcept = 0;
    [[nodiscard]] virtual std::size_t completed_steps() const noexcept = 0;
    [[nodiscard]] virtual bool finished() const noexcept = 0;
    [[nodiscard]] virtual const Grid3D& grid() const = 0;
    [[nodiscard]] virtual double dt_s() const noexcept = 0;
    virtual void advance(std::size_t maximum_steps) = 0;
    [[nodiscard]] virtual task::VisualizationDownloadTiming
    download_visualization(
        cuda::VisualizationField field,
        float* destination,
        std::size_t value_count) = 0;
    [[nodiscard]] virtual task::CudaForwardRunReport finalize() = 0;
};

using ForwardJobFactory = std::function<std::unique_ptr<ForwardJob>()>;

class ForwardRunWorker final : public QThread {
public:
    explicit ForwardRunWorker(
        QString configuration_path,
        std::size_t batch_steps = 1,
        QObject* parent = nullptr);
    explicit ForwardRunWorker(
        ForwardJobFactory factory,
        std::size_t batch_steps = 1,
        QObject* parent = nullptr);
    ~ForwardRunWorker() override;

    ForwardRunWorker(const ForwardRunWorker&) = delete;
    ForwardRunWorker& operator=(const ForwardRunWorker&) = delete;

    void request_pause();
    void request_resume();
    void request_stop();
    void request_visualization_field(cuda::VisualizationField field);
    void request_display_interval(std::size_t completed_step_interval);
    [[nodiscard]] ForwardRunSnapshot snapshot() const;

protected:
    void run() override;

private:
    void set_progress(std::size_t completed, std::size_t total);
    void capture_frame(ForwardJob& job);

    ForwardJobFactory factory_;
    std::size_t batch_steps_{1};
    mutable QMutex mutex_;
    QWaitCondition resume_condition_;
    ForwardRunSnapshot snapshot_;
    bool pause_requested_{false};
    bool stop_requested_{false};
    bool frame_requested_{true};
    cuda::VisualizationField visualization_field_{
        cuda::VisualizationField::Speed};
    std::size_t display_interval_steps_{1};
    std::uint64_t next_frame_sequence_{1};
    std::array<std::shared_ptr<cuda::PinnedHostBuffer<float>>, 2>
        frame_buffers_{};
};

} // namespace wave3d::desktop
