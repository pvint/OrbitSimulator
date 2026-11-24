// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.hpp"

#include "demo_controller.hpp"
#include "display_format.hpp"
#include "flight_stats_panel.hpp"
#include "globe_view.hpp"
#include "orbit/core/mission_parameters.hpp"
#include "orbit/core/scenario.hpp"
#include "parameter_panel.hpp"
#include "plot_panel.hpp"
#include "program_panel.hpp"
#include "simulation_controller.hpp"
#include "sweep_panel.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>

namespace {
/// Where the example scenarios are compiled in (see src/app/CMakeLists.txt).
constexpr QLatin1StringView kExamplesDir(":/scenarios");

QString fileFilter() { return MainWindow::tr("Orbit Simulator scenarios (*.json);;All files (*)"); }
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_simulation(new SimulationController(this)),
      m_globe(new GlobeView(this)), m_demo(new DemoController(*this, *m_globe)),
      m_mission(orbit::defaultMission()) {
    setCentralWidget(m_globe);
    createMenus();
    createDocks();
    connectPanels();
    restoreLayout();
    updateTitle();

    // First flight: the default mission.
    requestSimulation();
}

void MainWindow::createMenus() {
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newScenario);
    fileMenu->addAction(tr("&Open Scenario…"), QKeySequence::Open, this, &MainWindow::openScenario);
    populateExamplesMenu(fileMenu->addMenu(tr("&Examples")));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Save Scenario"), QKeySequence::Save, this, &MainWindow::saveScenario);
    fileMenu->addAction(tr("Save Scenario &As…"), QKeySequence::SaveAs, this,
                        &MainWindow::saveScenarioAs);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    // Filled in by createDocks() with a show/hide entry for each panel.
    m_viewMenu = menuBar()->addMenu(tr("&View"));

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&Demo Tour"), QKeySequence(Qt::Key_F2), this, [this] { startDemo(); });
    helpMenu->addSeparator();
    helpMenu->addAction(tr("&About"), this, &MainWindow::showAbout);
}

void MainWindow::createDocks() {
    // The side panels run the full height of the window; the graphs fit
    // between them, under the 3D view.
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    const auto addDock = [this](const QString& title, const QString& objectName, QWidget* content,
                                Qt::DockWidgetArea area) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(objectName); // needed for saveState()/restoreState()
        dock->setWidget(content);
        addDockWidget(area, dock);
        m_viewMenu->addAction(dock->toggleViewAction());
        return dock;
    };

    // Parameters can be taller than the window, so they scroll.
    m_parameters = new ParameterPanel;
    auto* scroll = new QScrollArea;
    scroll->setWidget(m_parameters);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(m_parameters->sizeHint().width() + 20);
    addDock(tr("Parameters"), QStringLiteral("parametersDock"), scroll, Qt::LeftDockWidgetArea);

    m_flightStats = new FlightStatsPanel;
    m_flightDock =
        addDock(tr("Flight"), QStringLiteral("flightDock"), m_flightStats, Qt::RightDockWidgetArea);

    m_sweepPanel = new SweepPanel;
    m_sweepDock =
        addDock(tr("Sweep"), QStringLiteral("sweepDock"), m_sweepPanel, Qt::RightDockWidgetArea);
    // Flight and Sweep share the right-hand column as tabs; whichever fits
    // the current mode is brought to the front (see startSweep/stopSweep).
    tabifyDockWidget(m_flightDock, m_sweepDock);
    m_flightDock->raise();

    m_plots = new PlotPanel;
    QDockWidget* graphs =
        addDock(tr("Graphs"), QStringLiteral("graphsDock"), m_plots, Qt::BottomDockWidgetArea);

    // The flight program is a wide table, so it shares the bottom with the graphs.
    m_programPanel = new ProgramPanel;
    m_programDock = addDock(tr("Flight program"), QStringLiteral("programDock"), m_programPanel,
                            Qt::BottomDockWidgetArea);
    tabifyDockWidget(graphs, m_programDock);
    graphs->raise();
}

void MainWindow::connectPanels() {
    connect(m_parameters, &ParameterPanel::missionChanged, this,
            [this](const orbit::Mission& mission) {
                // Rocket ↔ natural body: a new kind of flight, a fresh view.
                const bool newVehicle = mission.vehicle != m_mission.vehicle;
                m_mission = mission;
                m_sweepPanel->setMission(mission);
                m_programPanel->setMission(mission);
                if (newVehicle) {
                    m_globe->clearGhosts();
                    m_globe->resetViewOnNextResult();
                    // A rocket's sweep means nothing to a body, and the
                    // other way round.
                    if (m_sweep && !orbit::findSweepTarget(mission, m_sweep->parameter)) {
                        stopSweep(); // runs the simulation
                        return;
                    }
                }
                requestSimulation();
            });
    connect(m_programPanel, &ProgramPanel::missionChanged, this,
            [this](const orbit::Mission& mission) {
                m_mission = mission;
                m_parameters->setMission(mission); // the plan may now be Custom
                m_sweepPanel->setMission(mission);
                requestSimulation();
            });
    connect(m_sweepPanel, &SweepPanel::sweepRequested, this, &MainWindow::startSweep);
    connect(m_sweepPanel, &SweepPanel::singleFlightRequested, this, &MainWindow::stopSweep);
    connect(m_sweepPanel, &SweepPanel::runSelected, this, [this](int index) {
        m_selected = index;
        showSelection();
    });
    connect(m_simulation, &SimulationController::resultReady, this, &MainWindow::onResult);
}

