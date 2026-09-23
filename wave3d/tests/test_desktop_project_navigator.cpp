#include "wave3d/desktop/project_navigator.hpp"
#include "wave3d/desktop/selection_controller.hpp"

#include <QApplication>
#include <QSignalSpy>
#include <QTreeView>

#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {

using namespace wave3d::desktop;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void collect_indexes(
    const ProjectNavigatorModel& model,
    const QModelIndex& parent,
    std::vector<QModelIndex>* output) {
    for (int row = 0; row < model.rowCount(parent); ++row) {
        const auto child = model.index(row, 0, parent);
        output->push_back(child);
        collect_indexes(model, child, output);
    }
}

QModelIndex find_kind(
    const ProjectNavigatorModel& model,
    ProjectNavigatorNodeKind kind,
    int occurrence = 0) {
    std::vector<QModelIndex> indexes;
    collect_indexes(model, {}, &indexes);
    for (const auto& index : indexes) {
        if (model.nodeKind(index) == kind && occurrence-- == 0) return index;
    }
    return {};
}

QModelIndex find_property(
    const ProjectNavigatorModel& model,
    SelectionModelProperty property) {
    std::vector<QModelIndex> indexes;
    collect_indexes(model, {}, &indexes);
    for (const auto& index : indexes) {
        const auto selection = model.selectionForIndex(index);
        if (selection && selection->kind == SelectionKind::ModelProperty &&
            selection->model_property == property) {
            return index;
        }
    }
    return {};
}

ProjectNavigatorState empty_model_state() {
    ProjectNavigatorState state;
    state.project_id = QStringLiteral("project-1");
    state.project_name = QStringLiteral("Research Project");
    state.model_id = QStringLiteral("project-1:model");
    return state;
}

ProjectNavigatorState populated_state() {
    auto state = empty_model_state();
    state.model_id = QStringLiteral("models/overthrust.h5");
    state.model_loaded = true;
    state.source_id = QStringLiteral("shot-001:source");
    state.source_configured = true;
    state.receiver_set_id = QStringLiteral("shot-001:receivers");
    state.receiver_set_configured = true;
    state.runs = {
        {QStringLiteral("run-preflight"), false},
        {QStringLiteral("run-completed"), true}};
    return state;
}

void test_empty_and_loaded_models() {
    ProjectNavigatorModel model;
    model.setProjectState(empty_model_state());
    const auto project = model.projectIndex();
    expect(project.isValid(), "project root is missing");
    expect(
        model.data(project).toString() == QStringLiteral("Research Project"),
        "project root does not use the real project name");
    const auto model_group = find_kind(model, ProjectNavigatorNodeKind::ModelGroup);
    expect(model_group.isValid(), "empty project has no Model group");
    expect(
        !model.selectionForIndex(model_group).has_value(),
        "unloaded model group pretends to be a model object");
    const auto information = model.index(0, 0, model_group);
    expect(
        model.data(information).toString() == QStringLiteral("Not loaded") &&
            model.flags(information) == Qt::NoItemFlags,
        "unloaded model state is not disabled information");

    model.setProjectState(populated_state());
    expect(find_property(model, SelectionModelProperty::Vp).isValid(), "Vp is missing");
    expect(find_property(model, SelectionModelProperty::Vs).isValid(), "Vs is missing");
    expect(
        find_property(model, SelectionModelProperty::Density).isValid(),
        "Density is missing");
    expect(
        find_kind(model, ProjectNavigatorNodeKind::Grid).isValid(),
        "Grid is missing");
}

