#include "wave3d/desktop/static_model_scene.hpp"

#include "wave3d/io/hdf5.hpp"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

wave3d::PhysicalModel fixture() {
    const wave3d::Grid3D grid{
        4, 3, 5,
        10.0F, 20.0F, 30.0F,
        6,
        {2, 2}, {2, 2}, {0, 2}};
    auto model = wave3d::make_homogeneous_model(
        grid, {3600.0F, 2000.0F, 2400.0F});
    const auto low = 0 + grid.nx * (0 + grid.ny * (grid.nz / 2));
    const auto high =
        (grid.nx - 1) + grid.nx * ((grid.ny - 1) + grid.ny * (grid.nz / 2));
    model.vp_m_s[low] = 3000.0F;
    model.vp_m_s[high] = 4500.0F;
    model.vs_m_s[low] = 1700.0F;
    model.vs_m_s[high] = 2500.0F;
    const auto density_low =
        (grid.nx / 2) + grid.nx * (0 + grid.ny * (grid.nz / 2));
    const auto density_high =
        (grid.nx / 2) + grid.nx * ((grid.ny - 1) + grid.ny * (grid.nz / 2));
    model.density_kg_m3[density_low] = 2200.0F;
    model.density_kg_m3[density_high] = 2700.0F;
    return model;
}

