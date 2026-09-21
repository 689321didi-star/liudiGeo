#include "wave3d/rtm/task.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class MockResult final : public wave3d::rtm::IResult {
public:
    MockResult()
        : grid_{2, 2, 2, 10.0F, 10.0F, 10.0F, 6, {}, {}, {}},
          image_(grid_.physical_cell_count(), 1.25F) {}

    [[nodiscard]] std::string_view task_id() const noexcept override {
        return "rtm-test";
    }

    [[nodiscard]] std::size_t image_count() const noexcept override {
        return 1;
    }

    [[nodiscard]] wave3d::rtm::ImageVolumeConstView image(
        std::size_t index) const override {
        if (index != 0) {
            throw std::out_of_range("mock RTM image is absent");
        }
        return {"pp_image", "1", grid_, wave3d::ReadOnlyFloatView(image_)};
    }

private:
    wave3d::Grid3D grid_;
    std::vector<float> image_;
};

class MockTask final : public wave3d::rtm::ITask {
public:
    [[nodiscard]] std::string_view task_id() const noexcept override {
        return "rtm-test";
    }

    [[nodiscard]] wave3d::rtm::TaskSnapshot snapshot() const override {
        return snapshot_;
    }

    void request_stop() noexcept override {
        if (snapshot_.state == wave3d::rtm::TaskState::Running) {
            snapshot_.state = wave3d::rtm::TaskState::Stopping;
        }
    }

    [[nodiscard]] std::shared_ptr<const wave3d::rtm::IResult> result()
        const override {
        return result_;
    }

    void complete() {
        snapshot_ = {wave3d::rtm::TaskState::Completed, 1, 1, {}};
        result_ = std::make_shared<MockResult>();
    }

private:
    wave3d::rtm::TaskSnapshot snapshot_{
        wave3d::rtm::TaskState::Running, 0, 1, {}};
    std::shared_ptr<const wave3d::rtm::IResult> result_;
};

void test_read_only_task_result_boundary() {
    MockTask task;
    expect(
        task.snapshot().state == wave3d::rtm::TaskState::Running &&
            !task.result(),
        "RTM task did not begin with a result-free running snapshot");
    task.request_stop();
    expect(
        task.snapshot().state == wave3d::rtm::TaskState::Stopping,
        "RTM stop request did not stay at the interface boundary");

    task.complete();
    const auto result = task.result();
    expect(
        result && result->task_id() == task.task_id() &&
            result->image_count() == 1,
        "completed RTM task did not expose its immutable result identity");
    const auto image = result->image(0);
    wave3d::rtm::require_valid_image_volume(image);
    expect(
        image.name == "pp_image" && image.units == "1" &&
            image.values[0] == 1.25F,
        "RTM result image view changed values or metadata");

    auto invalid = image;
    invalid.values = {};
    bool rejected = false;
    try {
        wave3d::rtm::require_valid_image_volume(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "invalid RTM image storage must fail explicitly");
}

} // namespace

int main() {
    try {
        test_read_only_task_result_boundary();
        std::cout << "Wave3D RTM task interface tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RTM task interface test failure: " << error.what() << '\n';
        return 1;
    }
}
