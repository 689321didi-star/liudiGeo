#include "wave3d/desktop/live_wavefield_view.hpp"

#include <QColor>
#include <QImage>

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

wave3d::desktop::LiveWavefieldFrame frame_from(
    const wave3d::Grid3D& grid,
    std::vector<float> values,
    bool signed_scale = false) {
    auto storage = std::make_shared<std::vector<float>>(std::move(values));
    wave3d::desktop::LiveWavefieldFrame frame;
    frame.grid = grid;
    frame.sequence = 17;
    frame.completed_steps = 9;
    frame.signed_scale = signed_scale;
    frame.value_count = storage->size();
    frame.normalized_values =
        std::shared_ptr<const float>(storage, storage->data());
    return frame;
}

QImage black_image(int width, int height) {
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    return image;
}

bool changed_from_black(const QColor& colour) {
    return colour.red() != 0 || colour.green() != 0 || colour.blue() != 0;
}

void test_field_mapping() {
    using wave3d::cuda::VisualizationField;
    const std::array fields{
        VisualizationField::Vx,
        VisualizationField::Vy,
        VisualizationField::Vz,
        VisualizationField::Speed,
        VisualizationField::Divergence,
        VisualizationField::CurlMagnitude};
    for (const auto field : fields) {
        const auto key = wave3d::desktop::visualization_field_key(field);
        expect(
            wave3d::desktop::visualization_field_from_key(key) == field,
            "visualization field key did not round-trip");
        expect(
            !wave3d::desktop::visualization_field_name(field).isEmpty() &&
                !wave3d::desktop::visualization_field_unit(field).isEmpty(),
            "visualization field has no presentation metadata");
    }
}

void test_three_slice_orientations() {
    const wave3d::Grid3D grid{
        2, 3, 2, 10.0F, 20.0F, 30.0F, 6,
        {6, 6}, {6, 6}, {0, 6}};
    std::vector<float> values(grid.physical_cell_count(), 0.0F);

    values[grid.physical_linear_index(1, 0, 1)] = 1.0F;
    auto frame = frame_from(grid, values);
    auto xy = wave3d::desktop::composite_live_wavefield_slice(
        black_image(2, 3), frame, 0, 1, 1.0F, 0.01F);
    expect(
        changed_from_black(xy.pixelColor(1, 2)) &&
            !changed_from_black(xy.pixelColor(1, 0)),
        "XY live slice did not preserve physical Y-up orientation");

    values.assign(values.size(), 0.0F);
    values[grid.physical_linear_index(1, 1, 0)] = 1.0F;
    frame = frame_from(grid, values);
    auto xz = wave3d::desktop::composite_live_wavefield_slice(
        black_image(2, 2), frame, 1, 1, 1.0F, 0.01F);
    expect(
        changed_from_black(xz.pixelColor(1, 0)) &&
            !changed_from_black(xz.pixelColor(1, 1)),
        "XZ live slice did not preserve physical Z-down orientation");

    values.assign(values.size(), 0.0F);
    values[grid.physical_linear_index(0, 2, 1)] = 1.0F;
    frame = frame_from(grid, values);
    auto yz = wave3d::desktop::composite_live_wavefield_slice(
        black_image(3, 2), frame, 2, 0, 1.0F, 0.01F);
    expect(
        changed_from_black(yz.pixelColor(2, 1)) &&
            !changed_from_black(yz.pixelColor(2, 0)),
        "YZ live slice did not preserve physical Z-down orientation");
}

void test_signed_zero_and_colours() {
    const wave3d::Grid3D grid{
        3, 1, 1, 10.0F, 10.0F, 10.0F, 6,
        {6, 6}, {6, 6}, {0, 6}};
    auto frame = frame_from(grid, {0.0F, 0.5F, 1.0F}, true);
    const auto image = wave3d::desktop::composite_live_wavefield_slice(
        black_image(3, 1), frame, 0, 0, 1.0F, 0.01F);
    const auto negative = image.pixelColor(0, 0);
    const auto zero = image.pixelColor(1, 0);
    const auto positive = image.pixelColor(2, 0);
    expect(
        negative.blue() > negative.red() &&
            !changed_from_black(zero) && positive.red() > positive.blue(),
        "signed live map is not blue/transparent/red around zero");
}

} // namespace

int main() {
    try {
        test_field_mapping();
        test_three_slice_orientations();
        test_signed_zero_and_colours();
        std::cout << "desktop live wavefield tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "desktop live wavefield test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
