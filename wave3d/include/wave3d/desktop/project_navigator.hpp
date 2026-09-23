#pragma once

#include "wave3d/desktop/selection_context.hpp"

#include <QAbstractItemModel>
#include <QVector>
#include <QWidget>

#include <memory>
#include <optional>

class QTreeView;

namespace wave3d::desktop {

class SelectionController;

enum class ProjectNavigatorNodeKind {
    Information,
    Project,
    ModelGroup,
    ModelProperty,
    Grid,
    AcquisitionGroup,
    Source,
    ReceiverSet,
    SimulationGroup,
    Simulation,
    Boundary,
    Output,
    RunsGroup,
    Run,
    ResultsGroup,
    Result,
};

struct ProjectNavigatorRun final {
    QString run_id;
    bool has_result{false};
};

struct ProjectNavigatorState final {
    QString project_id;
    QString project_name;
    QString model_id;
    bool model_loaded{false};
    QString source_id;
    bool source_configured{false};
    QString receiver_set_id;
    bool receiver_set_configured{false};
    QVector<ProjectNavigatorRun> runs;
};

class ProjectNavigatorModel final : public QAbstractItemModel {
public:
    struct Node;

    explicit ProjectNavigatorModel(QObject* parent = nullptr);
    ~ProjectNavigatorModel() override;

    void setProjectState(const ProjectNavigatorState& state);

    [[nodiscard]] QModelIndex index(
        int row,
        int column,
        const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex& index,
        int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

    [[nodiscard]] ProjectNavigatorNodeKind nodeKind(
        const QModelIndex& index) const;
    [[nodiscard]] std::optional<SelectionContext> selectionForIndex(
        const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexForSelection(
        const SelectionContext& selection) const;
    [[nodiscard]] QModelIndex projectIndex() const;

private:
    std::unique_ptr<Node> root_;
};

class ProjectNavigator final : public QWidget {
public:
    explicit ProjectNavigator(
        SelectionController* selection_controller,
        QWidget* parent = nullptr);

    void setProjectState(const ProjectNavigatorState& state);
    [[nodiscard]] ProjectNavigatorModel* model() const noexcept;
    [[nodiscard]] QTreeView* treeView() const noexcept;
    [[nodiscard]] bool selectContext(const SelectionContext& selection);
    void publishCurrentSelection();

private:
    void applySelection(const QModelIndex& index);
    void expandDefaultNodes();

    SelectionController* selection_controller_{nullptr};
    ProjectNavigatorModel* model_{nullptr};
    QTreeView* tree_{nullptr};
};

} // namespace wave3d::desktop
