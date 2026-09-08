#pragma once

#include "wave3d/core/checked_size.hpp"
#include "wave3d/core/grid.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wave3d {

enum class MemoryCategory {
    Model,
    Coefficient,
    Wavefield,
    Boundary,
    Receiver,
    Workspace,
};

[[nodiscard]] inline const char* memory_category_name(MemoryCategory category) {
    switch (category) {
    case MemoryCategory::Model:
        return "model";
    case MemoryCategory::Coefficient:
        return "coefficient";
    case MemoryCategory::Wavefield:
        return "wavefield";
    case MemoryCategory::Boundary:
        return "boundary";
    case MemoryCategory::Receiver:
        return "receiver";
    case MemoryCategory::Workspace:
        return "workspace";
    }
    return "unknown";
}

struct FieldMemory {
    std::string name;
    MemoryCategory category{};
    std::size_t element_count{0};
    std::size_t element_size_bytes{0};
    std::size_t bytes{0};
};

struct ForwardMemoryPlan {
    Grid3D grid{};
    std::vector<FieldMemory> fields;
    std::size_t allocation_bytes{0};
    std::size_t runtime_reserve_bytes{0};
    std::size_t required_bytes{0};
    std::size_t available_device_bytes{0};
    std::size_t budget_bytes{0};
    double max_available_fraction{0.0};

    [[nodiscard]] bool fits() const noexcept {
        return required_bytes <= budget_bytes;
    }

    [[nodiscard]] std::size_t budget_headroom_bytes() const noexcept {
        return fits() ? budget_bytes - required_bytes : 0;
    }

    [[nodiscard]] std::size_t budget_shortfall_bytes() const noexcept {
        return fits() ? 0 : required_bytes - budget_bytes;
    }

    [[nodiscard]] std::size_t category_bytes(MemoryCategory category) const {
        std::size_t total = 0;
        for (const auto& field : fields) {
            if (field.category == category) {
                total = detail::checked_size_add(
                    total,
                    field.bytes,
                    "Wave3D memory category total overflows size_t");
            }
        }
        return total;
    }

    void require_fit() const {
        if (!fits()) {
            throw std::length_error(
                "Wave3D device memory plan requires " +
                std::to_string(required_bytes) + " bytes but the configured "
                "budget is " + std::to_string(budget_bytes) + " bytes");
        }
    }
};

struct ForwardMemoryPlanRequest {
    Grid3D grid{};
    std::size_t receiver_count{0};
    std::size_t time_step_count{0};
    std::size_t workspace_bytes{0};
    std::size_t runtime_reserve_bytes{512ULL * 1024ULL * 1024ULL};
    std::size_t available_device_bytes{0};
    double max_available_fraction{0.80};
};

[[nodiscard]] inline ForwardMemoryPlan make_elastic_forward_memory_plan(
    const ForwardMemoryPlanRequest& request) {
    if (request.available_device_bytes == 0) {
        throw std::invalid_argument("available device memory must be positive");
    }
    if (!std::isfinite(request.max_available_fraction) ||
        !(request.max_available_fraction > 0.0) ||
        !(request.max_available_fraction <= 1.0)) {
        throw std::invalid_argument(
            "maximum available-memory fraction must be in (0, 1]");
    }
    if ((request.receiver_count == 0) != (request.time_step_count == 0)) {
        throw std::invalid_argument(
            "receiver count and time-step count must both be zero or positive");
    }

    ForwardMemoryPlan plan{};
    plan.grid = request.grid;
    plan.runtime_reserve_bytes = request.runtime_reserve_bytes;
    plan.available_device_bytes = request.available_device_bytes;
    plan.max_available_fraction = request.max_available_fraction;
    plan.budget_bytes = static_cast<std::size_t>(std::floor(
        static_cast<long double>(request.available_device_bytes) *
        static_cast<long double>(request.max_available_fraction)));

    const auto add_field = [&plan](
                               std::string name,
                               MemoryCategory category,
                               std::size_t element_count,
                               std::size_t element_size_bytes) {
        const auto bytes = detail::checked_size_product(
            element_count,
            element_size_bytes,
            "Wave3D field byte count overflows size_t");
        plan.allocation_bytes = detail::checked_size_add(
            plan.allocation_bytes,
            bytes,
            "Wave3D planned allocation total overflows size_t");
        plan.fields.push_back(
            {std::move(name), category, element_count, element_size_bytes, bytes});
    };

    const auto cells = request.grid.allocated_cell_count();
    constexpr auto scalar_bytes = sizeof(float);

    for (const char* name : {"vp", "vs", "rho"}) {
        add_field(name, MemoryCategory::Model, cells, scalar_bytes);
    }
    for (const char* name : {
             "lambda",
             "mu",
             "bulk_modulus",
             "buoyancy_x",
             "buoyancy_y",
             "buoyancy_z",
             "mu_xy",
             "mu_xz",
             "mu_yz"}) {
        add_field(name, MemoryCategory::Coefficient, cells, scalar_bytes);
    }
    for (const char* name : {
             "vx", "vy", "vz", "sxx", "syy", "szz", "sxy", "sxz", "syz"}) {
        add_field(name, MemoryCategory::Wavefield, cells, scalar_bytes);
    }
    add_field("sponge", MemoryCategory::Boundary, cells, scalar_bytes);

    if (request.receiver_count != 0) {
        const auto trace_samples = detail::checked_size_product(
            request.receiver_count,
            request.time_step_count,
            "Wave3D receiver trace count overflows size_t");
        for (const char* name : {"trace_vx", "trace_vy", "trace_vz"}) {
            add_field(name, MemoryCategory::Receiver, trace_samples, scalar_bytes);
        }
    }
    if (request.workspace_bytes != 0) {
        add_field(
            "workspace", MemoryCategory::Workspace, request.workspace_bytes, 1);
    }

    plan.required_bytes = detail::checked_size_add(
        plan.allocation_bytes,
        plan.runtime_reserve_bytes,
        "Wave3D memory requirement overflows size_t");
    return plan;
}

} // namespace wave3d
