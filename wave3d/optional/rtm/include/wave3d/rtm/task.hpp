#pragma once

#include "wave3d/core/grid.hpp"
#include "wave3d/wave/elastic_wavefield_view.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace wave3d::rtm {

enum class TaskState {
    Pending,
    Running,
    Stopping,
    Completed,
    Cancelled,
    Failed
};

struct TaskSnapshot {
    TaskState state{TaskState::Pending};
    std::size_t completed_shots{0};
    std::size_t total_shots{0};
    std::string diagnostic;
};

struct ImageVolumeConstView {
    std::string_view name;
    std::string_view units;
    Grid3D grid{};
    ReadOnlyFloatView values;
};

inline void require_valid_image_volume(const ImageVolumeConstView& image) {
    require_valid_grid_geometry(image.grid);
    if (image.name.empty() || image.units.empty() ||
        image.values.size() != image.grid.physical_cell_count()) {
        throw std::invalid_argument(
            "RTM image needs a name, units, and one value per physical cell");
    }
}

class IResult {
public:
    virtual ~IResult() = default;

    [[nodiscard]] virtual std::string_view task_id() const noexcept = 0;
    [[nodiscard]] virtual std::size_t image_count() const noexcept = 0;
    [[nodiscard]] virtual ImageVolumeConstView image(
        std::size_t index) const = 0;
};

class ITask {
public:
    virtual ~ITask() = default;

    [[nodiscard]] virtual std::string_view task_id() const noexcept = 0;
    [[nodiscard]] virtual TaskSnapshot snapshot() const = 0;
    virtual void request_stop() noexcept = 0;
    [[nodiscard]] virtual std::shared_ptr<const IResult> result() const = 0;
};

} // namespace wave3d::rtm