void test_selection_mapping_and_emission() {
    SelectionController controller;
    ProjectNavigator navigator(&controller);
    navigator.setProjectState(populated_state());
    QSignalSpy changes(&controller, &SelectionController::selectionChanged);

    const auto select = [&](const QModelIndex& index, SelectionKind expected) {
        expect(index.isValid(), "required selection node is missing");
        navigator.treeView()->setCurrentIndex(index);
        expect(
            controller.currentSelection().kind == expected,
            "tree node emitted the wrong SelectionKind");
    };
    select(navigator.model()->projectIndex(), SelectionKind::Project);
    select(find_property(*navigator.model(), SelectionModelProperty::Vp),
           SelectionKind::ModelProperty);
    expect(
        controller.currentSelection().model_property == SelectionModelProperty::Vp,
        "Vp selection lost its property type");
    select(find_property(*navigator.model(), SelectionModelProperty::Vs),
           SelectionKind::ModelProperty);
    select(find_property(*navigator.model(), SelectionModelProperty::Density),
           SelectionKind::ModelProperty);
    select(find_kind(*navigator.model(), ProjectNavigatorNodeKind::Grid),
           SelectionKind::Grid);
    select(find_kind(*navigator.model(), ProjectNavigatorNodeKind::Source),
           SelectionKind::Source);
    select(find_kind(*navigator.model(), ProjectNavigatorNodeKind::ReceiverSet),
           SelectionKind::ReceiverSet);
    select(find_kind(*navigator.model(), ProjectNavigatorNodeKind::Simulation),
           SelectionKind::Simulation);
    select(find_kind(*navigator.model(), ProjectNavigatorNodeKind::Boundary),
           SelectionKind::Boundary);
    select(find_kind(*navigator.model(), ProjectNavigatorNodeKind::Output),
           SelectionKind::Output);
    select(find_kind(*navigator.model(), ProjectNavigatorNodeKind::Run),
           SelectionKind::Run);
    select(find_kind(*navigator.model(), ProjectNavigatorNodeKind::Result),
           SelectionKind::Result);
    expect(changes.count() >= 12, "selection changes were not emitted");
}

void test_refresh_restore_and_removal_fallback() {
    SelectionController controller;
    ProjectNavigator navigator(&controller);
    auto state = populated_state();
    navigator.setProjectState(state);
    const auto vp = navigator.model()->selectionForIndex(
        find_property(*navigator.model(), SelectionModelProperty::Vp));
    expect(vp.has_value() && navigator.selectContext(*vp), "cannot select Vp");
    expect(controller.currentSelection() == *vp, "Vp selection was not published");

    state.project_name = QStringLiteral("Renamed display label");
    navigator.setProjectState(state);
    expect(
        controller.currentSelection() == *vp &&
            navigator.model()->selectionForIndex(
                navigator.treeView()->currentIndex()) == vp,
        "selection did not survive a display-only refresh");

    controller.setSelection(SelectionContext{
        SelectionKind::File, QStringLiteral("files-root"),
        QStringLiteral("project-1")});
    navigator.publishCurrentSelection();
    expect(
        controller.currentSelection() == *vp,
        "returning to Project did not republish the retained tree selection");

    state.model_loaded = false;
    navigator.setProjectState(state);
    expect(
        controller.currentSelection().kind == SelectionKind::Project,
        "removed model object did not fall back to the project root");
}

void test_run_result_and_model_boundary() {
    static_assert(!std::is_base_of_v<QWidget, ProjectNavigatorModel>);
    ProjectNavigatorModel model;
    model.setProjectState(populated_state());
    expect(
        find_kind(model, ProjectNavigatorNodeKind::Run, 0).isValid() &&
            find_kind(model, ProjectNavigatorNodeKind::Run, 1).isValid(),
        "real runs were not listed");
    expect(
        find_kind(model, ProjectNavigatorNodeKind::Result).isValid(),
        "completed result was not listed");
    expect(
        !model.data(model.projectIndex(), Qt::UserRole).isValid(),
        "model exposed an untyped object payload");
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    try {
        test_empty_and_loaded_models();
        test_selection_mapping_and_emission();
        test_refresh_restore_and_removal_fallback();
        test_run_result_and_model_boundary();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
