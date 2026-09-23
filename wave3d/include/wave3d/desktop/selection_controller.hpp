#pragma once

#include "wave3d/desktop/selection_context.hpp"

#include <QObject>

namespace wave3d::desktop {

class SelectionController final : public QObject {
    Q_OBJECT

public:
    explicit SelectionController(QObject* parent = nullptr);

    [[nodiscard]] const SelectionContext& currentSelection() const noexcept;

public slots:
    void setSelection(const SelectionContext& selection);
    void clearSelection();

signals:
    void selectionChanged(const SelectionContext& selection);

private:
    SelectionContext current_selection_;
};

} // namespace wave3d::desktop
