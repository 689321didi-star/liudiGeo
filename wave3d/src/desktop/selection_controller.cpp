#include "wave3d/desktop/selection_controller.hpp"

namespace wave3d::desktop {

QString selection_kind_display_name(SelectionKind kind) {
    switch (kind) {
    case SelectionKind::None:
        return QStringLiteral("None");
    case SelectionKind::Project:
        return QStringLiteral("Project");
    case SelectionKind::Model:
        return QStringLiteral("Model");
    case SelectionKind::ModelProperty:
        return QStringLiteral("Model Property");
    case SelectionKind::Grid:
        return QStringLiteral("Grid");
    case SelectionKind::Source:
        return QStringLiteral("Source");
    case SelectionKind::ReceiverSet:
        return QStringLiteral("Receiver Set");
    case SelectionKind::Simulation:
        return QStringLiteral("Simulation");
    case SelectionKind::Boundary:
        return QStringLiteral("Boundary");
    case SelectionKind::Output:
        return QStringLiteral("Output");
    case SelectionKind::Run:
        return QStringLiteral("Run");
    case SelectionKind::Result:
        return QStringLiteral("Result");
    case SelectionKind::File:
        return QStringLiteral("File");
    case SelectionKind::WorkflowStep:
        return QStringLiteral("Workflow Step");
    }
    return QStringLiteral("Unknown");
}

SelectionController::SelectionController(QObject* parent) : QObject(parent) {
    qRegisterMetaType<SelectionContext>();
}

const SelectionContext& SelectionController::currentSelection() const noexcept {
    return current_selection_;
}

void SelectionController::setSelection(const SelectionContext& selection) {
    if (selection == current_selection_) {
        return;
    }
    current_selection_ = selection;
    emit selectionChanged(current_selection_);
}

void SelectionController::clearSelection() {
    setSelection(SelectionContext{});
}

} // namespace wave3d::desktop
