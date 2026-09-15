#pragma once

#include "wave3d/model/physical_model.hpp"

#include <QImage>
#include <QString>

#include <cstddef>

namespace wave3d::desktop {

enum class ModelProperty { Vp, Vs, Density };

struct StaticModelSummary {
    Grid3D grid{};
    PhysicalPoint3D maximum_coordinate_m{};
    PhysicalModelExtrema extrema{};
    std::size_t center_x{0};
    std::size_t center_y{0};
    std::size_t center_z{0};
};

struct ModelCropBounds {
    std::size_t x_begin{0};
    std::size_t x_end{0};
    std::size_t y_begin{0};
    std::size_t y_end{0};
    std::size_t z_begin{0};
    std::size_t z_end{0};
};

class StaticModelScene final {
public:
    [[nodiscard]] static StaticModelScene load_hdf5(const QString& path);

    explicit StaticModelScene(PhysicalModel model, QString source_path = {});

    [[nodiscard]] const StaticModelSummary& summary() const noexcept;
    [[nodiscard]] const QString& source_path() const noexcept;
    [[nodiscard]] QImage xy_slice(ModelProperty property) const;
    [[nodiscard]] QImage xz_slice(ModelProperty property) const;
    [[nodiscard]] QImage yz_slice(ModelProperty property) const;
    [[nodiscard]] QImage xy_slice(
        ModelProperty property,
        std::size_t z_index) const;
    [[nodiscard]] QImage xz_slice(
        ModelProperty property,
        std::size_t y_index) const;
    [[nodiscard]] QImage yz_slice(
        ModelProperty property,
        std::size_t x_index) const;
    [[nodiscard]] PhysicalModel cropped_model(
        const ModelCropBounds& bounds) const;

private:
    [[nodiscard]] QImage make_slice(
        ModelProperty property,
        int orientation,
        std::size_t fixed_index) const;

    PhysicalModel model_;
    QString source_path_;
    StaticModelSummary summary_;
};

} // namespace wave3d::desktop
