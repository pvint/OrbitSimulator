// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/simulation.hpp"
#include "orbit/core/sweep.hpp"
#include "run_set.hpp"

#include <QMainWindow>

#include <memory>
#include <optional>

class FlightStatsPanel;
class QDockWidget;
class GlobeView;
class ParameterPanel;
class PlotPanel;
class SimulationController;
class SweepPanel;

/// The application window: the 3D view in the centre, parameters on the
/// left, flight read-outs and the sweep panel on the right, graphs along the
/// bottom. Every panel is a dock that can be moved, stacked or hidden (View
/// menu).
///
/// It owns the app's state: the mission, the sweep (if any), the latest
/// results, which flight is selected, and the current scenario file. The
/// panels only show what they are given and report what the user did.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// Open a scenario file (also accepts ":/scenarios/..." for the built-in
    /// examples). Shows a message and returns false if it can't be read.
    bool openScenarioFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createDocks();
    void createMenus();
    void connectPanels();
    void restoreLayout();
    void showAbout();

    void requestSimulation();
    void onResult(const std::shared_ptr<const RunSet>& runs, double milliseconds);
    void showSelection();
    void startSweep(const orbit::SweepSpec& spec);
    void stopSweep();

    // Scenario files (File menu)
    void newScenario();
    void openScenario();
    void saveScenario();
    void saveScenarioAs();
    bool writeScenario(const QString& path);
    void loadScenarioText(const QByteArray& text, const QString& path);
    void populateExamplesMenu(QMenu* menu);
    void updateTitle();

    SimulationController* m_simulation = nullptr;
    GlobeView* m_globe = nullptr;
    ParameterPanel* m_parameters = nullptr;
    FlightStatsPanel* m_flightStats = nullptr;
    PlotPanel* m_plots = nullptr;
    SweepPanel* m_sweepPanel = nullptr;
    QDockWidget* m_flightDock = nullptr;
    QDockWidget* m_sweepDock = nullptr;
    QMenu* m_viewMenu = nullptr;

    orbit::Mission m_mission;
    std::optional<orbit::SweepSpec> m_sweep;
    std::shared_ptr<const RunSet> m_runs;
    int m_selected = 0;

    QString m_scenarioName;
    QString m_scenarioDescription;
    QString m_scenarioPath; ///< empty until saved or opened from a file
};
