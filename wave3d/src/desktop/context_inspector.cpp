#include "wave3d/desktop/context_inspector.hpp"

#include "wave3d/desktop/experiment_controller.hpp"
#include "wave3d/desktop/inspector_edit_footer.hpp"
#include "wave3d/desktop/selection_controller.hpp"

#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace wave3d::desktop {
namespace {

QString number(double value) {
    return QString::number(value, 'g', 8);
}

QString time_value(double seconds) {
    if (seconds < 1.0) {
        return QStringLiteral("%1 ms (%2 s)")
            .arg(number(seconds * 1000.0), number(seconds));
    }
    return QStringLiteral("%1 s").arg(number(seconds));
}

QString range_value(double minimum, double maximum) {
    return QStringLiteral("%1 … %2 m").arg(number(minimum), number(maximum));
}

QString source_type(DraftSourceMode mode) {
    switch (mode) {
    case DraftSourceMode::IsotropicExplosion:
        return QStringLiteral("Isotropic explosion moment tensor");
    case DraftSourceMode::MomentTensor:
        return QStringLiteral("Symmetric moment tensor");
    case DraftSourceMode::DoubleCouple:
        return QStringLiteral("Double couple (strike/dip/rake)");
    }
    return QStringLiteral("Unknown");
}

QLabel* value_label(QWidget* parent, const char* object_name) {
    auto* label = new QLabel(QStringLiteral("—"), parent);
    label->setObjectName(QString::fromUtf8(object_name));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

void add_row(QGridLayout* layout, int row, const QString& name, QLabel* value) {
    auto* caption = new QLabel(name, value->parentWidget());
    caption->setProperty("secondaryText", true);
    caption->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(caption, row, 0);
    layout->addWidget(value, row, 1);
    layout->setColumnStretch(1, 1);
}

void add_control_row(
    QGridLayout* layout,
    int row,
    const QString& name,
    QWidget* control) {
    auto* caption = new QLabel(name, control->parentWidget());
    caption->setProperty("secondaryText", true);
    caption->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(caption, row, 0);
    layout->addWidget(control, row, 1);
    layout->setColumnStretch(1, 1);
}

QWidget* section(
    QWidget* parent,
    const QString& title,
    std::initializer_list<std::pair<QString, QLabel*>> rows) {
    auto* widget = new QWidget(parent);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 4, 0, 8);
    layout->setSpacing(7);
    auto* heading = new QLabel(title, widget);
    heading->setProperty("panelTitle", true);
    layout->addWidget(heading);
    auto* separator = new QFrame(widget);
    separator->setFrameShape(QFrame::HLine);
    separator->setProperty("inspectorSeparator", true);
    layout->addWidget(separator);
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(7);
    int row = 0;
    for (const auto& item : rows) add_row(grid, row++, item.first, item.second);
    layout->addLayout(grid);
    return widget;
}

QWidget* control_section(
    QWidget* parent,
    const QString& title,
    std::initializer_list<std::pair<QString, QWidget*>> rows) {
    auto* widget = new QWidget(parent);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 4, 0, 8);
    layout->setSpacing(7);
    auto* heading = new QLabel(title, widget);
    heading->setProperty("panelTitle", true);
    layout->addWidget(heading);
    auto* separator = new QFrame(widget);
    separator->setFrameShape(QFrame::HLine);
    separator->setProperty("inspectorSeparator", true);
    layout->addWidget(separator);
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(7);
    int row = 0;
    for (const auto& item : rows) {
        add_control_row(grid, row++, item.first, item.second);
    }
    layout->addLayout(grid);
    return widget;
}

QDoubleSpinBox* source_spin(
    QWidget* parent,
    const char* object_name,
    double minimum,
    double maximum,
    int decimals,
    const QString& suffix) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setObjectName(QString::fromUtf8(object_name));
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setKeyboardTracking(false);
    spin->setSuffix(suffix);
    spin->setMinimumWidth(128);
    return spin;
}

QWidget* inspector_page(
    QWidget* parent,
    const char* object_name,
    const QString& title,
    const QString& subtitle,
    const std::function<void(QVBoxLayout*, QWidget*)>& populate) {
    auto* scroll = new QScrollArea(parent);
    scroll->setObjectName(QString::fromUtf8(object_name));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* contents = new QWidget(scroll);
    auto* layout = new QVBoxLayout(contents);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(7);
    auto* heading = new QLabel(title, contents);
    heading->setProperty("workspaceTitle", true);
    layout->addWidget(heading);
    auto* detail = new QLabel(subtitle, contents);
    detail->setProperty("secondaryText", true);
    detail->setWordWrap(true);
    layout->addWidget(detail);
    layout->addSpacing(8);
    populate(layout, contents);
    layout->addStretch();
    scroll->setWidget(contents);
    return scroll;
}

QWidget* message_page(
    QWidget* parent,
    const char* object_name,
    const QString& title,
    const QString& message) {
    return inspector_page(
        parent, object_name, title, message,
        [](QVBoxLayout*, QWidget*) {});
}

} // namespace

