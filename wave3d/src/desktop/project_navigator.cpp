#include "wave3d/desktop/project_navigator.hpp"

#include "wave3d/desktop/selection_controller.hpp"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace wave3d::desktop {

struct ProjectNavigatorModel::Node final {
    ProjectNavigatorNodeKind kind{ProjectNavigatorNodeKind::Information};
    QString label;
    bool selectable{false};
    std::optional<SelectionContext> selection;
    Node* parent{nullptr};
    std::vector<std::unique_ptr<Node>> children;
};

namespace {

ProjectNavigatorModel::Node* add_node(
    ProjectNavigatorModel::Node* parent,
    ProjectNavigatorNodeKind kind,
    QString label,
    std::optional<SelectionContext> selection = std::nullopt) {
    auto child = std::make_unique<ProjectNavigatorModel::Node>();
    child->kind = kind;
    child->label = std::move(label);
    child->selectable = selection.has_value();
    child->selection = std::move(selection);
    child->parent = parent;
    auto* result = child.get();
    parent->children.push_back(std::move(child));
    return result;
}

bool is_project_selection(SelectionKind kind) {
    return kind != SelectionKind::None && kind != SelectionKind::File &&
           kind != SelectionKind::WorkflowStep;
}

} // namespace

ProjectNavigatorModel::ProjectNavigatorModel(QObject* parent)
    : QAbstractItemModel(parent), root_(std::make_unique<Node>()) {}

ProjectNavigatorModel::~ProjectNavigatorModel() = default;

void ProjectNavigatorModel::setProjectState(const ProjectNavigatorState& state) {
    beginResetModel();
    root_ = std::make_unique<Node>();
    if (state.project_id.isEmpty()) {
        add_node(
            root_.get(), ProjectNavigatorNodeKind::Information,
            QStringLiteral("No project open"));
        endResetModel();
        return;
    }

    const auto project_context = SelectionContext{
        SelectionKind::Project, state.project_id, state.project_id};
    auto* project = add_node(
        root_.get(), ProjectNavigatorNodeKind::Project,
        state.project_name, project_context);

    auto* model = add_node(
        project, ProjectNavigatorNodeKind::ModelGroup,
        QStringLiteral("Model"),
        state.model_loaded
            ? std::optional<SelectionContext>{SelectionContext{
                  SelectionKind::Model, state.model_id, state.project_id}}
            : std::nullopt);
    if (state.model_loaded) {
        const auto add_property = [&](QString label, SelectionModelProperty property) {
            const auto property_key =
                property == SelectionModelProperty::Vp
                    ? QStringLiteral("vp")
                    : property == SelectionModelProperty::Vs
                          ? QStringLiteral("vs")
                          : QStringLiteral("density");
            add_node(
                model, ProjectNavigatorNodeKind::ModelProperty,
                std::move(label),
                SelectionContext{
                    SelectionKind::ModelProperty,
                    state.model_id + QLatin1Char(':') + property_key,
                    state.project_id, std::nullopt, property});
        };
        add_property(QStringLiteral("Vp"), SelectionModelProperty::Vp);
        add_property(QStringLiteral("Vs"), SelectionModelProperty::Vs);
        add_property(QStringLiteral("Density"), SelectionModelProperty::Density);
        add_node(
            model, ProjectNavigatorNodeKind::Grid, QStringLiteral("Grid"),
            SelectionContext{
                SelectionKind::Grid,
                state.model_id + QStringLiteral(":grid"), state.project_id});
    } else {
        add_node(
            model, ProjectNavigatorNodeKind::Information,
            QStringLiteral("Not loaded"));
    }

    auto* acquisition = add_node(
        project, ProjectNavigatorNodeKind::AcquisitionGroup,
        QStringLiteral("Acquisition"));
    if (state.source_configured) {
        add_node(
            acquisition, ProjectNavigatorNodeKind::Source,
            QStringLiteral("Source"),
            SelectionContext{
                SelectionKind::Source, state.source_id, state.project_id});
    } else {
        add_node(
            acquisition, ProjectNavigatorNodeKind::Information,
            QStringLiteral("Source · Not configured"));
    }
    if (state.receiver_set_configured) {
        add_node(
            acquisition, ProjectNavigatorNodeKind::ReceiverSet,
            QStringLiteral("Receivers"),
            SelectionContext{
                SelectionKind::ReceiverSet, state.receiver_set_id,
                state.project_id});
    } else {
        add_node(
            acquisition, ProjectNavigatorNodeKind::Information,
            QStringLiteral("Receivers · Not configured"));
    }

    auto* simulation_group = add_node(
        project, ProjectNavigatorNodeKind::SimulationGroup,
        QStringLiteral("Simulation"));
    const auto simulation_id = state.project_id + QStringLiteral(":elastic-forward");
    add_node(
        simulation_group, ProjectNavigatorNodeKind::Simulation,
        QStringLiteral("Elastic Forward"),
        SelectionContext{
            SelectionKind::Simulation, simulation_id, state.project_id});
    add_node(
        simulation_group, ProjectNavigatorNodeKind::Boundary,
        QStringLiteral("Boundary"),
        SelectionContext{
            SelectionKind::Boundary, simulation_id + QStringLiteral(":boundary"),
            state.project_id});
    add_node(
        simulation_group, ProjectNavigatorNodeKind::Output,
        QStringLiteral("Output"),
        SelectionContext{
            SelectionKind::Output, simulation_id + QStringLiteral(":output"),
            state.project_id});

    auto* runs = add_node(
        project, ProjectNavigatorNodeKind::RunsGroup, QStringLiteral("Runs"));
    auto* results = add_node(
        project, ProjectNavigatorNodeKind::ResultsGroup,
        QStringLiteral("Results"));
    if (state.runs.isEmpty()) {
        add_node(
            runs, ProjectNavigatorNodeKind::Information,
            QStringLiteral("No runs"));
        add_node(
            results, ProjectNavigatorNodeKind::Information,
            QStringLiteral("No results"));
    } else {
        bool has_result = false;
        for (const auto& run : state.runs) {
            add_node(
                runs, ProjectNavigatorNodeKind::Run, run.run_id,
                SelectionContext{
                    SelectionKind::Run, run.run_id, state.project_id,
                    run.run_id});
            if (run.has_result) {
                has_result = true;
                add_node(
                    results, ProjectNavigatorNodeKind::Result,
                    run.run_id,
                    SelectionContext{
                        SelectionKind::Result,
                        run.run_id + QStringLiteral(":result"),
                        state.project_id, run.run_id});
            }
        }
        if (!has_result) {
            add_node(
                results, ProjectNavigatorNodeKind::Information,
                QStringLiteral("No results"));
        }
    }
    endResetModel();
}

