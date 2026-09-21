#include "wave3d/desktop/experiment_draft.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <limits>
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
    expect(
        draft.acquisition.has_value() &&
            draft.acquisition->mode ==
                wave3d::desktop::ReceiverGeometryMode::SurfaceRectangular &&
            draft.acquisition->rectangular.count_x == 101 &&
            draft.acquisition->rectangular.count_y == 101 &&
            resolved.receivers.size() == 10201 &&
            resolved.acquisition.receiver_count == 10201 &&
            resolved.acquisition.sample_count == 3000,
        "default 101 by 101 acquisition was not resolved");
    expect(
        resolved.receivers.front().x_m == 0.0 &&
            resolved.receivers.front().y_m == 0.0 &&
            resolved.receivers[100].x_m == 4975.0 &&
            resolved.receivers[100].y_m == 0.0 &&
            resolved.receivers[101].x_m == 0.0 &&
            resolved.receivers[101].y_m == 49.75 &&
            resolved.receivers.back().x_m == 4975.0 &&
            resolved.receivers.back().y_m == 4975.0,
        "surface receiver endpoints or x-fastest order changed");
    expect(
        resolved.acquisition.raw_trace_bytes ==
                std::size_t{10201} * 3000 * 3 * sizeof(float) &&
            resolved.acquisition.segy_bytes ==
                3 * 3600 + std::size_t{10201} * 3 *
                               (240 + 3000 * sizeof(float)),
        "three-component trace or SEG-Y byte estimate is incorrect");

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

    auto double_couple = draft;
    double_couple.source_mode =
        wave3d::desktop::DraftSourceMode::DoubleCouple;
    double_couple.double_couple = {2.0e12, 0.0, 90.0, 0.0};
    const auto double_couple_resolved =
        wave3d::desktop::ExperimentDraftStore::resolve(
            double_couple, grid(), extrema());
    expect(
        std::abs(double_couple_resolved.source.moment.m_xy_nm - 2.0e12) <
                1.0e-3 &&
            std::abs(double_couple_resolved.source.moment.m_xx_nm) < 1.0e-3 &&
            std::abs(double_couple_resolved.source.moment.m_yy_nm) < 1.0e-3,
        "double-couple draft did not resolve through the core conversion");

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
    invalid = double_couple;
    invalid.double_couple.strike_deg = 360.0;
    expect_rejected(
        [&] { wave3d::desktop::ExperimentDraftStore::validate(invalid); },
        "out-of-range double-couple strike must be rejected");
    invalid = draft;
    invalid.acquisition->rectangular.count_x = 1;
    expect_rejected(
        [&] { wave3d::desktop::ExperimentDraftStore::validate(invalid); },
        "single-point receiver axis must be rejected");
    invalid = draft;
    invalid.acquisition->rectangular.minimum_x_m =
        invalid.acquisition->rectangular.maximum_x_m;
    expect_rejected(
        [&] { wave3d::desktop::ExperimentDraftStore::validate(invalid); },
        "degenerate receiver aperture must be rejected");
    invalid = draft;
    invalid.acquisition->rectangular.depth_m = 25.0;
    expect_rejected(
        [&] { wave3d::desktop::ExperimentDraftStore::validate(invalid); },
        "non-surface receiver grid must be rejected");
    invalid = draft;
    invalid.acquisition->rectangular.maximum_x_m = 5000.0;
    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ExperimentDraftStore::resolve(
                invalid, grid(), extrema()));
        },
        "out-of-domain receiver aperture must be rejected");
    expect_rejected(
        [] {
            static_cast<void>(
                wave3d::desktop::ExperimentDraftStore::acquisition_estimate(
                    std::numeric_limits<std::size_t>::max(), 2));
        },
        "receiver storage overflow must be rejected");
}

