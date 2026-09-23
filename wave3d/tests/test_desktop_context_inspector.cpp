#include "wave3d/desktop/context_inspector.hpp"
#include "wave3d/desktop/selection_controller.hpp"

#include <QApplication>
#include <QLabel>

#include <stdexcept>
#include <type_traits>

namespace {

using namespace wave3d::desktop;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

QLabel* label(ContextInspector& inspector, const char* name) {
    auto* value = inspector.findChild<QLabel*>(QString::fromUtf8(name));
    if (value == nullptr) throw std::runtime_error("inspector label is missing");
    return value;
}

ContextInspectorState state(
    const QString& project_id = QStringLiteral("project-a"),
    const QString& project_name = QStringLiteral("Project A")) {
    ContextInspectorState result;
    result.project = ProjectInspectorState{
        project_id, project_name, QStringLiteral("/tmp/project-a"),
        QStringLiteral("models/model.h5"), true, 1, 3, 2};
    result.model = ModelInspectorState{
        QStringLiteral("models/model.h5"),
        QStringLiteral("/tmp/project-a/models/model.h5"),
        200, 200, 187, 25.0, 25.0, 25.0,
        4975.0, 4975.0, 4650.0,
        2445.8F, 6000.0F, 1200.0F, 3500.0F, 2100.0F, 2700.0F};
    return result;
}

SelectionContext selection(
    SelectionKind kind,
    QString object_id,
    std::optional<SelectionModelProperty> property = std::nullopt,
    const QString& project_id = QStringLiteral("project-a")) {
    return SelectionContext{
        kind, std::move(object_id), project_id, std::nullopt, property};
}

void test_page_mapping_and_reuse() {
    SelectionController controller;
    ContextInspector inspector(&controller);
    inspector.setState(state());
    expect(inspector.currentPage() == InspectorPage::Empty, "None is not empty");

    controller.setSelection(selection(SelectionKind::Project, QStringLiteral("project-a")));
    expect(inspector.currentPage() == InspectorPage::Project, "Project page is missing");
    expect(label(inspector, "projectInspectorName")->text() == QStringLiteral("Project A"),
           "Project data is incorrect");

    controller.setSelection(selection(SelectionKind::Model, QStringLiteral("models/model.h5")));
    expect(inspector.currentPage() == InspectorPage::Model, "Model page is missing");
    expect(label(inspector, "modelInspectorDimensions")->text() ==
               QStringLiteral("200 × 200 × 187"),
           "Model dimensions are incorrect");

    controller.setSelection(selection(
        SelectionKind::ModelProperty, QStringLiteral("models/model.h5:vp"),
        SelectionModelProperty::Vp));
    auto* property_page = inspector.currentPageWidget();
    expect(inspector.currentPage() == InspectorPage::ModelProperty,
           "Vp did not select the property page");
    expect(label(inspector, "modelPropertyInspectorMinimum")->text().contains(
               QStringLiteral("2445.8")),
           "Vp range is incorrect");

    controller.setSelection(selection(
        SelectionKind::ModelProperty, QStringLiteral("models/model.h5:vs"),
        SelectionModelProperty::Vs));
    expect(inspector.currentPageWidget() == property_page,
           "Vs recreated or replaced the shared property page");
    expect(label(inspector, "modelPropertyInspectorName")->text() == QStringLiteral("Vs"),
           "Vs did not update the shared property page");

    controller.setSelection(selection(
        SelectionKind::ModelProperty, QStringLiteral("models/model.h5:density"),
        SelectionModelProperty::Density));
    expect(inspector.currentPageWidget() == property_page,
           "Density does not reuse the property page");
    expect(label(inspector, "modelPropertyInspectorUnit")->text() ==
               QStringLiteral("kg/m³"),
           "Density unit is incorrect");

    controller.setSelection(selection(SelectionKind::Grid, QStringLiteral("models/model.h5:grid")));
    expect(inspector.currentPage() == InspectorPage::Grid, "Grid page is missing");
    expect(label(inspector, "gridInspectorTotalCells")->text().contains(
               QStringLiteral("7480000")),
           "Grid cell count is incorrect");
}

void test_state_replacement_and_safe_fallback() {
    SelectionController controller;
    ContextInspector inspector(&controller);
    inspector.setState(state());
    controller.setSelection(selection(SelectionKind::Project, QStringLiteral("project-a")));

    inspector.setState({});
    expect(inspector.currentPage() == InspectorPage::Empty,
           "project close retained a stale page");
    expect(label(inspector, "projectInspectorName")->text() == QStringLiteral("—"),
           "project close retained stale data");

    auto next = state(QStringLiteral("project-b"), QStringLiteral("Project B"));
    next.project->root_path = QStringLiteral("/tmp/project-b");
    next.model.reset();
    inspector.setState(next);
    controller.setSelection(selection(
        SelectionKind::Project, QStringLiteral("project-b"), std::nullopt,
        QStringLiteral("project-b")));
    expect(inspector.currentPage() == InspectorPage::Project &&
               label(inspector, "projectInspectorName")->text() ==
                   QStringLiteral("Project B"),
           "project switch did not update inspector state");

    controller.setSelection(selection(
        SelectionKind::Source, QStringLiteral("shot-001:source"), std::nullopt,
        QStringLiteral("project-b")));
    expect(inspector.currentPage() == InspectorPage::Unsupported,
           "unsupported selection has no safe fallback");
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    static_assert(!std::is_base_of_v<QWidget, SelectionContext>);
    static_assert(std::is_same_v<decltype(SelectionContext::object_id), QString>);
    try {
        test_page_mapping_and_reuse();
        test_state_replacement_and_safe_fallback();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