QModelIndex ProjectNavigatorModel::index(
    int row,
    int column,
    const QModelIndex& parent_index) const {
    if (column != 0 || row < 0) {
        return {};
    }
    const auto* parent_node = parent_index.isValid()
                                  ? static_cast<Node*>(parent_index.internalPointer())
                                  : root_.get();
    if (static_cast<std::size_t>(row) >= parent_node->children.size()) {
        return {};
    }
    return createIndex(row, column, parent_node->children[row].get());
}

QModelIndex ProjectNavigatorModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return {};
    const auto* node = static_cast<Node*>(child.internalPointer());
    const auto* parent_node = node->parent;
    if (parent_node == nullptr || parent_node == root_.get()) return {};
    const auto* grandparent = parent_node->parent;
    const auto found = std::find_if(
        grandparent->children.begin(), grandparent->children.end(),
        [parent_node](const auto& value) { return value.get() == parent_node; });
    return createIndex(
        static_cast<int>(std::distance(grandparent->children.begin(), found)),
        0, const_cast<Node*>(parent_node));
}

int ProjectNavigatorModel::rowCount(const QModelIndex& parent_index) const {
    if (parent_index.column() > 0) return 0;
    const auto* node = parent_index.isValid()
                           ? static_cast<Node*>(parent_index.internalPointer())
                           : root_.get();
    return static_cast<int>(node->children.size());
}

int ProjectNavigatorModel::columnCount(const QModelIndex&) const { return 1; }

QVariant ProjectNavigatorModel::data(const QModelIndex& item, int role) const {
    if (!item.isValid()) return {};
    const auto* node = static_cast<Node*>(item.internalPointer());
    if (role == Qt::DisplayRole) return node->label;
    if (role == Qt::ToolTipRole && !node->selectable) return node->label;
    return {};
}

Qt::ItemFlags ProjectNavigatorModel::flags(const QModelIndex& item) const {
    if (!item.isValid()) return Qt::NoItemFlags;
    const auto* node = static_cast<Node*>(item.internalPointer());
    if (node->kind == ProjectNavigatorNodeKind::Information) {
        return Qt::NoItemFlags;
    }
    return node->selectable
               ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
               : Qt::ItemIsEnabled;
}

