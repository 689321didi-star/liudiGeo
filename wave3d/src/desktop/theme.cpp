#include "wave3d/desktop/theme.hpp"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace wave3d::desktop {

void apply_scientific_theme(QApplication& application) {
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    application.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 10));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(13, 20, 29));
    palette.setColor(QPalette::WindowText, QColor(224, 234, 242));
    palette.setColor(QPalette::Base, QColor(9, 15, 22));
    palette.setColor(QPalette::AlternateBase, QColor(20, 30, 42));
    palette.setColor(QPalette::ToolTipBase, QColor(24, 36, 49));
    palette.setColor(QPalette::ToolTipText, QColor(235, 243, 249));
    palette.setColor(QPalette::Text, QColor(224, 234, 242));
    palette.setColor(QPalette::Button, QColor(24, 36, 49));
    palette.setColor(QPalette::ButtonText, QColor(224, 234, 242));
    palette.setColor(QPalette::BrightText, QColor(255, 255, 255));
    palette.setColor(QPalette::Highlight, QColor(25, 155, 210));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::PlaceholderText, QColor(111, 132, 148));
    application.setPalette(palette);

    application.setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget { background: #0d141d; color: #e0eaf2; }
        QLabel { background: transparent; }
        QMenuBar, QMenu, QToolBar, QStatusBar { background: #111c28; border: none; }
        QMenuBar::item, QMenu::item { padding: 7px 12px; border-radius: 5px; }
        QMenuBar::item:selected, QMenu::item:selected { background: #1b3447; }
        QToolBar { spacing: 8px; padding: 7px 10px; border-bottom: 1px solid #263746; }
        QToolButton { background: transparent; color: #cbdbe7; border: 1px solid transparent; border-radius: 6px; padding: 6px 10px; }
        QToolButton:hover { background: #1a2a39; border-color: #314a5d; }
        QToolButton:disabled { color: #657783; background: transparent; }
        QDockWidget { color: #cbdbe7; font-weight: 600; }
        QDockWidget::title { background: #111c28; border-bottom: 1px solid #263746; padding: 9px 12px; }
        QGroupBox { background: #121d29; border: 1px solid #273a4a; border-radius: 8px; margin-top: 14px; padding: 12px 10px 10px 10px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #9fc7dd; }
        QListWidget, QTextEdit, QComboBox { background: #0a1119; border: 1px solid #273a4a; border-radius: 7px; padding: 5px; selection-background-color: #174d69; selection-color: #ffffff; }
        QListWidget::item { min-height: 31px; border-radius: 5px; padding-left: 9px; }
        QListWidget::item:hover { background: #162736; }
        QListWidget::item:selected { background: #175270; }
        QComboBox { min-height: 28px; padding-left: 9px; }
        QComboBox::drop-down { border: none; width: 24px; }
        QPushButton { min-height: 30px; background: #1a3446; border: 1px solid #31556b; border-radius: 7px; padding: 3px 12px; }
        QPushButton:hover { background: #20506b; border-color: #42a7d3; }
        QPushButton:pressed { background: #12394f; }
        QPushButton:disabled { color: #657783; background: #17212a; border-color: #26343e; }
        QPushButton#startRunButton { background: #087ca8; border-color: #32b8e8; font-weight: 700; }
        QPushButton#startRunButton:disabled { color: #657783; background: #17212a; border-color: #26343e; }
        QProgressBar { min-height: 9px; max-height: 9px; border: none; border-radius: 4px; background: #26343e; text-align: center; }
        QProgressBar::chunk { background: #22a9da; border-radius: 4px; }
        QSplitter::handle { background: #1e2e3b; }
        QSplitter::handle:hover { background: #278cb4; }
        QFrame#workspaceHeader { background: #111d29; border: 1px solid #263b4c; border-radius: 9px; }
        QLabel#workspaceTitle { font-size: 18px; font-weight: 700; color: #f1f7fb; }
        QLabel#workspaceSubtitle { color: #87a4b8; }
        QLabel#modelStateBadge { color: #8ed8f3; background: #12364a; border: 1px solid #27627e; border-radius: 10px; padding: 4px 10px; }
        QStatusBar { color: #91adbf; border-top: 1px solid #263746; }
        QToolTip { color: #f2f7fa; background: #1a2b3a; border: 1px solid #3a5a70; }
    )"));
}

} // namespace wave3d::desktop
