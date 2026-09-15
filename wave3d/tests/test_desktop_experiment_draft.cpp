#include "wave3d/desktop/experiment_draft.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Operation>
void expect_rejected(Operation operation, const char* message) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected, message);
}

QByteArray bytes(const QString& path) {
    QFile input(path);
    expect(input.open(QIODevice::ReadOnly), "cannot read test file");
    return input.readAll();
}

wave3d::Grid3D grid() {
    return {
        200,
        200,
        187,
        25.0F,
        25.0F,
        25.0F,
        6,
        {20, 20},
        {20, 20},
        {0, 20}};
}

wave3d::PhysicalModelExtrema extrema() {
    return {
        {2445.75928F, 1412.05981F, 2180.04321F},
        {6000.0F, 3464.10156F, 2728.34644F}};
}

void test_defaults_and_resolution() {
    const auto draft = wave3d::desktop::ExperimentDraftStore::defaults(
        QStringLiteral("shot-001"),
        QStringLiteral("models/elastic.h5"),
        grid(),
        extrema());
    expect(draft.dt_s == 0.001, "Overthrust default dt changed");
    expect(
        draft.design_frequency_hz == 9.0 &&
            draft.wavelet.dominant_frequency_hz == 3.0,
        "Overthrust design/source frequency defaults changed");
    expect(
        draft.source_location_m.x_m == 2500.0 &&
            draft.source_location_m.y_m == 2500.0 &&
            draft.source_location_m.z_m == 1150.0,
        "default source is not on the intended model nodes");
    const auto resolved = wave3d::desktop::ExperimentDraftStore::resolve(
        draft, grid(), extrema());
    expect(
        resolved.simulation.time.step_count() == 3000 &&
            resolved.simulation.top_boundary == wave3d::TopBoundary::FreeSurface &&
            resolved.source.moment.m_xx_nm == 1.0e12 &&
            resolved.source.moment.m_yy_nm == 1.0e12 &&
            resolved.source.moment.m_zz_nm == 1.0e12 &&
            resolved.source.moment.m_xy_nm == 0.0,
        "isotropic draft did not resolve through accepted scientific types");
    expect(
        resolved.numerical.cfl_fraction < 1.0 &&
            resolved.numerical.shear_points_per_wavelength[0] >= 5.0 &&
            resolved.numerical.time_samples_per_period >= 20.0,
        "default draft is not numerically valid");

    auto manual = draft;
    manual.source_mode = wave3d::desktop::DraftSourceMode::MomentTensor;
    manual.moment_tensor_nm = {0.0, 0.0, 0.0, 1.0e12, 0.0, 0.0};
    const auto manual_resolved =
        wave3d::desktop::ExperimentDraftStore::resolve(
            manual, grid(), extrema());
    expect(
        manual_resolved.source.moment.m_xx_nm == 0.0 &&
            manual_resolved.source.moment.m_xy_nm == 1.0e12,
        "manual moment tensor did not remain exact");

    auto invalid = draft;
    invalid.dt_s = 0.01;
    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ExperimentDraftStore::resolve(
                invalid, grid(), extrema()));
        },
        "CFL-unstable draft must be rejected");
    invalid = draft;
    invalid.design_frequency_hz = 20.0;
    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ExperimentDraftStore::resolve(
                invalid, grid(), extrema()));
        },
        "under-resolved design frequency must be rejected");
    invalid = draft;
    invalid.source_location_m.x_m = 5000.0;
    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ExperimentDraftStore::resolve(
                invalid, grid(), extrema()));
        },
        "out-of-domain source must be rejected");
    invalid = manual;
    invalid.moment_tensor_nm = {};
    expect_rejected(
        [&] { wave3d::desktop::ExperimentDraftStore::validate(invalid); },
        "zero manual moment tensor must be rejected");
}

void test_atomic_persistence() {
    QTemporaryDir temporary;
    expect(temporary.isValid(), "cannot create temporary project");
    QDir root(temporary.path());
    expect(root.mkpath(QStringLiteral("source")), "cannot create source directory");
    expect(root.mkpath(QStringLiteral("runs/retained")), "cannot create run sentinel");
    QFile project(root.filePath(QStringLiteral("project.wave3d.json")));
    expect(project.open(QIODevice::WriteOnly), "cannot create project sentinel");
    expect(project.write("project-sentinel") > 0, "cannot write project sentinel");
    project.close();
    QFile run(root.filePath(QStringLiteral("runs/retained/config.yaml")));
    expect(run.open(QIODevice::WriteOnly), "cannot create run sentinel");
    expect(run.write("run-sentinel") > 0, "cannot write run sentinel");
    run.close();

    auto draft = wave3d::desktop::ExperimentDraftStore::defaults(
        QStringLiteral("shot-001"),
        QStringLiteral("models/elastic.h5"),
        grid(),
        extrema());
    wave3d::desktop::ExperimentDraftStore::save(temporary.path(), draft);
    expect(
        wave3d::desktop::ExperimentDraftStore::exists(
            temporary.path(), QStringLiteral("shot-001")),
        "saved draft does not exist");
    auto loaded = wave3d::desktop::ExperimentDraftStore::load(
        temporary.path(), QStringLiteral("shot-001"));
    expect(
        loaded.shot_id == draft.shot_id &&
            loaded.model_reference == draft.model_reference &&
            loaded.dt_s == draft.dt_s &&
            loaded.source_location_m.z_m == draft.source_location_m.z_m &&
            loaded.wavelet.peak_delay_s == draft.wavelet.peak_delay_s,
        "experiment JSON did not round trip exactly");

    draft.total_time_s = 4.25;
    draft.source_mode = wave3d::desktop::DraftSourceMode::MomentTensor;
    draft.moment_tensor_nm = {0.0, 0.0, 0.0, 1.0e12, 0.0, 0.0};
    wave3d::desktop::ExperimentDraftStore::save(temporary.path(), draft);
    loaded = wave3d::desktop::ExperimentDraftStore::load(
        temporary.path(), QStringLiteral("shot-001"));
    expect(
        loaded.total_time_s == 4.25 &&
            loaded.source_mode == wave3d::desktop::DraftSourceMode::MomentTensor &&
            loaded.moment_tensor_nm.m_xy_nm == 1.0e12,
        "atomic draft replacement did not retain the new complete document");
    expect(
        bytes(root.filePath(QStringLiteral("project.wave3d.json"))) ==
                QByteArray("project-sentinel") &&
            bytes(root.filePath(QStringLiteral("runs/retained/config.yaml"))) ==
                QByteArray("run-sentinel"),
        "draft persistence changed project or prepared-run files");
    expect(
        root.entryList(QStringList{QStringLiteral("*.XXXXXX")}, QDir::Files)
            .isEmpty(),
        "atomic draft save retained a temporary file");

    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ExperimentDraftStore::relative_path(
                QStringLiteral("../shot")));
        },
        "unsafe shot identity must be rejected");
    draft.model_reference = QStringLiteral("../escape.h5");
    expect_rejected(
        [&] {
            wave3d::desktop::ExperimentDraftStore::save(
                temporary.path(), draft);
        },
        "unsafe model reference must be rejected");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        test_defaults_and_resolution();
        test_atomic_persistence();
        std::cout << "desktop experiment draft tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "desktop experiment draft test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
