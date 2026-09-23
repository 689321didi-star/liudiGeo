#include "wave3d/desktop/main_window_shell.hpp"
#include "wave3d/desktop/selection_controller.hpp"

#include <QApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QTabWidget>
#include <QWidget>

#include <stdexcept>

namespace {

using wave3d::desktop::MainWindowShell;
using wave3d::desktop::NavigatorPage;
using wave3d::desktop::SelectionContext;
using wave3d::desktop::SelectionController;
using wave3d::desktop::SelectionKind;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_selection_lifecycle() {
    SelectionController controller;
    expect(
        controller.currentSelection().kind == SelectionKind::None,
        "default selection is not None");

    QSignalSpy changes(&controller, &SelectionController::selectionChanged);
    const SelectionContext project{
        SelectionKind::Project, QStringLiteral("project-1"),
        QStringLiteral("project-1")};
    controller.setSelection(project);
    expect(
        controller.currentSelection() == project,
        "setSelection did not retain the selection");
    expect(changes.count() == 1, "setSelection did not emit exactly once");
    expect(
        qvariant_cast<SelectionContext>(changes.at(0).at(0)) == project,
        "selectionChanged carried the wrong value");

    controller.setSelection(project);
    expect(changes.count() == 1, "equal selection emitted a duplicate signal");

    controller.clearSelection();
    expect(
        controller.currentSelection().kind == SelectionKind::None,
        "clearSelection did not restore None");
    expect(changes.count() == 2, "clearSelection did not emit a change");
}

void test_selection_transitions() {
    SelectionController controller;
    QSignalSpy changes(&controller, &SelectionController::selectionChanged);
    const SelectionContext project{
        SelectionKind::Project, QStringLiteral("project-1")};
    const SelectionContext model{
        SelectionKind::Model, QStringLiteral("model-1"),
        QStringLiteral("project-1")};
    const SelectionContext source{
        SelectionKind::Source, QStringLiteral("source-1"),
        QStringLiteral("project-1")};
    const SelectionContext run{
        SelectionKind::Run, QStringLiteral("run-1"),
        QStringLiteral("project-1"), QStringLiteral("run-1")};

    controller.setSelection(project);
    expect(
        controller.currentSelection().kind == SelectionKind::Project,
        "Project transition failed");
    controller.setSelection(model);
    expect(
        controller.currentSelection().kind == SelectionKind::Model,
        "Project -> Model transition failed");
    controller.setSelection(source);
    expect(
        controller.currentSelection().kind == SelectionKind::Source,
        "Model -> Source transition failed");
    controller.setSelection(run);
    expect(
        controller.currentSelection().kind == SelectionKind::Run,
        "Source -> Run transition failed");
    expect(changes.count() == 4, "selection transitions were not all emitted");
    expect(
        controller.currentSelection() == run,
        "Project -> Model -> Source -> Run ended in the wrong state");
}

void test_parent_owned_destruction() {
    QPointer<SelectionController> observer;
    {
        QObject owner;
        observer = new SelectionController(&owner);
        observer->setSelection(SelectionContext{
            SelectionKind::Result, QStringLiteral("result-1")});
    }
    expect(observer.isNull(), "parent did not safely destroy the controller");
}

void test_shell_navigator_wiring() {
    MainWindowShell shell;
    shell.set_navigator_pages(
        NavigatorPage{
            new QWidget(&shell), QStringLiteral("Project"),
            SelectionContext{
                SelectionKind::Project, QStringLiteral("project-placeholder")}},
        NavigatorPage{
            new QWidget(&shell), QStringLiteral("Files"),
            SelectionContext{
                SelectionKind::File, QStringLiteral("files-placeholder")}},
        NavigatorPage{
            new QWidget(&shell), QStringLiteral("Workflow"),
            SelectionContext{
                SelectionKind::WorkflowStep,
                QStringLiteral("workflow-placeholder")}});

    auto* controller = shell.selection_controller();
    expect(controller != nullptr, "shell has no SelectionController");
    expect(
        controller->currentSelection().kind == SelectionKind::Project,
        "Project navigator page did not publish its selection");

    QSignalSpy changes(controller, &SelectionController::selectionChanged);
    shell.navigator_host()->setCurrentIndex(1);
    expect(
        controller->currentSelection().kind == SelectionKind::File,
        "Files navigator page did not publish its selection");
    shell.navigator_host()->setCurrentIndex(2);
    expect(
        controller->currentSelection().kind == SelectionKind::WorkflowStep,
        "Workflow navigator page did not publish its selection");
    expect(changes.count() == 2, "navigator emitted an unexpected change count");
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    try {
        test_selection_lifecycle();
        test_selection_transitions();
        test_parent_owned_destruction();
        test_shell_navigator_wiring();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