void MainWindow::requestSimulation() { m_simulation->run(m_mission, m_sweep); }

void MainWindow::startSweep(const orbit::SweepSpec& spec) {
    m_sweep = spec;
    m_sweepDock->raise();
    m_parameters->setLockedParameter(spec.parameter);
    m_programPanel->setLockedKey(spec.parameter);
    requestSimulation();
}

void MainWindow::stopSweep() {
    m_sweep.reset();
    m_flightDock->raise();
    m_parameters->setLockedParameter({});
    m_programPanel->setLockedKey({});
    requestSimulation();
}

void MainWindow::onResult(const std::shared_ptr<const RunSet>& runs, double milliseconds) {
    if (!runs || runs->runs.empty()) {
        statusBar()->showMessage(tr("Nothing to show: the sweep settings aren't valid"));
        return;
    }
    // Keep the selection when a sweep is re-run with the same shape (a slider
    // moved); otherwise start in the middle of the sweep.
    const std::optional<orbit::SweepSpec>& sweep = runs->sweep;
    const bool sameShape = m_runs && m_runs->sweep && sweep &&
                           m_runs->runs.size() == runs->runs.size() &&
                           m_runs->sweep->parameter == sweep->parameter;
    if (!sweep) {
        m_selected = 0;
    } else if (!sameShape) {
        m_selected = static_cast<int>(runs->runs.size() - 1) / 2;
    }
    m_runs = runs;
    showSelection();

    if (sweep) {
        const std::optional<orbit::SweepTarget> p =
            orbit::findSweepTarget(m_mission, sweep->parameter);
        statusBar()->showMessage(tr("Sweep of %1: %2 flights in %3 ms")
                                     .arg(p ? QString::fromStdString(p->title)
                                            : QString::fromStdString(sweep->parameter))
                                     .arg(runs->runs.size())
                                     .arg(QLocale().toString(milliseconds, 'f', 1)));
    } else {
        const orbit::SimResult& r = *runs->runs.front().result;
        statusBar()->showMessage(tr("%1 · simulated %2 of flight in %3 ms")
                                     .arg(display::outcomeLabel(r.outcome),
                                          display::duration(r.duration),
                                          QLocale().toString(milliseconds, 'f', 1)));
    }
}

/// Show the selected flight in every panel.
void MainWindow::showSelection() {
    if (!m_runs || m_runs->runs.empty()) return;
    m_selected = std::clamp(m_selected, 0, static_cast<int>(m_runs->runs.size()) - 1);
    m_flightStats->showResult(*m_runs->runs[static_cast<std::size_t>(m_selected)].result);
    m_plots->showRunSet(m_runs, m_selected);
    m_globe->showRunSet(m_runs, m_selected);
    m_sweepPanel->showRunSet(*m_runs, m_selected);
}

// --- Scenario files ---------------------------------------------------------

void MainWindow::startDemo(int firstStep) { m_demo->start(firstStep); }

void MainWindow::newScenario() {
    m_demo->stop();
    m_scenarioName.clear();
    m_scenarioDescription.clear();
    m_scenarioPath.clear();
    m_mission = orbit::defaultMission();
    m_parameters->setMission(m_mission);
    m_sweepPanel->setMission(m_mission);
    m_programPanel->setMission(m_mission);
    m_globe->clearGhosts();
    updateTitle();
    stopSweep();
}

void MainWindow::openScenario() {
    m_demo->stop();
    const QSettings settings;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Scenario"), settings.value(QStringLiteral("scenario/lastDir")).toString(),
        fileFilter());
    if (path.isEmpty()) return;
    if (openScenarioFile(path)) {
        QSettings().setValue(QStringLiteral("scenario/lastDir"), QFileInfo(path).absolutePath());
    }
}

bool MainWindow::openScenarioFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open Scenario"),
                             tr("Could not open %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    // Built-in examples can't be saved back to; Save asks for a new file.
    return openScenarioText(file.readAll(), path.startsWith(QLatin1Char(':')) ? QString() : path);
}

