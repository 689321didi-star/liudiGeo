#include "wave3d/core/forward_memory_plan.hpp"

#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

wave3d::ForwardMemoryPlanRequest typical_request() {
    wave3d::ForwardMemoryPlanRequest request{};
    request.grid = {
        200, 200, 200,
        10.0F, 10.0F, 10.0F,
        6,
        {20, 20}, {20, 20}, {0, 20}};
    request.receiver_count = 1000;
    request.time_step_count = 4000;
    request.workspace_bytes = 64ULL * 1024ULL * 1024ULL;
    request.available_device_bytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
    return request;
}

void test_field_by_field_plan() {
    const auto request = typical_request();
    const auto plan = wave3d::make_elastic_forward_memory_plan(request);
    const auto cells = request.grid.allocated_cell_count();
    const auto bytes_per_volume = cells * sizeof(float);

    expect(plan.grid.nx == 200, "plan must retain physical grid dimensions");
    expect(
        plan.grid.allocated_nx() == 252 && plan.grid.allocated_nz() == 232,
        "plan must retain allocated grid dimensions");
    expect(plan.fields.size() == 22, "plan must enumerate every initial field");
    expect(
        plan.category_bytes(wave3d::MemoryCategory::Model) ==
            3 * bytes_per_volume,
        "model category must contain Vp, Vs, and density");
    expect(
        plan.category_bytes(wave3d::MemoryCategory::Coefficient) ==
            5 * bytes_per_volume,
        "coefficient category must contain five elastic fields");
    expect(
        plan.category_bytes(wave3d::MemoryCategory::Wavefield) ==
            9 * bytes_per_volume,
        "wavefield category must contain three velocities and six stresses");
    expect(
        plan.category_bytes(wave3d::MemoryCategory::Boundary) == bytes_per_volume,
        "boundary category must contain the initial sponge field");
    expect(
        plan.category_bytes(wave3d::MemoryCategory::Receiver) ==
            3ULL * request.receiver_count * request.time_step_count * sizeof(float),
        "receiver category must contain three trace components");
    expect(
        plan.required_bytes ==
            plan.allocation_bytes + plan.runtime_reserve_bytes,
        "required bytes must include the runtime reserve");
    expect(plan.fits(), "typical RTX 5060 plan must fit an 8 GiB device budget");
    expect(
        plan.budget_headroom_bytes() == plan.budget_bytes - plan.required_bytes,
        "fitting plan must report exact budget headroom");
    expect(
        plan.budget_shortfall_bytes() == 0,
        "fitting plan must not report a shortfall");

    try {
        plan.require_fit();
    } catch (const std::exception&) {
        expect(false, "fitting plan must not throw");
    }
}

void test_budget_rejection() {
    auto request = typical_request();
    request.available_device_bytes = 1024ULL * 1024ULL * 1024ULL;
    request.max_available_fraction = 0.5;
    const auto plan = wave3d::make_elastic_forward_memory_plan(request);
    expect(!plan.fits(), "oversized plan must be rejected before allocation");
    expect(
        plan.budget_shortfall_bytes() == plan.required_bytes - plan.budget_bytes,
        "rejected plan must report exact budget shortfall");
    expect(
        plan.budget_headroom_bytes() == 0,
        "rejected plan must not report headroom");

    bool threw = false;
    try {
        plan.require_fit();
    } catch (const std::length_error&) {
        threw = true;
    }
    expect(threw, "oversized plan must report a length error");
}

void test_invalid_and_overflowing_requests() {
    auto request = typical_request();
    request.max_available_fraction = std::numeric_limits<double>::quiet_NaN();
    bool threw = false;
    try {
        static_cast<void>(wave3d::make_elastic_forward_memory_plan(request));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "non-finite memory fraction must fail");

    request = typical_request();
    request.receiver_count = std::numeric_limits<std::size_t>::max();
    request.time_step_count = 2;
    threw = false;
    try {
        static_cast<void>(wave3d::make_elastic_forward_memory_plan(request));
    } catch (const std::overflow_error&) {
        threw = true;
    }
    expect(threw, "receiver trace size overflow must fail");

    request = typical_request();
    request.time_step_count = 0;
    threw = false;
    try {
        static_cast<void>(wave3d::make_elastic_forward_memory_plan(request));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "partial receiver dimensions must fail");
}

} // namespace

int main() {
    test_field_by_field_plan();
    test_budget_rejection();
    test_invalid_and_overflowing_requests();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Wave3D memory-plan tests passed\n";
    return 0;
}
