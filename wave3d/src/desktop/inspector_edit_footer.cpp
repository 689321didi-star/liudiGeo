#include "wave3d/desktop/inspector_edit_footer.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <utility>

namespace wave3d::desktop {

InspectorEditFooter::InspectorEditFooter(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("inspectorEditFooter"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(8);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setProperty("inspectorSeparator", true);
    layout->addWidget(separator);

    auto* status_row = new QHBoxLayout;
    status_row->setContentsMargins(0, 0, 0, 0);
    auto* caption = new QLabel(QStringLiteral("Draft status"), this);
    caption->setProperty("secondaryText", true);
    status_row->addWidget(caption);
    status_row->addStretch();
    status_ = new QLabel(QStringLiteral("Unavailable"), this);
    status_->setObjectName(QStringLiteral("inspectorEditStatus"));
    status_->setProperty("editState", QStringLiteral("unavailable"));
    status_row->addWidget(status_);
    layout->addLayout(status_row);

    validation_ = new QLabel(QStringLiteral("No experiment loaded"), this);
    validation_->setObjectName(QStringLiteral("inspectorEditValidation"));
    validation_->setWordWrap(true);
    validation_->setProperty("secondaryText", true);
    layout->addWidget(validation_);

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->addStretch();
    revert_ = new QPushButton(QStringLiteral("Revert"), this);
    revert_->setObjectName(QStringLiteral("inspectorRevertButton"));
    apply_ = new QPushButton(QStringLiteral("Apply"), this);
    apply_->setObjectName(QStringLiteral("inspectorApplyButton"));
    apply_->setProperty("primaryAction", true);
    actions->addWidget(revert_);
    actions->addWidget(apply_);
    layout->addLayout(actions);

    connect(revert_, &QPushButton::clicked, this, [this] {
        if (revert_callback_) revert_callback_();
    });
    connect(apply_, &QPushButton::clicked, this, [this] {
        if (apply_callback_) apply_callback_();
    });
    setState({});
}

void InspectorEditFooter::setCallbacks(
    std::function<void()> revert_callback,
    std::function<void()> apply_callback) {
    revert_callback_ = std::move(revert_callback);
    apply_callback_ = std::move(apply_callback);
}

void InspectorEditFooter::setState(const InspectorEditFooterState& state) {
    QString text;
    QString property;
    switch (state.status) {
    case InspectorEditStatus::Unavailable:
        text = QStringLiteral("Unavailable");
        property = QStringLiteral("unavailable");
        break;
    case InspectorEditStatus::Applied:
        text = QStringLiteral("Applied");
        property = QStringLiteral("applied");
        break;
    case InspectorEditStatus::Modified:
        text = QStringLiteral("Modified");
        property = QStringLiteral("modified");
        break;
    case InspectorEditStatus::Invalid:
        text = QStringLiteral("Invalid");
        property = QStringLiteral("invalid");
        break;
    }
    status_->setText(text);
    if (status_->property("editState").toString() != property) {
        status_->setProperty("editState", property);
        status_->style()->unpolish(status_);
        status_->style()->polish(status_);
    }
    validation_->setText(state.validation_message.isEmpty()
                             ? QStringLiteral("Validation has not run")
                             : state.validation_message);
    validation_->setProperty(
        "validationError", state.status == InspectorEditStatus::Invalid);
    validation_->style()->unpolish(validation_);
    validation_->style()->polish(validation_);
    apply_->setEnabled(state.apply_enabled);
    revert_->setEnabled(state.revert_enabled);
}

} // namespace wave3d::desktop
