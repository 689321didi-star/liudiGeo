#pragma once

#include <QMainWindow>

namespace wave3d::desktop {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
};

} // namespace wave3d::desktop
