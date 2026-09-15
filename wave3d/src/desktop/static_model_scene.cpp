#include "wave3d/desktop/static_model_scene.hpp"

#include "wave3d/io/hdf5.hpp"

#include <QColor>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace wave3d::desktop {
namespace {

const std::vector<float>& property_values(
    const PhysicalModel& model,
    ModelProperty property) {
    switch (property) {
    case ModelProperty::Vp:
        return model.vp_m_s;
    case ModelProperty::Vs:
        return model.vs_m_s;
    case ModelProperty::Density:
        return model.density_kg_m3;
    }
    throw std::invalid_argument("unsupported model property");
}

std::pair<float, float> property_range(
    const PhysicalModel& model,
    ModelProperty property) {
    const auto& values = property_values(model, property);
    const auto range = std::minmax_element(values.begin(), values.end());
    return {*range.first, *range.second};
}

QRgb sequential_colour(float value, float minimum, float maximum) {
    const auto span = static_cast<double>(maximum) - minimum;
    const auto normalized = span == 0.0
                                ? 0.5
                                : std::clamp(
                                      (static_cast<double>(value) - minimum) / span,
                                      0.0,
                                      1.0);
    struct Stop {
        double position;
        QColor colour;
    };
    static const Stop stops[] = {
        {0.0, QColor(8, 29, 55)},
        {0.5, QColor(23, 132, 160)},
        {1.0, QColor(250, 221, 90)}};
    const auto& lower = normalized <= 0.5 ? stops[0] : stops[1];
    const auto& upper = normalized <= 0.5 ? stops[1] : stops[2];
    const auto fraction =
        (normalized - lower.position) / (upper.position - lower.position);
    const auto channel = [fraction](int first, int second) {
        return static_cast<int>(
            std::lround(first + fraction * static_cast<double>(second - first)));
    };
    return qRgb(
        channel(lower.colour.red(), upper.colour.red()),
        channel(lower.colour.green(), upper.colour.green()),
        channel(lower.colour.blue(), upper.colour.blue()));
}

int image_dimension(std::size_t value) {
    if (value == 0 ||
        value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("model dimension cannot be represented by QImage");
    }
    return static_cast<int>(value);
}

std::size_t linear_index(
    const Grid3D& grid,
    std::size_t x,
    std::size_t y,
    std::size_t z) {
    return x + grid.nx * (y + grid.ny * z);
}

} // namespace

StaticModelScene StaticModelScene::load_hdf5(const QString& path) {
    const auto absolute_path = QFileInfo(path).absoluteFilePath();
    return StaticModelScene(
        io::read_hdf5_model(absolute_path.toStdString()), absolute_path);
}

StaticModelScene::StaticModelScene(PhysicalModel model, QString source_path)
    : model_(std::move(model)), source_path_(std::move(source_path)) {
    require_valid_physical_model(model_);
    summary_ = {
        model_.grid,
        physical_domain_max(model_.grid),
        physical_model_extrema(model_),
        model_.grid.nx / 2,
        model_.grid.ny / 2,
        model_.grid.nz / 2};
}

const StaticModelSummary& StaticModelScene::summary() const noexcept {
    return summary_;
}

const QString& StaticModelScene::source_path() const noexcept {
    return source_path_;
}

QImage StaticModelScene::xy_slice(ModelProperty property) const {
    return xy_slice(property, summary_.center_z);
}

QImage StaticModelScene::xz_slice(ModelProperty property) const {
    return xz_slice(property, summary_.center_y);
}

QImage StaticModelScene::yz_slice(ModelProperty property) const {
    return yz_slice(property, summary_.center_x);
}

QImage StaticModelScene::xy_slice(
    ModelProperty property,
    std::size_t z_index) const {
    return make_slice(property, 0, z_index);
}

QImage StaticModelScene::xz_slice(
    ModelProperty property,
    std::size_t y_index) const {
    return make_slice(property, 1, y_index);
}

QImage StaticModelScene::yz_slice(
    ModelProperty property,
    std::size_t x_index) const {
    return make_slice(property, 2, x_index);
}

QImage StaticModelScene::make_slice(
    ModelProperty property,
    int orientation,
    std::size_t fixed_index) const {
    const auto& grid = model_.grid;
    const auto limit = orientation == 0 ? grid.nz
                                       : orientation == 1 ? grid.ny : grid.nx;
    if (fixed_index >= limit) {
        throw std::out_of_range("model slice index is outside the physical grid");
    }
    const auto& values = property_values(model_, property);
    const auto [minimum, maximum] = property_range(model_, property);

    const auto width = image_dimension(orientation == 2 ? grid.ny : grid.nx);
    const auto height = image_dimension(orientation == 0 ? grid.ny : grid.nz);
    QImage image(width, height, QImage::Format_RGB32);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            std::size_t x = orientation == 2 ? fixed_index : summary_.center_x;
            std::size_t y = orientation == 1 ? fixed_index : summary_.center_y;
            std::size_t z = orientation == 0 ? fixed_index : summary_.center_z;
            if (orientation == 0) {
                x = static_cast<std::size_t>(column);
                y = grid.ny - 1 - static_cast<std::size_t>(row);
            } else if (orientation == 1) {
                x = static_cast<std::size_t>(column);
                z = static_cast<std::size_t>(row);
            } else {
                y = static_cast<std::size_t>(column);
                z = static_cast<std::size_t>(row);
            }
            image.setPixel(
                column,
                row,
                sequential_colour(
                    values[linear_index(grid, x, y, z)], minimum, maximum));
        }
    }
    return image;
}

PhysicalModel StaticModelScene::cropped_model(
    const ModelCropBounds& bounds) const {
    const auto& source_grid = model_.grid;
    if (bounds.x_begin >= bounds.x_end || bounds.x_end > source_grid.nx ||
        bounds.y_begin >= bounds.y_end || bounds.y_end > source_grid.ny ||
        bounds.z_begin >= bounds.z_end || bounds.z_end > source_grid.nz) {
        throw std::invalid_argument("model crop bounds are empty or outside the grid");
    }
    auto output_grid = source_grid;
    output_grid.nx = bounds.x_end - bounds.x_begin;
    output_grid.ny = bounds.y_end - bounds.y_begin;
    output_grid.nz = bounds.z_end - bounds.z_begin;
    const auto count = output_grid.physical_cell_count();
    PhysicalModel output{
        output_grid,
        std::vector<float>(count),
        std::vector<float>(count),
        std::vector<float>(count)};
    for (std::size_t z = 0; z < output_grid.nz; ++z) {
        for (std::size_t y = 0; y < output_grid.ny; ++y) {
            for (std::size_t x = 0; x < output_grid.nx; ++x) {
                const auto source = source_grid.physical_linear_index(
                    bounds.x_begin + x,
                    bounds.y_begin + y,
                    bounds.z_begin + z);
                const auto destination =
                    output_grid.physical_linear_index(x, y, z);
                output.vp_m_s[destination] = model_.vp_m_s[source];
                output.vs_m_s[destination] = model_.vs_m_s[source];
                output.density_kg_m3[destination] =
                    model_.density_kg_m3[source];
            }
        }
    }
    require_valid_physical_model(output);
    return output;
}

} // namespace wave3d::desktop
