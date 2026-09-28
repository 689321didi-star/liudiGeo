#pragma once

#include <QString>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;

namespace wave3d::desktop {

enum class InspectorEditStatus {
    Unavailable,
    Applied,
    Modified,
    Invalid,
};

struct InspectorEditFooterState final {
    InspectorEditStatus status{InspectorEditStatus::Unavailable};
    QString validation_message;
    bool apply_enabled{false};
    bool revert_enabled{false};
};

class InspectorEditFooter final : public QWidget {
public:
    explicit InspectorEditFooter(QWidget* parent = nullptr);

    void setCallbacks(
        std::function<void()> revert_callback,
        std::function<void()> apply_callback);
    void setState(const InspectorEditFooterState& state);

private:
    QLabel* status_{nullptr};
    QLabel* validation_{nullptr};
    QPushButton* revert_{nullptr};
    QPushButton* apply_{nullptr};
    std::function<void()> revert_callback_;
    std::function<void()> apply_callback_;
};

} // namespace wave3d::desktop
