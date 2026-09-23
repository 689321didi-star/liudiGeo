#pragma once

#include <QMetaType>
#include <QString>

#include <optional>
#include <utility>

namespace wave3d::desktop {

enum class SelectionKind {
    None,
    Project,
    Model,
    ModelProperty,
    Grid,
    Source,
    ReceiverSet,
    Simulation,
    Boundary,
    Output,
    Run,
    Result,
    File,
    WorkflowStep,
};

enum class SelectionModelProperty { Vp, Vs, Density };

struct SelectionContext final {
    SelectionKind kind{SelectionKind::None};
    QString object_id;
    std::optional<QString> project_id;
    std::optional<QString> run_id;
    std::optional<SelectionModelProperty> model_property;

    SelectionContext() = default;

    SelectionContext(
        SelectionKind selected_kind,
        QString stable_object_id,
        std::optional<QString> selected_project_id = std::nullopt,
        std::optional<QString> selected_run_id = std::nullopt,
        std::optional<SelectionModelProperty> selected_model_property =
            std::nullopt)
        : kind(selected_kind),
          object_id(std::move(stable_object_id)),
          project_id(std::move(selected_project_id)),
          run_id(std::move(selected_run_id)),
          model_property(selected_model_property) {}
};

[[nodiscard]] inline bool operator==(
    const SelectionContext& left,
    const SelectionContext& right) noexcept {
    return left.kind == right.kind && left.object_id == right.object_id &&
           left.project_id == right.project_id && left.run_id == right.run_id &&
           left.model_property == right.model_property;
}

[[nodiscard]] inline bool operator!=(
    const SelectionContext& left,
    const SelectionContext& right) noexcept {
    return !(left == right);
}

[[nodiscard]] QString selection_kind_display_name(SelectionKind kind);

} // namespace wave3d::desktop

Q_DECLARE_METATYPE(wave3d::desktop::SelectionContext)
