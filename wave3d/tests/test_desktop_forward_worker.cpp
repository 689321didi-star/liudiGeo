#include "wave3d/desktop/forward_run_worker.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

using wave3d::desktop::ForwardRunState;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

class FakeJob final : public wave3d::desktop::ForwardJob {
public:
    FakeJob(std::size_t total, std::chrono::milliseconds delay)
        : total_(total), delay_(delay) {}

    [[nodiscard]] std::size_t total_steps() const noexcept override {
        return total_;
    }
    [[nodiscard]] std::size_t completed_steps() const noexcept override {
        return completed_;
    }
    [[nodiscard]] bool finished() const noexcept override {
        return completed_ == total_;
    }
    void advance(std::size_t maximum_steps) override {
        if (maximum_steps == 0 || finished()) {
            throw std::logic_error("invalid fake advance");
        }
        std::this_thread::sleep_for(delay_);
        completed_ += std::min(maximum_steps, total_ - completed_);
    }
    [[nodiscard]] wave3d::task::CudaForwardRunReport finalize() override {
        if (!finished()) {
            throw std::logic_error("early fake finalization");
        }
        wave3d::task::CudaForwardRunReport report;
        report.receiver_count = 3;
        report.sample_count = total_;
        report.device_name = "fake-device";
        report.output_segy_path = "output/record.sgy";
        return report;
    }

private:
    std::size_t total_{0};
    std::size_t completed_{0};
    std::chrono::milliseconds delay_{0};
};

void test_pause_resume_and_complete() {
    wave3d::desktop::ForwardRunWorker worker(
        [] {
            return std::make_unique<FakeJob>(80, std::chrono::milliseconds(2));
        });
    worker.start();
    expect(
        wait_until(
            [&] {
                const auto value = worker.snapshot();
                return value.state == ForwardRunState::Running &&
                       value.completed_steps >= 2;
            },
            std::chrono::seconds(2)),
        "worker did not enter running state");
    worker.request_pause();
    expect(
        wait_until(
            [&] { return worker.snapshot().state == ForwardRunState::Paused; },
            std::chrono::seconds(2)),
        "worker did not pause at a batch boundary");
    const auto paused_steps = worker.snapshot().completed_steps;
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    expect(
        worker.snapshot().completed_steps == paused_steps,
        "paused worker continued advancing");
    worker.request_resume();
    expect(worker.wait(3000), "resumed worker did not finish");
    const auto result = worker.snapshot();
    expect(
        result.state == ForwardRunState::Completed &&
            result.completed_steps == result.total_steps && result.report &&
            result.report->sample_count == 80,
        "worker did not publish a completed report");
}

void test_cancel_and_failure() {
    wave3d::desktop::ForwardRunWorker cancelled(
        [] {
            return std::make_unique<FakeJob>(1000, std::chrono::milliseconds(2));
        });
    cancelled.start();
    expect(
        wait_until(
            [&] { return cancelled.snapshot().completed_steps >= 2; },
            std::chrono::seconds(2)),
        "cancellation fixture did not start");
    cancelled.request_stop();
    expect(cancelled.wait(3000), "cancelled worker did not stop");
    const auto cancelled_result = cancelled.snapshot();
    expect(
        cancelled_result.state == ForwardRunState::Cancelled &&
            !cancelled_result.report &&
            cancelled_result.completed_steps < cancelled_result.total_steps,
        "cancelled worker claimed completion");

    wave3d::desktop::ForwardRunWorker failed([]() -> std::unique_ptr<wave3d::desktop::ForwardJob> {
        throw std::runtime_error("fixture setup failure");
    });
    failed.start();
    expect(failed.wait(3000), "failed worker did not terminate");
    const auto failed_result = failed.snapshot();
    expect(
        failed_result.state == ForwardRunState::Failed &&
            failed_result.diagnostic == QStringLiteral("fixture setup failure") &&
            !failed_result.report,
        "worker did not retain setup failure diagnostics");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        test_pause_resume_and_complete();
        test_cancel_and_failure();
        std::cout << "desktop forward worker tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "desktop forward worker test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
