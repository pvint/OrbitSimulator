// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMainWindow>

class FlightStatsPanel;
class ParameterPanel;
class PlotPanel;
class QQuickWidget;
class SimulationController;

/// The application window: the 3D view in the centre, parameters on the
/// left, flight read-outs on the right and graphs along the bottom. Every panel is a dock that
/// can be moved, stacked or hidden (View menu).
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
    void connectSimulation();
    void restoreLayout();
    void showAbout();

    SimulationController* m_simulation = nullptr;
    QQuickWidget* m_sceneView = nullptr;
    ParameterPanel* m_parameters = nullptr;
    FlightStatsPanel* m_flightStats = nullptr;
    PlotPanel* m_plots = nullptr;
    QMenu* m_viewMenu = nullptr;
};
