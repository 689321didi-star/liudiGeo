#pragma once

#include "wave3d/desktop/experiment_inspector_state.hpp"
#include "wave3d/desktop/selection_context.hpp"

#include <QWidget>

#include <cstddef>
#include <optional>

class QLabel;
class QStackedWidget;

namespace wave3d::desktop {

class SelectionController;

struct ProjectInspectorState final {
    QString project_id;
    QString name;
    QString root_path;
    QString model_file;
    bool model_loaded{false};
    std::size_t shot_count{0};
    std::size_t run_count{0};
    std::size_t result_count{0};
};

struct ModelInspectorState final {
    QString object_id;
    QString file_path;
    std::size_t nx{0};
    std::size_t ny{0};
    std::size_t nz{0};
    double dx_m{0.0};
    double dy_m{0.0};
    double dz_m{0.0};
    double extent_x_m{0.0};
    double extent_y_m{0.0};
    double extent_z_m{0.0};
    float vp_min_m_s{0.0F};
    float vp_max_m_s{0.0F};
    float vs_min_m_s{0.0F};
    float vs_max_m_s{0.0F};
    float density_min_kg_m3{0.0F};
    float density_max_kg_m3{0.0F};
};

struct ContextInspectorState final {
    std::optional<ProjectInspectorState> project;
    std::optional<ModelInspectorState> model;
    std::optional<ExperimentInspectorState> experiment;
};

enum class InspectorPage {
    Empty,
    Project,
    Model,
    ModelProperty,
    Grid,
    Source,
    ReceiverSet,
    Simulation,
    Boundary,
    Output,
    Unsupported,
};

class ContextInspector final : public QWidget {
public:
    explicit ContextInspector(
        SelectionController* selection_controller,
        QWidget* parent = nullptr);

    void setState(ContextInspectorState state);

    [[nodiscard]] const ContextInspectorState& state() const noexcept;
    [[nodiscard]] InspectorPage currentPage() const noexcept;
    [[nodiscard]] QWidget* currentPageWidget() const noexcept;

private:
    void applySelection(const SelectionContext& selection);
    void updatePages();

    SelectionController* selection_controller_{nullptr};
    ContextInspectorState state_;
    SelectionContext selection_;
    QStackedWidget* pages_{nullptr};
    QWidget* empty_page_{nullptr};
    QWidget* project_page_{nullptr};
    QWidget* model_page_{nullptr};
    QWidget* model_property_page_{nullptr};
    QWidget* grid_page_{nullptr};
    QWidget* source_page_{nullptr};
    QWidget* receiver_page_{nullptr};
    QWidget* simulation_page_{nullptr};
    QWidget* boundary_page_{nullptr};
    QWidget* output_page_{nullptr};
    QWidget* unsupported_page_{nullptr};
    InspectorPage current_page_{InspectorPage::Empty};

    QLabel* project_name_{nullptr};
    QLabel* project_id_{nullptr};
    QLabel* project_path_{nullptr};
    QLabel* project_model_file_{nullptr};
    QLabel* project_model_status_{nullptr};
    QLabel* project_shot_count_{nullptr};
    QLabel* project_run_count_{nullptr};
    QLabel* project_result_count_{nullptr};

    QLabel* model_file_{nullptr};
    QLabel* model_dimensions_{nullptr};
    QLabel* model_spacing_{nullptr};
    QLabel* model_extent_{nullptr};
    QLabel* model_vp_available_{nullptr};
    QLabel* model_vs_available_{nullptr};
    QLabel* model_density_available_{nullptr};
    QLabel* model_status_{nullptr};

    QLabel* property_title_{nullptr};
    QLabel* property_name_{nullptr};
    QLabel* property_unit_{nullptr};
    QLabel* property_minimum_{nullptr};
    QLabel* property_maximum_{nullptr};
    QLabel* property_dimensions_{nullptr};
    QLabel* property_spacing_{nullptr};
    QLabel* property_current_field_{nullptr};

    QLabel* grid_nx_{nullptr};
    QLabel* grid_ny_{nullptr};
    QLabel* grid_nz_{nullptr};
    QLabel* grid_dx_{nullptr};
    QLabel* grid_dy_{nullptr};
    QLabel* grid_dz_{nullptr};
    QLabel* grid_lx_{nullptr};
    QLabel* grid_ly_{nullptr};
    QLabel* grid_lz_{nullptr};
    QLabel* grid_x_range_{nullptr};
    QLabel* grid_y_range_{nullptr};
    QLabel* grid_z_range_{nullptr};
    QLabel* grid_total_cells_{nullptr};

    QLabel* source_status_{nullptr};
    QLabel* source_type_{nullptr};
    QLabel* source_id_{nullptr};
    QLabel* source_physical_position_{nullptr};
    QLabel* source_storage_position_{nullptr};
    QLabel* source_wavelet_type_{nullptr};
    QLabel* source_frequency_{nullptr};
    QLabel* source_peak_rate_{nullptr};
    QLabel* source_peak_time_{nullptr};
    QLabel* source_origin_time_{nullptr};
    QLabel* source_mechanism_{nullptr};
    QLabel* source_tensor_{nullptr};

    QLabel* receiver_status_{nullptr};
    QLabel* receiver_count_{nullptr};
    QLabel* receiver_geometry_type_{nullptr};
    QLabel* receiver_components_{nullptr};
    QLabel* receiver_geometry_{nullptr};
    QLabel* receiver_spacing_{nullptr};
    QLabel* receiver_x_range_{nullptr};
    QLabel* receiver_y_range_{nullptr};
    QLabel* receiver_z_range_{nullptr};
    QLabel* receiver_sample_interval_{nullptr};
    QLabel* receiver_sample_count_{nullptr};
    QLabel* receiver_duration_{nullptr};

    QLabel* simulation_status_{nullptr};
    QLabel* simulation_physics_{nullptr};
    QLabel* simulation_formulation_{nullptr};
    QLabel* simulation_grid_{nullptr};
    QLabel* simulation_spatial_order_{nullptr};
    QLabel* simulation_stencil_radius_{nullptr};
    QLabel* simulation_dt_{nullptr};
    QLabel* simulation_steps_{nullptr};
    QLabel* simulation_time_{nullptr};
    QLabel* simulation_backend_{nullptr};
    QLabel* simulation_device_{nullptr};
    QLabel* simulation_precision_{nullptr};
    QLabel* simulation_preflight_{nullptr};

    QLabel* boundary_status_{nullptr};
    QLabel* boundary_type_{nullptr};
    QLabel* boundary_x_{nullptr};
    QLabel* boundary_y_{nullptr};
    QLabel* boundary_z_{nullptr};
    QLabel* boundary_free_surface_{nullptr};
    QLabel* boundary_surface_face_{nullptr};
    QLabel* boundary_halo_{nullptr};

    QLabel* output_status_{nullptr};
    QLabel* output_components_{nullptr};
    QLabel* output_sample_interval_{nullptr};
    QLabel* output_samples_{nullptr};
    QLabel* output_format_{nullptr};
    QLabel* output_segy_enabled_{nullptr};
    QLabel* output_segy_revision_{nullptr};
    QLabel* output_segy_sample_format_{nullptr};
    QLabel* output_segy_names_{nullptr};
    QLabel* output_visualization_component_{nullptr};
    QLabel* output_run_directory_{nullptr};
    QLabel* output_result_manifest_{nullptr};
};

} // namespace wave3d::desktop