void test_static_model_contract() {
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary directory creation failed");
    const auto path = QDir(temporary.path()).filePath(QStringLiteral("model.h5"));
    wave3d::io::write_hdf5_model(path.toStdString(), fixture());

    const auto scene = wave3d::desktop::StaticModelScene::load_hdf5(path);
    const auto& summary = scene.summary();
    expect(
        summary.grid.nx == 4 && summary.grid.ny == 3 && summary.grid.nz == 5,
        "static model dimensions changed");
    expect(
        summary.maximum_coordinate_m.x_m == 30.0 &&
            summary.maximum_coordinate_m.y_m == 40.0 &&
            summary.maximum_coordinate_m.z_m == 120.0,
        "static model physical extents changed");
    expect(
        summary.center_x == 2 && summary.center_y == 1 && summary.center_z == 2,
        "central section indices changed");
    expect(
        summary.extrema.minimum.vp_m_s == 3000.0F &&
            summary.extrema.maximum.vp_m_s == 4500.0F &&
            summary.extrema.minimum.vs_m_s == 1700.0F &&
            summary.extrema.maximum.density_kg_m3 == 2700.0F,
        "static model extrema changed");

    const auto xy = scene.xy_slice(wave3d::desktop::ModelProperty::Vp);
    const auto xz = scene.xz_slice(wave3d::desktop::ModelProperty::Vp);
    const auto yz = scene.yz_slice(wave3d::desktop::ModelProperty::Density);
    expect(xy.size() == QSize(4, 3), "XY section dimensions are incorrect");
    expect(xz.size() == QSize(4, 5), "XZ section dimensions are incorrect");
    expect(yz.size() == QSize(3, 5), "YZ section dimensions are incorrect");
    expect(
        xy.pixelColor(0, 2) == QColor(8, 29, 55),
        "XY y-axis orientation or minimum colour changed");
    expect(
        xy.pixelColor(3, 0) == QColor(250, 221, 90),
        "XY y-axis orientation or maximum colour changed");
    expect(
        yz.pixelColor(0, 2) == QColor(8, 29, 55),
        "density selection or YZ section indexing changed");
    expect(
        yz.pixelColor(2, 2) == QColor(250, 221, 90),
        "density maximum colour or YZ section indexing changed");
    expect(
        scene.source_path() == QFileInfo(path).absoluteFilePath(),
        "model source path was not canonicalized");

    const auto top = scene.xy_slice(wave3d::desktop::ModelProperty::Vp, 0);
    expect(top.size() == QSize(4, 3), "arbitrary XY section size changed");
    expect(
        top.pixelColor(0, 2) != xy.pixelColor(0, 2),
        "arbitrary XY index did not select a different physical plane");
    bool rejected = false;
    try {
        static_cast<void>(
            scene.yz_slice(wave3d::desktop::ModelProperty::Vp, 4));
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    expect(rejected, "out-of-range model slice must be rejected");

    const auto cropped = scene.cropped_model({1, 4, 0, 2, 1, 5});
    expect(
        cropped.grid.nx == 3 && cropped.grid.ny == 2 && cropped.grid.nz == 4,
        "crop dimensions changed");
    expect(
        cropped.grid.dx_m == summary.grid.dx_m &&
            cropped.grid.dy_m == summary.grid.dy_m &&
            cropped.grid.dz_m == summary.grid.dz_m &&
            cropped.grid.halo == summary.grid.halo &&
            cropped.grid.x_boundary.lower_absorbing ==
                summary.grid.x_boundary.lower_absorbing,
        "crop did not preserve spacing or storage metadata");
    const auto source_model = fixture();
    for (std::size_t z = 0; z < cropped.grid.nz; ++z) {
        for (std::size_t y = 0; y < cropped.grid.ny; ++y) {
            for (std::size_t x = 0; x < cropped.grid.nx; ++x) {
                const auto destination =
                    cropped.grid.physical_linear_index(x, y, z);
                const auto source_index = source_model.grid.physical_linear_index(
                    x + 1, y, z + 1);
                expect(
                    cropped.vp_m_s[destination] ==
                            source_model.vp_m_s[source_index] &&
                        cropped.vs_m_s[destination] ==
                            source_model.vs_m_s[source_index] &&
                        cropped.density_kg_m3[destination] ==
                            source_model.density_kg_m3[source_index],
                    "crop changed canonical three-property sample mapping");
            }
        }
    }
    rejected = false;
    try {
        static_cast<void>(scene.cropped_model({1, 1, 0, 2, 0, 2}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "empty crop must be rejected");

    const auto texture =
        scene.volume_texture(wave3d::desktop::ModelProperty::Vp);
    expect(
        texture.nx == 4 && texture.ny == 3 && texture.nz == 5 &&
            texture.normalized_values.size() == fixture().cell_count(),
        "volume texture shape changed");
    expect(
        texture.normalized_values[
            summary.grid.physical_linear_index(0, 0, summary.center_z)] == 0.0F &&
            texture.normalized_values[summary.grid.physical_linear_index(
                3, 2, summary.center_z)] == 1.0F,
        "volume normalization endpoints changed or sample order was permuted");
    expect(
        texture.physical_aspect[0] == 0.25F &&
            texture.physical_aspect[1] == (1.0F / 3.0F) &&
            texture.physical_aspect[2] == 1.0F,
        "volume physical aspect changed");
    const auto source_after_texture =
        scene.cropped_model({0, 4, 0, 3, 0, 5});
    expect(
        source_after_texture.vp_m_s == fixture().vp_m_s &&
            source_after_texture.vs_m_s == fixture().vs_m_s &&
            source_after_texture.density_kg_m3 == fixture().density_kg_m3,
        "volume preparation changed the source physical arrays");
    const auto normalized_crop =
        scene.normalized_crop_bounds({1, 4, 0, 2, 1, 5});
    expect(
        normalized_crop[0] == (1.0F / 3.0F) && normalized_crop[1] == 1.0F &&
            normalized_crop[2] == 0.0F && normalized_crop[3] == 0.5F &&
        normalized_crop[4] == 0.25F && normalized_crop[5] == 1.0F,
        "volume crop bounds do not map to texture coordinates");
    rejected = false;
    try {
        static_cast<void>(
            scene.normalized_crop_bounds({0, 5, 0, 2, 0, 2}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "invalid volume crop bounds must be rejected");

    const auto constant = wave3d::desktop::StaticModelScene(
                              wave3d::make_homogeneous_model(
                                  summary.grid,
                                  {3600.0F, 2000.0F, 2400.0F}))
                              .volume_texture(
                                  wave3d::desktop::ModelProperty::Density);
    expect(
        std::all_of(
            constant.normalized_values.begin(),
            constant.normalized_values.end(),
            [](float value) { return value == 0.5F; }),
        "constant volume normalization must use the stable midpoint");
}

} // namespace

int main() {
    try {
        test_static_model_contract();
        std::cout << "desktop static model tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "desktop static model test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