void test_acquisition_geometry_and_templates() {
    auto draft = wave3d::desktop::ExperimentDraftStore::defaults(
        QStringLiteral("shot-001"),
        QStringLiteral("models/elastic.h5"),
        grid(),
        extrema());
    draft.acquisition->mode =
        wave3d::desktop::ReceiverGeometryMode::SurfaceLine;
    draft.acquisition->line = {5, 100.0, 200.0, 500.0, 600.0, 0.0};
    draft.acquisition->translate_x_m = 10.0;
    draft.acquisition->translate_y_m = 20.0;
    auto resolved = wave3d::desktop::ExperimentDraftStore::resolve(
        draft, grid(), extrema());
    expect(
        resolved.receivers.size() == 5 &&
            resolved.receivers.front().x_m == 110.0 &&
            resolved.receivers.front().y_m == 220.0 &&
            resolved.receivers[2].x_m == 310.0 &&
            resolved.receivers[2].y_m == 420.0 &&
            resolved.receivers.back().x_m == 510.0 &&
            resolved.receivers.back().y_m == 620.0,
        "translated diagonal receiver line is incorrect");

    const auto csv = wave3d::desktop::ExperimentDraftStore::parse_receiver_csv(
        QByteArray("x_m,y_m,z_m\n25,50,0\n75,100,0\n125,150,0\n"));
    expect(
        csv.size() == 3 && csv[0].x_m == 25.0 && csv[1].y_m == 100.0 &&
            csv[2].x_m == 125.0,
        "receiver CSV did not preserve row order");
    draft.acquisition->mode =
        wave3d::desktop::ReceiverGeometryMode::ExplicitCoordinates;
    draft.acquisition->explicit_coordinates = csv;
    draft.acquisition->translate_x_m = 5.0;
    draft.acquisition->translate_y_m = -10.0;
    resolved = wave3d::desktop::ExperimentDraftStore::resolve(
        draft, grid(), extrema());
    expect(
        resolved.receivers.size() == 3 &&
            resolved.receivers[0].x_m == 30.0 &&
            resolved.receivers[0].y_m == 40.0,
        "explicit receiver translation is incorrect");

    expect_rejected(
        [] {
            static_cast<void>(
                wave3d::desktop::ExperimentDraftStore::parse_receiver_csv(
                    QByteArray("x_m,y_m,z_m\n1,2,0\n1,2,0\n")));
        },
        "duplicate CSV receivers must be rejected");
    expect_rejected(
        [] {
            static_cast<void>(
                wave3d::desktop::ExperimentDraftStore::parse_receiver_csv(
                    QByteArray("x_m,y_m,z_m\n1,2,3\n")));
        },
        "buried CSV receiver must be rejected");
    expect_rejected(
        [] {
            static_cast<void>(
                wave3d::desktop::ExperimentDraftStore::parse_receiver_csv(
                    QByteArray("x,y,z\n1,2,0\n")));
        },
        "unexpected CSV header must be rejected");

    auto duplicate = *draft.acquisition;
    duplicate.explicit_coordinates = {{10.0, 20.0, 0.0}, {10.0, 20.0, 0.0}};
    expect_rejected(
        [&] {
            static_cast<void>(
                wave3d::desktop::ExperimentDraftStore::generate_receivers(
                    duplicate, grid()));
        },
        "duplicate explicit receiver geometry must be rejected");
    auto outside = *draft.acquisition;
    outside.translate_x_m = 5000.0;
    expect_rejected(
        [&] {
            static_cast<void>(
                wave3d::desktop::ExperimentDraftStore::generate_receivers(
                    outside, grid()));
        },
        "translated out-of-domain receivers must be rejected");
    auto excessive = wave3d::desktop::ExperimentDraftStore::default_acquisition(
        grid());
    excessive.mode = wave3d::desktop::ReceiverGeometryMode::SurfaceLine;
    excessive.line.count = 1'100'001;
    expect_rejected(
        [&] {
            static_cast<void>(
                wave3d::desktop::ExperimentDraftStore::generate_receivers(
                    excessive, grid()));
        },
        "receiver safety limit must be enforced before allocation");
    auto nonfinite = *draft.acquisition;
    nonfinite.translate_y_m = std::numeric_limits<double>::infinity();
    expect_rejected(
        [&] {
            static_cast<void>(
                wave3d::desktop::ExperimentDraftStore::generate_receivers(
                    nonfinite, grid()));
        },
        "non-finite receiver translation must be rejected");

    QTemporaryDir temporary;
    expect(temporary.isValid(), "cannot create acquisition template directory");
    const auto template_path =
        QDir(temporary.path()).filePath(QStringLiteral("line.wave3d-acquisition.json"));
    auto line = wave3d::desktop::ExperimentDraftStore::default_acquisition(grid());
    line.mode = wave3d::desktop::ReceiverGeometryMode::SurfaceLine;
    line.line = {7, 0.0, 100.0, 600.0, 100.0, 0.0};
    line.translate_y_m = 25.0;
    wave3d::desktop::ExperimentDraftStore::save_acquisition_template(
        template_path, line);
    const auto loaded =
        wave3d::desktop::ExperimentDraftStore::load_acquisition_template(
            template_path);
    expect(
        loaded.mode == wave3d::desktop::ReceiverGeometryMode::SurfaceLine &&
            loaded.line.count == 7 && loaded.line.last_x_m == 600.0 &&
            loaded.translate_y_m == 25.0,
        "acquisition template did not round trip");
    QFile malformed(template_path);
    expect(
        malformed.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "cannot create malformed acquisition template");
    malformed.write("{}");
    malformed.close();
    expect_rejected(
        [&] {
            static_cast<void>(
                wave3d::desktop::ExperimentDraftStore::load_acquisition_template(
                    template_path));
        },
        "malformed acquisition template must be rejected");
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
            loaded.wavelet.peak_delay_s == draft.wavelet.peak_delay_s &&
            loaded.acquisition.has_value() &&
            loaded.acquisition->rectangular.count_x == 101 &&
            loaded.acquisition->rectangular.maximum_y_m == 4975.0,
        "experiment JSON did not round trip exactly");

    draft.total_time_s = 4.25;
    draft.source_mode = wave3d::desktop::DraftSourceMode::DoubleCouple;
    draft.double_couple = {3.0e12, 123.0, 38.0, -47.0};
    draft.acquisition->mode =
        wave3d::desktop::ReceiverGeometryMode::ExplicitCoordinates;
    draft.acquisition->explicit_coordinates = {
        {100.0, 200.0, 0.0}, {300.0, 400.0, 0.0}};
    draft.acquisition->translate_x_m = 25.0;
    wave3d::desktop::ExperimentDraftStore::save(temporary.path(), draft);
    loaded = wave3d::desktop::ExperimentDraftStore::load(
        temporary.path(), QStringLiteral("shot-001"));
    expect(
        loaded.total_time_s == 4.25 &&
            loaded.source_mode ==
                wave3d::desktop::DraftSourceMode::DoubleCouple &&
            loaded.double_couple.scalar_moment_nm == 3.0e12 &&
            loaded.double_couple.strike_deg == 123.0 &&
            loaded.double_couple.dip_deg == 38.0 &&
            loaded.double_couple.rake_deg == -47.0 &&
            loaded.acquisition->mode ==
                wave3d::desktop::ReceiverGeometryMode::ExplicitCoordinates &&
            loaded.acquisition->explicit_coordinates.size() == 2 &&
            loaded.acquisition->explicit_coordinates[1].y_m == 400.0 &&
            loaded.acquisition->translate_x_m == 25.0,
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

    const auto draft_path = root.filePath(
        wave3d::desktop::ExperimentDraftStore::relative_path(
            QStringLiteral("shot-001")));
    auto legacy_root = QJsonDocument::fromJson(bytes(draft_path)).object();
    const auto current_acquisition =
        legacy_root.value(QStringLiteral("acquisition")).toObject();
    const auto rectangular =
        current_acquisition.value(QStringLiteral("rectangular")).toObject();
    legacy_root.insert(
        QStringLiteral("acquisition"),
        QJsonObject{
            {QStringLiteral("mode"), QStringLiteral("surface_rectangular")},
            {QStringLiteral("count_x"),
             rectangular.value(QStringLiteral("count_x"))},
            {QStringLiteral("count_y"),
             rectangular.value(QStringLiteral("count_y"))},
            {QStringLiteral("x_range_m"),
             rectangular.value(QStringLiteral("x_range_m"))},
            {QStringLiteral("y_range_m"),
             rectangular.value(QStringLiteral("y_range_m"))},
            {QStringLiteral("depth_m"),
             rectangular.value(QStringLiteral("depth_m"))},
            {QStringLiteral("components"),
             QJsonArray{QStringLiteral("vx"), QStringLiteral("vy"),
                        QStringLiteral("vz")}}});
    legacy_root.insert(
        QStringLiteral("schema"),
        QString::fromUtf8(wave3d::desktop::kLegacyExperimentDraftSchemaV3));
    QFile legacy_file(draft_path);
    expect(
        legacy_file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "cannot write version-3 draft fixture");
    expect(
        legacy_file.write(QJsonDocument(legacy_root).toJson()) > 0,
        "cannot publish version-3 draft fixture");
    legacy_file.close();
    const auto legacy_v3 = wave3d::desktop::ExperimentDraftStore::load(
        temporary.path(), QStringLiteral("shot-001"));
    expect(
        legacy_v3.acquisition.has_value() &&
            legacy_v3.source_mode ==
                wave3d::desktop::DraftSourceMode::DoubleCouple &&
            legacy_v3.double_couple.strike_deg == 123.0,
        "version-3 draft did not retain source and rectangular acquisition");

    legacy_root.insert(
        QStringLiteral("schema"),
        QString::fromUtf8(wave3d::desktop::kLegacyExperimentDraftSchemaV2));
    auto legacy_v2_source = legacy_root.value(QStringLiteral("source")).toObject();
    legacy_v2_source.insert(
        QStringLiteral("mode"), QStringLiteral("isotropic_explosion"));
    legacy_v2_source.remove(QStringLiteral("double_couple"));
    legacy_root.insert(QStringLiteral("source"), legacy_v2_source);
    expect(
        legacy_file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "cannot write version-2 draft fixture");
    expect(
        legacy_file.write(QJsonDocument(legacy_root).toJson()) > 0,
        "cannot publish version-2 draft fixture");
    legacy_file.close();
    const auto legacy_v2 = wave3d::desktop::ExperimentDraftStore::load(
        temporary.path(), QStringLiteral("shot-001"));
    expect(
        legacy_v2.acquisition.has_value() &&
            legacy_v2.source_mode ==
                wave3d::desktop::DraftSourceMode::IsotropicExplosion &&
            legacy_v2.double_couple.scalar_moment_nm == 1.0e12,
        "version-2 draft did not migrate with safe double-couple defaults");

    legacy_root.insert(
        QStringLiteral("schema"),
        QString::fromUtf8(wave3d::desktop::kLegacyExperimentDraftSchema));
    legacy_root.remove(QStringLiteral("acquisition"));
    expect(
        legacy_file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "cannot write legacy draft fixture");
    expect(
        legacy_file.write(QJsonDocument(legacy_root).toJson()) > 0,
        "cannot publish legacy draft fixture");
    legacy_file.close();
    const auto legacy = wave3d::desktop::ExperimentDraftStore::load(
        temporary.path(), QStringLiteral("shot-001"));
    expect(
        !legacy.acquisition.has_value(),
        "version-1 draft did not retain its missing-acquisition migration marker");
    expect_rejected(
        [&] {
            static_cast<void>(wave3d::desktop::ExperimentDraftStore::resolve(
                legacy, grid(), extrema()));
        },
        "legacy draft without model-derived acquisition must not resolve");

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
        test_acquisition_geometry_and_templates();
        test_atomic_persistence();
        std::cout << "desktop experiment draft tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "desktop experiment draft test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
