#include "wave3d/desktop/context_inspector.hpp"

#include "wave3d/desktop/selection_controller.hpp"

#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace wave3d::desktop {
namespace {

QString number(double value) {
    return QString::number(value, 'g', 8);
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
    QWidget* parent)
    : QWidget(parent), selection_controller_(selection_controller) {
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

    unsupported_page_ = message_page(
        pages_, "unsupportedInspector", QStringLiteral("Inspector unavailable"),
        QStringLiteral("Inspector for this object will be available in a later phase."));
    for (auto* page : {empty_page_, project_page_, model_page_,
                       model_property_page_, grid_page_, unsupported_page_}) {
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
    updatePages();
    applySelection(selection_);
}

void ContextInspector::setState(ContextInspectorState state) {
    state_ = std::move(state);
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

} // namespace wave3d::desktop
