#pragma once

#include "wave3d/task/cuda_forward_run.hpp"

#include <QMutex>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <cstddef>
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

struct ForwardRunSnapshot {
    ForwardRunState state{ForwardRunState::Idle};
    std::size_t completed_steps{0};
    std::size_t total_steps{0};
    QString diagnostic;
    std::optional<task::CudaForwardRunReport> report;
};

class ForwardJob {
public:
    virtual ~ForwardJob() = default;
    [[nodiscard]] virtual std::size_t total_steps() const noexcept = 0;
    [[nodiscard]] virtual std::size_t completed_steps() const noexcept = 0;
    [[nodiscard]] virtual bool finished() const noexcept = 0;
    virtual void advance(std::size_t maximum_steps) = 0;
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
    [[nodiscard]] ForwardRunSnapshot snapshot() const;

protected:
    void run() override;

private:
    void set_progress(std::size_t completed, std::size_t total);

    ForwardJobFactory factory_;
    std::size_t batch_steps_{1};
    mutable QMutex mutex_;
    QWaitCondition resume_condition_;
    ForwardRunSnapshot snapshot_;
    bool pause_requested_{false};
    bool stop_requested_{false};
};

} // namespace wave3d::desktop
