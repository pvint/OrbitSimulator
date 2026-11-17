// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMainWindow>

class QQuickWidget;

/// The application window: the 3D view in the centre, with dockable panels
/// for parameters (left) and flight readouts (right).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createSceneView();
    void createDocks();
    void createMenus();
    void restoreLayout();
    void showAbout();

    QQuickWidget* m_sceneView = nullptr;
    QMenu* m_viewMenu = nullptr;
};