bool MainWindow::openScenarioText(const QByteArray& text, const QString& path) {
    orbit::LoadedScenario loaded;
    try {
        loaded = orbit::scenarioFromJson(
            std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
    } catch (const orbit::ScenarioError& e) {
        QMessageBox::warning(this, tr("Open Scenario"),
                             tr("This file can't be used as a scenario:\n%1").arg(e.what()));
        return false;
    }
    if (!loaded.warnings.empty()) {
        QStringList lines;
        for (const std::string& w : loaded.warnings) lines << QString::fromStdString(w);
        QMessageBox::information(this, tr("Open Scenario"),
                                 tr("The scenario was loaded, but some of it was not "
                                    "understood:\n\n%1")
                                     .arg(lines.join(QLatin1Char('\n'))));
    }

    const orbit::Scenario& s = loaded.scenario;
    m_scenarioName = QString::fromStdString(s.name);
    m_scenarioDescription = QString::fromStdString(s.description);
    m_scenarioPath = path;
    m_mission = s.mission;
    m_parameters->setMission(m_mission);
    m_sweepPanel->setMission(m_mission);
    m_programPanel->setMission(m_mission);
    // A custom program is the point of such a scenario: show it.
    if (m_mission.plan == orbit::FlightPlan::Custom) m_programDock->raise();
    m_globe->clearGhosts();
    m_globe->resetViewOnNextResult();
    updateTitle();
    if (!m_scenarioDescription.isEmpty()) statusBar()->showMessage(m_scenarioDescription, 20000);

    if (s.sweep) {
        m_sweepPanel->setSpec(*s.sweep);
        startSweep(*s.sweep);
    } else {
        stopSweep();
    }
    return true;
}

void MainWindow::saveScenario() {
    if (m_scenarioPath.isEmpty()) {
        saveScenarioAs();
    } else {
        writeScenario(m_scenarioPath);
    }
}

void MainWindow::saveScenarioAs() {
    const QSettings settings;
    QString suggestion = settings.value(QStringLiteral("scenario/lastDir")).toString();
    if (!m_scenarioName.isEmpty()) {
        suggestion = QDir(suggestion).filePath(m_scenarioName + QStringLiteral(".json"));
    }
    const QString path =
        QFileDialog::getSaveFileName(this, tr("Save Scenario"), suggestion, fileFilter());
    if (path.isEmpty()) return;
    QSettings().setValue(QStringLiteral("scenario/lastDir"), QFileInfo(path).absolutePath());
    if (writeScenario(path)) m_scenarioPath = path;
}

bool MainWindow::writeScenario(const QString& path) {
    if (m_scenarioName.isEmpty()) m_scenarioName = QFileInfo(path).completeBaseName();
    orbit::Scenario s;
    s.name = m_scenarioName.toStdString();
    s.description = m_scenarioDescription.toStdString();
    s.mission = m_mission;
    s.sweep = m_sweep;

    // QSaveFile writes to a temporary file and renames it at the end, so a
    // crash or full disk never leaves a half-written scenario behind.
    QSaveFile file(path);
    const std::string json = orbit::scenarioToJson(s);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(json.data(), static_cast<qint64>(json.size())) < 0 || !file.commit()) {
        QMessageBox::warning(this, tr("Save Scenario"),
                             tr("Could not save %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    updateTitle();
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path)), 5000);
    return true;
}

/// One menu entry per example scenario compiled into the app.
void MainWindow::populateExamplesMenu(QMenu* menu) {
    const QDir dir{QString(kExamplesDir)};
    for (const QString& fileName :
         dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(fileName));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray text = file.readAll();
        QString name = fileName;
        try {
            name = QString::fromStdString(
                orbit::scenarioFromJson(
                    std::string_view(text.constData(), static_cast<std::size_t>(text.size())))
                    .scenario.name);
        } catch (const orbit::ScenarioError&) {
            continue; // a broken example is caught by the tests, not shown here
        }
        // Examples aren't files you can save back to, so the path stays empty.
        menu->addAction(name, this, [this, text] {
            m_demo->stop();
            openScenarioText(text, QString());
        });
    }
}

void MainWindow::updateTitle() {
    const QString app = QApplication::applicationName();
    setWindowTitle(m_scenarioName.isEmpty() ? app : m_scenarioName + QStringLiteral(" — ") + app);
}

// --- Window -----------------------------------------------------------------

// Window size, position and panel layout persist between runs.
//
// The layout is saved with a version number. Bump kLayoutVersion whenever the
// set of panels changes: an older saved layout is then ignored once (it
// wouldn't know where the new panels go) and the default below applies.
namespace {
constexpr int kLayoutVersion = 3; // 2: Sweep panel added (M4); 3: Flight program panel
}

void MainWindow::restoreLayout() {
    const QSettings settings;
    if (!restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray())) {
        resize(1500, 900);
    }
    const bool restored =
        restoreState(settings.value(QStringLiteral("window/state")).toByteArray(), kLayoutVersion);
    if (!restored) {
        // Default: a right-hand column wide enough for the sweep legend.
        resizeDocks({m_flightDock}, {360}, Qt::Horizontal);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState(kLayoutVersion));
    event->accept();
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this, tr("About %1").arg(QApplication::applicationName()),
        tr("<h3>%1 %2</h3>"
           "<p>A rocket launch and orbit simulator for building intuition "
           "about orbital mechanics.</p>"
           "<p>Free software under the GNU General Public License, "
           "version 3 or later.</p>"
           "<p>Earth imagery: Blue Marble Next Generation, "
           "NASA Earth Observatory (Reto Stöckli).<br>"
           "Moon imagery: CGI Moon Kit, NASA Scientific Visualization Studio, "
           "from Lunar Reconnaissance Orbiter data.</p>")
            .arg(QApplication::applicationName(), QApplication::applicationVersion()));
}