ContextInspector::ContextInspector(
    SelectionController* selection_controller,
    ExperimentController* experiment_controller,
    QWidget* parent)
    : QWidget(parent),
      selection_controller_(selection_controller),
      experiment_controller_(experiment_controller) {
    setObjectName(QStringLiteral("contextInspector"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("contextInspectorPages"));
    layout->addWidget(pages_);

    empty_page_ = message_page(
        pages_, "emptyInspector", QStringLiteral("No selection"),
        QStringLiteral("Select an item from the Project Navigator to inspect its properties."));

    project_page_ = inspector_page(
        pages_, "projectInspector", QStringLiteral("Project"),
        QStringLiteral("Current project information"),
        [this](QVBoxLayout* page, QWidget* contents) {
            project_name_ = value_label(contents, "projectInspectorName");
            project_id_ = value_label(contents, "projectInspectorId");
            project_path_ = value_label(contents, "projectInspectorPath");
            project_model_file_ = value_label(contents, "projectInspectorModelFile");
            project_model_status_ = value_label(contents, "projectInspectorModelStatus");
            project_shot_count_ = value_label(contents, "projectInspectorShotCount");
            project_run_count_ = value_label(contents, "projectInspectorRunCount");
            project_result_count_ = value_label(contents, "projectInspectorResultCount");
            page->addWidget(section(contents, QStringLiteral("General"), {
                {QStringLiteral("Name"), project_name_},
                {QStringLiteral("Project ID"), project_id_},
                {QStringLiteral("Project Path"), project_path_}}));
            page->addWidget(section(contents, QStringLiteral("Model"), {
                {QStringLiteral("Model File"), project_model_file_},
                {QStringLiteral("Status"), project_model_status_}}));
            page->addWidget(section(contents, QStringLiteral("Acquisition"), {
                {QStringLiteral("Shot Count"), project_shot_count_}}));
            page->addWidget(section(contents, QStringLiteral("Runs"), {
                {QStringLiteral("Run Count"), project_run_count_}}));
            page->addWidget(section(contents, QStringLiteral("Results"), {
                {QStringLiteral("Result Count"), project_result_count_}}));
        });

    model_page_ = inspector_page(
        pages_, "modelInspector", QStringLiteral("Model"),
        QStringLiteral("Loaded physical model"),
        [this](QVBoxLayout* page, QWidget* contents) {
            model_file_ = value_label(contents, "modelInspectorFile");
            model_dimensions_ = value_label(contents, "modelInspectorDimensions");
            model_spacing_ = value_label(contents, "modelInspectorSpacing");
            model_extent_ = value_label(contents, "modelInspectorExtent");
            model_vp_available_ = value_label(contents, "modelInspectorVpAvailable");
            model_vs_available_ = value_label(contents, "modelInspectorVsAvailable");
            model_density_available_ = value_label(contents, "modelInspectorDensityAvailable");
            model_status_ = value_label(contents, "modelInspectorStatus");
            page->addWidget(section(contents, QStringLiteral("File"), {
                {QStringLiteral("Path"), model_file_}}));
            page->addWidget(section(contents, QStringLiteral("Geometry"), {
                {QStringLiteral("Dimensions"), model_dimensions_},
                {QStringLiteral("Spacing"), model_spacing_},
                {QStringLiteral("Physical Extent"), model_extent_}}));
            page->addWidget(section(contents, QStringLiteral("Properties"), {
                {QStringLiteral("Vp available"), model_vp_available_},
                {QStringLiteral("Vs available"), model_vs_available_},
                {QStringLiteral("Density available"), model_density_available_}}));
            page->addWidget(section(contents, QStringLiteral("Status"), {
                {QStringLiteral("State"), model_status_}}));
        });

    model_property_page_ = inspector_page(
        pages_, "modelPropertyInspector", QStringLiteral("Model Property"),
        QStringLiteral("Selected physical property"),
        [this](QVBoxLayout* page, QWidget* contents) {
            property_title_ = value_label(contents, "modelPropertyInspectorSelected");
            property_name_ = value_label(contents, "modelPropertyInspectorName");
            property_unit_ = value_label(contents, "modelPropertyInspectorUnit");
            property_minimum_ = value_label(contents, "modelPropertyInspectorMinimum");
            property_maximum_ = value_label(contents, "modelPropertyInspectorMaximum");
            property_dimensions_ = value_label(contents, "modelPropertyInspectorDimensions");
            property_spacing_ = value_label(contents, "modelPropertyInspectorSpacing");
            property_current_field_ = value_label(contents, "modelPropertyInspectorCurrentField");
            page->addWidget(section(contents, QStringLiteral("Selected"), {
                {QStringLiteral("Property"), property_title_}}));
            page->addWidget(section(contents, QStringLiteral("Information"), {
                {QStringLiteral("Property"), property_name_},
                {QStringLiteral("Unit"), property_unit_}}));
            page->addWidget(section(contents, QStringLiteral("Range"), {
                {QStringLiteral("Minimum"), property_minimum_},
                {QStringLiteral("Maximum"), property_maximum_}}));
            page->addWidget(section(contents, QStringLiteral("Grid"), {
                {QStringLiteral("Dimensions"), property_dimensions_},
                {QStringLiteral("Spacing"), property_spacing_}}));
            page->addWidget(section(contents, QStringLiteral("Display"), {
                {QStringLiteral("Current field"), property_current_field_}}));
        });

    grid_page_ = inspector_page(
        pages_, "gridInspector", QStringLiteral("Grid"),
        QStringLiteral("Physical model grid geometry"),
        [this](QVBoxLayout* page, QWidget* contents) {
            grid_nx_ = value_label(contents, "gridInspectorNx");
            grid_ny_ = value_label(contents, "gridInspectorNy");
            grid_nz_ = value_label(contents, "gridInspectorNz");
            grid_dx_ = value_label(contents, "gridInspectorDx");
            grid_dy_ = value_label(contents, "gridInspectorDy");
            grid_dz_ = value_label(contents, "gridInspectorDz");
            grid_lx_ = value_label(contents, "gridInspectorLx");
            grid_ly_ = value_label(contents, "gridInspectorLy");
            grid_lz_ = value_label(contents, "gridInspectorLz");
            grid_x_range_ = value_label(contents, "gridInspectorXRange");
            grid_y_range_ = value_label(contents, "gridInspectorYRange");
            grid_z_range_ = value_label(contents, "gridInspectorZRange");
            grid_total_cells_ = value_label(contents, "gridInspectorTotalCells");
            page->addWidget(section(contents, QStringLiteral("Dimensions"), {
                {QStringLiteral("Nx"), grid_nx_}, {QStringLiteral("Ny"), grid_ny_},
                {QStringLiteral("Nz"), grid_nz_}}));
            page->addWidget(section(contents, QStringLiteral("Spacing"), {
                {QStringLiteral("dx"), grid_dx_}, {QStringLiteral("dy"), grid_dy_},
                {QStringLiteral("dz"), grid_dz_}}));
            page->addWidget(section(contents, QStringLiteral("Physical Size"), {
                {QStringLiteral("Lx"), grid_lx_}, {QStringLiteral("Ly"), grid_ly_},
                {QStringLiteral("Lz"), grid_lz_}}));
            page->addWidget(section(contents, QStringLiteral("Index Range"), {
                {QStringLiteral("X"), grid_x_range_}, {QStringLiteral("Y"), grid_y_range_},
                {QStringLiteral("Z"), grid_z_range_},
                {QStringLiteral("Total Cells"), grid_total_cells_}}));
        });

    source_page_ = inspector_page(
        pages_, "sourceInspector", QStringLiteral("Source"),
        QStringLiteral("Edit physical position and the production Ricker wavelet"),
        [this](QVBoxLayout* page, QWidget* contents) {
            source_status_ = value_label(contents, "sourceInspectorStatus");
            source_type_ = value_label(contents, "sourceInspectorType");
            source_id_ = value_label(contents, "sourceInspectorId");
            source_storage_position_ = value_label(contents, "sourceInspectorStoragePosition");
            source_wavelet_type_ = value_label(contents, "sourceInspectorWaveletType");
            source_mechanism_ = value_label(contents, "sourceInspectorMechanism");
            source_tensor_ = value_label(contents, "sourceInspectorTensor");
            source_x_ = source_spin(
                contents, "sourceInspectorXSpin", -1.0e12, 1.0e12, 3,
                QStringLiteral(" m"));
            source_y_ = source_spin(
                contents, "sourceInspectorYSpin", -1.0e12, 1.0e12, 3,
                QStringLiteral(" m"));
            source_z_ = source_spin(
                contents, "sourceInspectorZSpin", -1.0e12, 1.0e12, 3,
                QStringLiteral(" m"));
            source_origin_time_ = source_spin(
                contents, "sourceInspectorOriginTimeSpin", 0.0, 10000.0, 6,
                QStringLiteral(" s"));
            source_frequency_ = source_spin(
                contents, "sourceInspectorFrequencySpin", 0.0, 10000.0, 3,
                QStringLiteral(" Hz"));
            source_peak_time_ = source_spin(
                contents, "sourceInspectorPeakDelaySpin", 0.0, 10000.0, 6,
                QStringLiteral(" s"));
            source_peak_rate_ = source_spin(
                contents, "sourceInspectorPeakRateSpin", -1.0e12, 1.0e12, 6,
                QStringLiteral(" s⁻¹"));
            page->addWidget(section(contents, QStringLiteral("General"), {
                {QStringLiteral("Status"), source_status_},
                {QStringLiteral("Draft type"), source_type_},
                {QStringLiteral("Shot / Source ID"), source_id_}}));
            page->addWidget(control_section(contents, QStringLiteral("Position"), {
                {QStringLiteral("X"), source_x_},
                {QStringLiteral("Y"), source_y_},
                {QStringLiteral("Z (positive down)"), source_z_},
                {QStringLiteral("Applied storage coordinate"),
                 source_storage_position_}}));
            page->addWidget(control_section(contents, QStringLiteral("Wavelet"), {
                {QStringLiteral("Type"), source_wavelet_type_},
                {QStringLiteral("Dominant frequency"), source_frequency_},
                {QStringLiteral("Peak rate / amplitude"), source_peak_rate_},
                {QStringLiteral("Peak delay"), source_peak_time_},
                {QStringLiteral("Origin time"), source_origin_time_}}));
            source_footer_ = new InspectorEditFooter(contents);
            page->addWidget(source_footer_);
            page->addWidget(section(contents, QStringLiteral("Applied mechanism (read-only)"), {
                {QStringLiteral("Parameters"), source_mechanism_},
                {QStringLiteral("Applied tensor"), source_tensor_}}));
        });

    receiver_page_ = inspector_page(
        pages_, "receiverSetInspector", QStringLiteral("Receivers"),
        QStringLiteral("Current resolved observation system"),
        [this](QVBoxLayout* page, QWidget* contents) {
            receiver_status_ = value_label(contents, "receiverInspectorStatus");
            receiver_count_ = value_label(contents, "receiverInspectorCount");
            receiver_geometry_type_ = value_label(contents, "receiverInspectorGeometryType");
            receiver_components_ = value_label(contents, "receiverInspectorComponents");
            receiver_geometry_ = value_label(contents, "receiverInspectorGeometry");
            receiver_spacing_ = value_label(contents, "receiverInspectorSpacing");
            receiver_x_range_ = value_label(contents, "receiverInspectorXRange");
            receiver_y_range_ = value_label(contents, "receiverInspectorYRange");
            receiver_z_range_ = value_label(contents, "receiverInspectorZRange");
            receiver_sample_interval_ = value_label(contents, "receiverInspectorSampleInterval");
            receiver_sample_count_ = value_label(contents, "receiverInspectorSampleCount");
            receiver_duration_ = value_label(contents, "receiverInspectorDuration");
            page->addWidget(section(contents, QStringLiteral("General"), {
                {QStringLiteral("Status"), receiver_status_},
                {QStringLiteral("Receiver count"), receiver_count_},
                {QStringLiteral("Geometry type"), receiver_geometry_type_}}));
            page->addWidget(section(contents, QStringLiteral("Components"), {
                {QStringLiteral("Recorded"), receiver_components_}}));
            page->addWidget(section(contents, QStringLiteral("Geometry"), {
                {QStringLiteral("Definition"), receiver_geometry_},
                {QStringLiteral("Spacing"), receiver_spacing_}}));
            page->addWidget(section(contents, QStringLiteral("Coverage"), {
                {QStringLiteral("X range"), receiver_x_range_},
                {QStringLiteral("Y range"), receiver_y_range_},
                {QStringLiteral("Z range"), receiver_z_range_}}));
            page->addWidget(section(contents, QStringLiteral("Sampling"), {
                {QStringLiteral("Sample interval"), receiver_sample_interval_},
                {QStringLiteral("Sample count"), receiver_sample_count_},
                {QStringLiteral("Recording duration"), receiver_duration_}}));
        });

    simulation_page_ = inspector_page(
        pages_, "simulationInspector", QStringLiteral("Elastic Forward"),
        QStringLiteral("Resolved forward simulation configuration"),
        [this](QVBoxLayout* page, QWidget* contents) {
            simulation_status_ = value_label(contents, "simulationInspectorStatus");
            simulation_physics_ = value_label(contents, "simulationInspectorPhysics");
            simulation_formulation_ = value_label(contents, "simulationInspectorFormulation");
            simulation_grid_ = value_label(contents, "simulationInspectorGrid");
            simulation_spatial_order_ = value_label(contents, "simulationInspectorSpatialOrder");
            simulation_stencil_radius_ = value_label(contents, "simulationInspectorStencilRadius");
            simulation_dt_ = value_label(contents, "simulationInspectorDt");
            simulation_steps_ = value_label(contents, "simulationInspectorSteps");
            simulation_time_ = value_label(contents, "simulationInspectorTime");
            simulation_backend_ = value_label(contents, "simulationInspectorBackend");
            simulation_device_ = value_label(contents, "simulationInspectorDevice");
            simulation_precision_ = value_label(contents, "simulationInspectorPrecision");
            simulation_preflight_ = value_label(contents, "simulationInspectorPreflight");
            page->addWidget(section(contents, QStringLiteral("Method"), {
                {QStringLiteral("Physics"), simulation_physics_},
                {QStringLiteral("Formulation"), simulation_formulation_},
                {QStringLiteral("Grid"), simulation_grid_}}));
            page->addWidget(section(contents, QStringLiteral("Numerics"), {
                {QStringLiteral("Spatial order"), simulation_spatial_order_},
                {QStringLiteral("Stencil radius"), simulation_stencil_radius_},
                {QStringLiteral("Time step"), simulation_dt_},
                {QStringLiteral("Number of steps"), simulation_steps_},
                {QStringLiteral("Total physical time"), simulation_time_}}));
            page->addWidget(section(contents, QStringLiteral("Compute"), {
                {QStringLiteral("Backend"), simulation_backend_},
                {QStringLiteral("CUDA device"), simulation_device_},
                {QStringLiteral("Precision"), simulation_precision_}}));
            page->addWidget(section(contents, QStringLiteral("Status"), {
                {QStringLiteral("Configuration"), simulation_status_},
                {QStringLiteral("Preflight"), simulation_preflight_}}));
        });

    boundary_page_ = inspector_page(
        pages_, "boundaryInspector", QStringLiteral("Boundary"),
        QStringLiteral("Resolved forward boundary configuration"),
        [this](QVBoxLayout* page, QWidget* contents) {
            boundary_status_ = value_label(contents, "boundaryInspectorStatus");
            boundary_type_ = value_label(contents, "boundaryInspectorType");
            boundary_x_ = value_label(contents, "boundaryInspectorX");
            boundary_y_ = value_label(contents, "boundaryInspectorY");
            boundary_z_ = value_label(contents, "boundaryInspectorZ");
            boundary_free_surface_ = value_label(contents, "boundaryInspectorFreeSurface");
            boundary_surface_face_ = value_label(contents, "boundaryInspectorSurfaceFace");
            boundary_halo_ = value_label(contents, "boundaryInspectorHalo");
            page->addWidget(section(contents, QStringLiteral("Absorbing Boundary"), {
                {QStringLiteral("Status"), boundary_status_},
                {QStringLiteral("Type"), boundary_type_},
                {QStringLiteral("X min / max"), boundary_x_},
                {QStringLiteral("Y min / max"), boundary_y_},
                {QStringLiteral("Z min / max"), boundary_z_}}));
            page->addWidget(section(contents, QStringLiteral("Free Surface"), {
                {QStringLiteral("Enabled"), boundary_free_surface_},
                {QStringLiteral("Surface face"), boundary_surface_face_}}));
            page->addWidget(section(contents, QStringLiteral("Grid Padding"), {
                {QStringLiteral("Halo"), boundary_halo_}}));
        });

    output_page_ = inspector_page(
        pages_, "outputInspector", QStringLiteral("Output"),
        QStringLiteral("Scientific recording and presentation outputs"),
        [this](QVBoxLayout* page, QWidget* contents) {
            output_status_ = value_label(contents, "outputInspectorStatus");
            output_components_ = value_label(contents, "outputInspectorComponents");
            output_sample_interval_ = value_label(contents, "outputInspectorSampleInterval");
            output_samples_ = value_label(contents, "outputInspectorSamples");
            output_format_ = value_label(contents, "outputInspectorFormat");
            output_segy_enabled_ = value_label(contents, "outputInspectorSegyEnabled");
            output_segy_revision_ = value_label(contents, "outputInspectorSegyRevision");
            output_segy_sample_format_ = value_label(contents, "outputInspectorSegySampleFormat");
            output_segy_names_ = value_label(contents, "outputInspectorSegyNames");
            output_visualization_component_ = value_label(contents, "outputInspectorVisualizationComponent");
            output_run_directory_ = value_label(contents, "outputInspectorRunDirectory");
            output_result_manifest_ = value_label(contents, "outputInspectorResultManifest");
            page->addWidget(section(contents, QStringLiteral("Receiver Recording"), {
                {QStringLiteral("Status"), output_status_},
                {QStringLiteral("Components"), output_components_},
                {QStringLiteral("Sample interval"), output_sample_interval_},
                {QStringLiteral("Samples"), output_samples_},
                {QStringLiteral("Format"), output_format_}}));
            page->addWidget(section(contents, QStringLiteral("SEG-Y"), {
                {QStringLiteral("Enabled"), output_segy_enabled_},
                {QStringLiteral("Revision"), output_segy_revision_},
                {QStringLiteral("Sample format"), output_segy_sample_format_},
                {QStringLiteral("Output naming"), output_segy_names_}}));
            page->addWidget(section(contents, QStringLiteral("Wavefield Visualization"), {
                {QStringLiteral("Current component"), output_visualization_component_}}));
            page->addWidget(section(contents, QStringLiteral("Storage"), {
                {QStringLiteral("Run directory"), output_run_directory_},
                {QStringLiteral("Result manifest"), output_result_manifest_}}));
        });

    unsupported_page_ = message_page(
        pages_, "unsupportedInspector", QStringLiteral("Inspector unavailable"),
        QStringLiteral("Inspector for this object will be available in a later phase."));
    for (auto* page : {empty_page_, project_page_, model_page_,
                       model_property_page_, grid_page_, source_page_,
                       receiver_page_, simulation_page_, boundary_page_,
                       output_page_, unsupported_page_}) {
        pages_->addWidget(page);
    }

    if (selection_controller_ != nullptr) {
        selection_ = selection_controller_->currentSelection();
        connect(
            selection_controller_, &SelectionController::selectionChanged,
            this, [this](const SelectionContext& selection) {
                applySelection(selection);
            });
    }
    const auto publish_source = [this](double) { publishSourceEdit(); };
    for (auto* spin : {source_x_, source_y_, source_z_, source_frequency_,
                       source_peak_rate_, source_peak_time_,
                       source_origin_time_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, publish_source);
    }
    source_footer_->setCallbacks(
        [this] {
            if (experiment_controller_ != nullptr) {
                static_cast<void>(experiment_controller_->revert());
            }
        },
        [this] {
            if (experiment_controller_ != nullptr) {
                static_cast<void>(experiment_controller_->apply());
            }
        });
    if (experiment_controller_ != nullptr) {
        connect(
            experiment_controller_, &ExperimentController::draftChanged,
            this, [this](const ExperimentDraft&) { updateSourceEditor(); });
        connect(
            experiment_controller_, &ExperimentController::validationChanged,
            this,
            [this](const ExperimentValidationResult&) { updateSourceFooter(); });
        connect(
            experiment_controller_, &ExperimentController::editStateChanged,
            this, [this](ExperimentEditState) { updateSourceFooter(); });
        connect(
            experiment_controller_, &ExperimentController::dirtyChanged,
            this, [this](bool) { updateSourceFooter(); });
        connect(
            experiment_controller_, &ExperimentController::contextChanged,
            this, [this](bool) {
                updateSourceEditor();
                updateSourceFooter();
            });
    }
    updatePages();
    updateSourceEditor();
    updateSourceFooter();
    applySelection(selection_);
}

void ContextInspector::setState(ContextInspectorState state) {
    state_ = std::move(state);
    updateSourceEditor();
    updateSourceFooter();
    applySelection(selection_);
}

const ContextInspectorState& ContextInspector::state() const noexcept {
    return state_;
}

InspectorPage ContextInspector::currentPage() const noexcept {
    return current_page_;
}

QWidget* ContextInspector::currentPageWidget() const noexcept {
    return pages_->currentWidget();
}

void ContextInspector::applySelection(const SelectionContext& selection) {
    selection_ = selection;
    updatePages();
    QWidget* page = empty_page_;
    current_page_ = InspectorPage::Empty;
    const bool matching_project =
        state_.project &&
        (!selection.project_id || *selection.project_id == state_.project->project_id);
    const bool matching_property =
        matching_project && state_.model && selection.model_property &&
        selection.object_id ==
            state_.model->object_id + QLatin1Char(':') +
                (*selection.model_property == SelectionModelProperty::Vp
                     ? QStringLiteral("vp")
                     : *selection.model_property == SelectionModelProperty::Vs
                           ? QStringLiteral("vs")
                           : QStringLiteral("density"));
    switch (selection.kind) {
    case SelectionKind::None:
        break;
    case SelectionKind::Project:
        if (state_.project &&
            (selection.object_id == state_.project->project_id || matching_project)) {
            page = project_page_;
            current_page_ = InspectorPage::Project;
        }
        break;
    case SelectionKind::Model:
        if (matching_project && state_.model &&
            selection.object_id == state_.model->object_id) {
            page = model_page_;
            current_page_ = InspectorPage::Model;
        }
        break;
    case SelectionKind::ModelProperty:
        if (matching_property) {
            page = model_property_page_;
            current_page_ = InspectorPage::ModelProperty;
        }
        break;
    case SelectionKind::Grid:
        if (matching_project && state_.model &&
            selection.object_id ==
                state_.model->object_id + QStringLiteral(":grid")) {
            page = grid_page_;
            current_page_ = InspectorPage::Grid;
        }
        break;
    case SelectionKind::Source:
        if (matching_project && state_.experiment &&
            selection.object_id == state_.experiment->source.object_id) {
            page = source_page_;
            current_page_ = InspectorPage::Source;
        }
        break;
    case SelectionKind::ReceiverSet:
        if (matching_project && state_.experiment &&
            selection.object_id == state_.experiment->receivers.object_id) {
            page = receiver_page_;
            current_page_ = InspectorPage::ReceiverSet;
        }
        break;
    case SelectionKind::Simulation:
        if (matching_project && state_.experiment &&
            selection.object_id == state_.experiment->simulation.object_id) {
            page = simulation_page_;
            current_page_ = InspectorPage::Simulation;
        }
        break;
    case SelectionKind::Boundary:
        if (matching_project && state_.experiment &&
            selection.object_id == state_.experiment->boundary.object_id) {
            page = boundary_page_;
            current_page_ = InspectorPage::Boundary;
        }
        break;
    case SelectionKind::Output:
        if (matching_project && state_.experiment &&
            selection.object_id == state_.experiment->output.object_id) {
            page = output_page_;
            current_page_ = InspectorPage::Output;
        }
        break;
    default:
        page = unsupported_page_;
        current_page_ = InspectorPage::Unsupported;
        break;
    }
    pages_->setCurrentWidget(page);
}

void ContextInspector::updatePages() {
    if (state_.project) {
        const auto& project = *state_.project;
        project_name_->setText(project.name);
        project_id_->setText(project.project_id);
        project_path_->setText(project.root_path);
        project_path_->setToolTip(project.root_path);
        project_model_file_->setText(
            project.model_file.isEmpty() ? QStringLiteral("—") : project.model_file);
        project_model_file_->setToolTip(project.model_file);
        project_model_status_->setText(
            project.model_loaded ? QStringLiteral("Loaded")
                                 : QStringLiteral("Not loaded"));
        project_shot_count_->setText(QString::number(project.shot_count));
        project_run_count_->setText(QString::number(project.run_count));
        project_result_count_->setText(QString::number(project.result_count));
    } else {
        for (auto* label : {project_name_, project_id_, project_path_,
                            project_model_file_, project_model_status_,
                            project_shot_count_, project_run_count_,
                            project_result_count_}) label->setText(QStringLiteral("—"));
    }

    const auto unavailable = [](std::initializer_list<QLabel*> labels) {
        for (auto* label : labels) label->setText(QStringLiteral("—"));
    };
    if (state_.experiment) {
        const auto& experiment = *state_.experiment;
        const auto& source = experiment.source;
        const auto* draft = experiment_controller_ != nullptr &&
                                    experiment_controller_->draft()
                                ? &*experiment_controller_->draft()
                                : nullptr;
        source_status_->setText(
            draft != nullptr ? QStringLiteral("Draft available")
                             : QStringLiteral("Not configured"));
        source_id_->setText(
            draft != nullptr ? draft->shot_id
                             : source.shot_id.isEmpty() ? QStringLiteral("—")
                                                       : source.shot_id);
        source_type_->setText(
            draft != nullptr ? source_type(draft->source_mode)
                             : QStringLiteral("—"));
        source_wavelet_type_->setText(
            draft != nullptr ? QStringLiteral("Ricker") : QStringLiteral("—"));
        if (source.configured) {
            source_storage_position_->setText(
                QStringLiteral("(%1, %2, %3) cells")
                    .arg(number(source.storage_position.x),
                         number(source.storage_position.y),
                         number(source.storage_position.z)));
            source_mechanism_->setText(source.mechanism_detail);
            source_tensor_->setText(
                QStringLiteral("Mxx %1 · Myy %2 · Mzz %3\nMxy %4 · Mxz %5 · Myz %6 N·m")
                    .arg(number(source.moment_nm.m_xx_nm),
                         number(source.moment_nm.m_yy_nm),
                         number(source.moment_nm.m_zz_nm),
                         number(source.moment_nm.m_xy_nm),
                         number(source.moment_nm.m_xz_nm),
                         number(source.moment_nm.m_yz_nm)));
        } else {
            unavailable({source_storage_position_, source_mechanism_,
                         source_tensor_});
        }

        const auto& receivers = experiment.receivers;
        receiver_status_->setText(
            receivers.configured ? QStringLiteral("Configured")
                                 : QStringLiteral("Not configured"));
        receiver_components_->setText(receivers.components);
        if (receivers.configured) {
            receiver_count_->setText(QString::number(receivers.receiver_count));
            receiver_geometry_type_->setText(receivers.geometry_type);
            receiver_geometry_->setText(receivers.geometry_summary);
            receiver_spacing_->setText(receivers.spacing_summary);
            receiver_x_range_->setText(range_value(
                receivers.minimum_position_m.x_m, receivers.maximum_position_m.x_m));
            receiver_y_range_->setText(range_value(
                receivers.minimum_position_m.y_m, receivers.maximum_position_m.y_m));
            receiver_z_range_->setText(range_value(
                receivers.minimum_position_m.z_m, receivers.maximum_position_m.z_m));
            receiver_sample_interval_->setText(time_value(receivers.sample_interval_s));
            receiver_sample_count_->setText(QString::number(receivers.sample_count));
            receiver_duration_->setText(time_value(receivers.recording_duration_s));
        } else {
            unavailable({receiver_count_, receiver_geometry_type_, receiver_geometry_,
                         receiver_spacing_, receiver_x_range_, receiver_y_range_,
                         receiver_z_range_, receiver_sample_interval_,
                         receiver_sample_count_, receiver_duration_});
        }

        const auto& simulation = experiment.simulation;
        simulation_physics_->setText(simulation.physics);
        simulation_formulation_->setText(simulation.formulation);
        simulation_grid_->setText(simulation.grid_scheme);
        simulation_spatial_order_->setText(
            QStringLiteral("%1th order").arg(simulation.spatial_order));
        simulation_stencil_radius_->setText(
            QStringLiteral("%1 cells").arg(simulation.stencil_radius));
        simulation_backend_->setText(simulation.backend);
        simulation_device_->setText(simulation.device);
        simulation_precision_->setText(simulation.precision);
        simulation_preflight_->setText(simulation.preflight_status);
        simulation_status_->setText(
            simulation.configured ? QStringLiteral("Configured")
                                  : QStringLiteral("Not configured"));
        if (simulation.configured) {
            simulation_dt_->setText(time_value(simulation.dt_s));
            simulation_steps_->setText(QString::number(simulation.step_count));
            simulation_time_->setText(time_value(simulation.total_time_s));
        } else {
            unavailable({simulation_dt_, simulation_steps_, simulation_time_});
        }

        const auto& boundary = experiment.boundary;
        boundary_status_->setText(
            boundary.configured ? QStringLiteral("Configured")
                                : QStringLiteral("Not configured"));
        if (boundary.configured) {
            boundary_type_->setText(QStringLiteral("CPML"));
            boundary_x_->setText(QStringLiteral("%1 / %2 cells")
                                     .arg(boundary.x_min).arg(boundary.x_max));
            boundary_y_->setText(QStringLiteral("%1 / %2 cells")
                                     .arg(boundary.y_min).arg(boundary.y_max));
            boundary_z_->setText(QStringLiteral("%1 / %2 cells")
                                     .arg(boundary.z_min).arg(boundary.z_max));
            boundary_free_surface_->setText(
                boundary.free_surface ? QStringLiteral("Yes") : QStringLiteral("No"));
            boundary_surface_face_->setText(
                boundary.free_surface ? QStringLiteral("Z minimum (z = 0)")
                                      : QStringLiteral("—"));
            boundary_halo_->setText(QStringLiteral("%1 cells").arg(boundary.halo));
        } else {
            unavailable({boundary_type_, boundary_x_, boundary_y_, boundary_z_,
                         boundary_free_surface_, boundary_surface_face_, boundary_halo_});
        }

        const auto& output = experiment.output;
        output_status_->setText(
            output.configured ? QStringLiteral("Configured")
                              : QStringLiteral("Not configured"));
        output_components_->setText(output.receiver_components);
        output_format_->setText(output.recording_format);
        output_segy_enabled_->setText(
            output.segy_enabled ? QStringLiteral("Yes") : QStringLiteral("No"));
        output_segy_revision_->setText(output.segy_revision);
        output_segy_sample_format_->setText(output.segy_sample_format);
        output_segy_names_->setText(output.segy_output_naming);
        output_visualization_component_->setText(output.visualization_component);
        output_run_directory_->setText(output.run_directory);
        output_run_directory_->setToolTip(output.run_directory);
        output_result_manifest_->setText(output.result_manifest);
        if (output.configured) {
            output_sample_interval_->setText(time_value(output.sample_interval_s));
            output_samples_->setText(QString::number(output.sample_count));
        } else {
            unavailable({output_sample_interval_, output_samples_});
        }
    } else {
        unavailable({source_status_, source_type_, source_id_,
                     source_storage_position_, source_wavelet_type_,
                     source_mechanism_, source_tensor_, receiver_status_, receiver_count_,
                     receiver_geometry_type_, receiver_components_, receiver_geometry_,
                     receiver_spacing_, receiver_x_range_, receiver_y_range_,
                     receiver_z_range_, receiver_sample_interval_,
                     receiver_sample_count_, receiver_duration_, simulation_status_,
                     simulation_physics_, simulation_formulation_, simulation_grid_,
                     simulation_spatial_order_, simulation_stencil_radius_,
                     simulation_dt_, simulation_steps_, simulation_time_,
                     simulation_backend_, simulation_device_, simulation_precision_,
                     simulation_preflight_, boundary_status_, boundary_type_,
                     boundary_x_, boundary_y_, boundary_z_, boundary_free_surface_,
                     boundary_surface_face_, boundary_halo_, output_status_,
                     output_components_, output_sample_interval_, output_samples_,
                     output_format_, output_segy_enabled_, output_segy_revision_,
                     output_segy_sample_format_, output_segy_names_,
                     output_visualization_component_, output_run_directory_,
                     output_result_manifest_});
    }

    if (!state_.model) {
        for (auto* label : {model_file_, model_dimensions_, model_spacing_,
                            model_extent_, model_vp_available_, model_vs_available_,
                            model_density_available_, model_status_, property_title_,
                            property_name_, property_unit_, property_minimum_,
                            property_maximum_, property_dimensions_, property_spacing_,
                            property_current_field_, grid_nx_, grid_ny_, grid_nz_,
                            grid_dx_, grid_dy_, grid_dz_, grid_lx_, grid_ly_, grid_lz_,
                            grid_x_range_, grid_y_range_, grid_z_range_,
                            grid_total_cells_}) label->setText(QStringLiteral("—"));
        return;
    }

    const auto& model = *state_.model;
    const auto dimensions = QStringLiteral("%1 × %2 × %3")
                                .arg(model.nx).arg(model.ny).arg(model.nz);
    const auto spacing = QStringLiteral("%1 × %2 × %3 m")
                             .arg(number(model.dx_m), number(model.dy_m), number(model.dz_m));
    const auto extent = QStringLiteral("%1 × %2 × %3 m")
                            .arg(number(model.extent_x_m), number(model.extent_y_m),
                                 number(model.extent_z_m));
    model_file_->setText(model.file_path);
    model_file_->setToolTip(model.file_path);
    model_dimensions_->setText(dimensions);
    model_spacing_->setText(spacing);
    model_extent_->setText(extent);
    model_vp_available_->setText(QStringLiteral("Yes"));
    model_vs_available_->setText(QStringLiteral("Yes"));
    model_density_available_->setText(QStringLiteral("Yes"));
    model_status_->setText(QStringLiteral("Loaded"));

    QString property_name = QStringLiteral("—");
    QString unit = QStringLiteral("—");
    float minimum = 0.0F;
    float maximum = 0.0F;
    if (selection_.model_property) {
        switch (*selection_.model_property) {
        case SelectionModelProperty::Vp:
            property_name = QStringLiteral("Vp");
            unit = QStringLiteral("m/s");
            minimum = model.vp_min_m_s;
            maximum = model.vp_max_m_s;
            break;
        case SelectionModelProperty::Vs:
            property_name = QStringLiteral("Vs");
            unit = QStringLiteral("m/s");
            minimum = model.vs_min_m_s;
            maximum = model.vs_max_m_s;
            break;
        case SelectionModelProperty::Density:
            property_name = QStringLiteral("Density");
            unit = QStringLiteral("kg/m³");
            minimum = model.density_min_kg_m3;
            maximum = model.density_max_kg_m3;
            break;
        }
    }
    property_title_->setText(property_name);
    property_name_->setText(property_name);
    property_unit_->setText(unit);
    property_minimum_->setText(QStringLiteral("%1 %2").arg(number(minimum), unit));
    property_maximum_->setText(QStringLiteral("%1 %2").arg(number(maximum), unit));
    property_dimensions_->setText(dimensions);
    property_spacing_->setText(spacing);
    property_current_field_->setText(property_name);

    grid_nx_->setText(QString::number(model.nx));
    grid_ny_->setText(QString::number(model.ny));
    grid_nz_->setText(QString::number(model.nz));
    grid_dx_->setText(QStringLiteral("%1 m").arg(number(model.dx_m)));
    grid_dy_->setText(QStringLiteral("%1 m").arg(number(model.dy_m)));
    grid_dz_->setText(QStringLiteral("%1 m").arg(number(model.dz_m)));
    grid_lx_->setText(QStringLiteral("%1 m").arg(number(model.extent_x_m)));
    grid_ly_->setText(QStringLiteral("%1 m").arg(number(model.extent_y_m)));
    grid_lz_->setText(QStringLiteral("%1 m").arg(number(model.extent_z_m)));
    grid_x_range_->setText(QStringLiteral("0 … %1").arg(model.nx - 1));
    grid_y_range_->setText(QStringLiteral("0 … %1").arg(model.ny - 1));
    grid_z_range_->setText(QStringLiteral("0 … %1").arg(model.nz - 1));
    const auto cells = model.nx * model.ny * model.nz;
    grid_total_cells_->setText(
        QStringLiteral("%1 × %2 × %3 = %4")
            .arg(model.nx).arg(model.ny).arg(model.nz).arg(cells));
}

void ContextInspector::updateSourceEditor() {
    const auto available = experiment_controller_ != nullptr &&
                           experiment_controller_->hasContext() &&
                           experiment_controller_->draft().has_value();
    const QSignalBlocker block_x(source_x_);
    const QSignalBlocker block_y(source_y_);
    const QSignalBlocker block_z(source_z_);
    const QSignalBlocker block_frequency(source_frequency_);
    const QSignalBlocker block_peak_rate(source_peak_rate_);
    const QSignalBlocker block_peak_time(source_peak_time_);
    const QSignalBlocker block_origin(source_origin_time_);
    for (auto* spin : {source_x_, source_y_, source_z_, source_frequency_,
                       source_peak_rate_, source_peak_time_,
                       source_origin_time_}) {
        spin->setEnabled(available);
        if (!available) spin->clear();
    }
    if (!available) {
        source_status_->setText(QStringLiteral("Not configured"));
        source_type_->setText(QStringLiteral("—"));
        source_id_->setText(QStringLiteral("—"));
        source_wavelet_type_->setText(QStringLiteral("—"));
        return;
    }
    const auto& draft = *experiment_controller_->draft();
    source_x_->setValue(draft.source_location_m.x_m);
    source_y_->setValue(draft.source_location_m.y_m);
    source_z_->setValue(draft.source_location_m.z_m);
    source_frequency_->setValue(draft.wavelet.dominant_frequency_hz);
    source_peak_rate_->setValue(draft.wavelet.peak_rate_s_inv);
    source_peak_time_->setValue(draft.wavelet.peak_delay_s);
    source_origin_time_->setValue(draft.source_origin_time_s);
    source_status_->setText(QStringLiteral("Draft available"));
    source_type_->setText(source_type(draft.source_mode));
    source_id_->setText(draft.shot_id);
    source_wavelet_type_->setText(QStringLiteral("Ricker"));
}

void ContextInspector::updateSourceFooter() {
    if (experiment_controller_ == nullptr ||
        !experiment_controller_->hasContext()) {
        source_footer_->setState({
            InspectorEditStatus::Unavailable,
            QStringLiteral("No experiment loaded"),
            false,
            false});
        return;
    }
    const auto state = experiment_controller_->state();
    InspectorEditStatus status = InspectorEditStatus::Applied;
    QString message = QStringLiteral(
        "Draft matches the applied runtime configuration.");
    if (state.edit_state == ExperimentEditState::Invalid) {
        status = InspectorEditStatus::Invalid;
        const auto& issues = experiment_controller_->validation().issues;
        message = issues.isEmpty() ? QStringLiteral("Experiment is invalid")
                                   : issues.front().message;
    } else if (state.dirty) {
        status = InspectorEditStatus::Modified;
        message = QStringLiteral(
            "Draft validation passed. Apply to update runtime configuration.");
    } else if (!state.applied) {
        status = InspectorEditStatus::Unavailable;
        message = QStringLiteral("No applied experiment configuration");
    }
    source_footer_->setState({
        status, message, state.can_apply, state.can_revert});
}

void ContextInspector::publishSourceEdit() {
    if (experiment_controller_ == nullptr ||
        !experiment_controller_->draft()) {
        return;
    }
    auto draft = *experiment_controller_->draft();
    draft.source_location_m = {
        source_x_->value(), source_y_->value(), source_z_->value()};
    draft.source_origin_time_s = source_origin_time_->value();
    draft.wavelet = {
        source_frequency_->value(),
        source_peak_time_->value(),
        source_peak_rate_->value()};
    static_cast<void>(experiment_controller_->updateDraft(std::move(draft)));
}

} // namespace wave3d::desktop