ProjectNavigatorNodeKind ProjectNavigatorModel::nodeKind(
    const QModelIndex& item) const {
    return item.isValid()
               ? static_cast<Node*>(item.internalPointer())->kind
               : ProjectNavigatorNodeKind::Information;
}

std::optional<SelectionContext> ProjectNavigatorModel::selectionForIndex(
    const QModelIndex& item) const {
    return item.isValid()
               ? static_cast<Node*>(item.internalPointer())->selection
               : std::nullopt;
}

QModelIndex ProjectNavigatorModel::indexForSelection(
    const SelectionContext& selection) const {
    std::function<QModelIndex(const Node*)> find = [&](const Node* node) {
        for (std::size_t row = 0; row < node->children.size(); ++row) {
            const auto* child = node->children[row].get();
            if (child->selection && *child->selection == selection) {
                return createIndex(static_cast<int>(row), 0, child);
            }
            const auto nested = find(child);
            if (nested.isValid()) return nested;
        }
        return QModelIndex{};
    };
    return find(root_.get());
}

QModelIndex ProjectNavigatorModel::projectIndex() const {
    if (root_->children.empty() ||
        root_->children.front()->kind != ProjectNavigatorNodeKind::Project) {
        return {};
    }
    return createIndex(0, 0, root_->children.front().get());
}

ProjectNavigator::ProjectNavigator(
    SelectionController* selection_controller,
    QWidget* parent)
    : QWidget(parent), selection_controller_(selection_controller) {
    setObjectName(QStringLiteral("projectNavigator"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    model_ = new ProjectNavigatorModel(this);
    tree_ = new QTreeView(this);
    tree_->setObjectName(QStringLiteral("projectNavigatorTree"));
    tree_->setModel(model_);
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    tree_->setIndentation(18);
    tree_->setAlternatingRowColors(false);
    layout->addWidget(tree_);
    connect(
        tree_->selectionModel(), &QItemSelectionModel::currentChanged,
        this, [this](const QModelIndex& current, const QModelIndex&) {
            applySelection(current);
        });
}

void ProjectNavigator::setProjectState(const ProjectNavigatorState& state) {
    const auto previous = selection_controller_->currentSelection();
    model_->setProjectState(state);
    expandDefaultNodes();
    if (selectContext(previous)) {
        return;
    }
    if (!is_project_selection(previous.kind)) {
        return;
    }
    const auto project = model_->projectIndex();
    if (project.isValid()) {
        tree_->setCurrentIndex(project);
        applySelection(project);
    } else {
        tree_->clearSelection();
        selection_controller_->clearSelection();
    }
}

ProjectNavigatorModel* ProjectNavigator::model() const noexcept { return model_; }
QTreeView* ProjectNavigator::treeView() const noexcept { return tree_; }

bool ProjectNavigator::selectContext(const SelectionContext& selection) {
    const auto index = model_->indexForSelection(selection);
    if (!index.isValid()) return false;
    tree_->setCurrentIndex(index);
    tree_->scrollTo(index);
    return true;
}

void ProjectNavigator::publishCurrentSelection() {
    auto current = tree_->currentIndex();
    if (!model_->selectionForIndex(current)) {
        current = model_->projectIndex();
        if (current.isValid()) tree_->setCurrentIndex(current);
    }
    applySelection(current);
}

void ProjectNavigator::applySelection(const QModelIndex& index) {
    const auto selection = model_->selectionForIndex(index);
    if (selection) selection_controller_->setSelection(*selection);
}

void ProjectNavigator::expandDefaultNodes() {
    const auto project = model_->projectIndex();
    if (!project.isValid()) return;
    tree_->expand(project);
    for (int row = 0; row < model_->rowCount(project); ++row) {
        const auto child = model_->index(row, 0, project);
        const auto kind = model_->nodeKind(child);
        if (kind == ProjectNavigatorNodeKind::ModelGroup ||
            kind == ProjectNavigatorNodeKind::AcquisitionGroup ||
            kind == ProjectNavigatorNodeKind::SimulationGroup ||
            (kind == ProjectNavigatorNodeKind::RunsGroup &&
             model_->rowCount(child) > 1) ||
            (kind == ProjectNavigatorNodeKind::ResultsGroup &&
             model_->rowCount(child) > 1)) {
            tree_->expand(child);
        }
    }
}

} // namespace wave3d::desktop
