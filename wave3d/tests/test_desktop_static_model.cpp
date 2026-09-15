#include "wave3d/desktop/static_model_scene.hpp"

#include "wave3d/io/hdf5.hpp"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

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
