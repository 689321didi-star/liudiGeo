#include "wave3d/desktop/forward_run_worker.hpp"

#include <QMutexLocker>

#include <stdexcept>
#include <utility>

namespace wave3d::desktop {
namespace {

class ProductionForwardJob final : public ForwardJob {
public:
    explicit ProductionForwardJob(const QString& configuration_path)
        : job_(configuration_path.toStdString()) {}

    [[nodiscard]] std::size_t total_steps() const noexcept override {
        return job_.total_steps();
    }
    [[nodiscard]] std::size_t completed_steps() const noexcept override {
        return job_.completed_steps();
    }
    [[nodiscard]] bool finished() const noexcept override {
        return job_.finished();
    }
    void advance(std::size_t maximum_steps) override {
        job_.advance(maximum_steps);
    }
    [[nodiscard]] task::CudaForwardRunReport finalize() override {
        return job_.finalize();
    }

private:
    task::CudaForwardJob job_;
};

} // namespace

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
