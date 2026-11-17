// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.hpp"

#include "display_format.hpp"
#include "flight_stats_panel.hpp"
#include "parameter_panel.hpp"
#include "plot_panel.hpp"
#include "simulation_controller.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QQmlError>
#include <QQuickWidget>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_simulation(new SimulationController(this)) {
    setWindowTitle(QApplication::applicationName());
    createSceneView();
    createMenus();
    createDocks();
    connectSimulation();
    restoreLayout();

    // First flight: the default mission.
    m_simulation->setMission(m_parameters->mission());
}

void MainWindow::createSceneView() {
    m_sceneView = new QQuickWidget(this);
    m_sceneView->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_sceneView->setSource(QUrl(QStringLiteral("qrc:/qt/qml/OrbitSim/qml/SceneView.qml")));

    // QML errors would otherwise only appear on the console; show them where
    // they can't be missed.
    if (m_sceneView->status() == QQuickWidget::Error) {
        QStringList messages;
        for (const QQmlError& error : m_sceneView->errors()) {
            messages << error.toString();
        }
        qWarning().noquote() << messages.join(QLatin1Char('\n'));
    }
    setCentralWidget(m_sceneView);
}

void MainWindow::createMenus() {
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* quit = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    // Filled in by createDocks() with a show/hide entry for each panel.
    m_viewMenu = menuBar()->addMenu(tr("&View"));

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
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
    addDock(tr("Flight"), QStringLiteral("flightDock"), m_flightStats, Qt::RightDockWidgetArea);

    m_plots = new PlotPanel;
    addDock(tr("Graphs"), QStringLiteral("graphsDock"), m_plots, Qt::BottomDockWidgetArea);
}

void MainWindow::connectSimulation() {
    connect(m_parameters, &ParameterPanel::missionChanged, m_simulation,
            &SimulationController::setMission);

    connect(m_simulation, &SimulationController::resultReady, this,
            [this](const std::shared_ptr<const orbit::SimResult>& result, double milliseconds) {
                m_flightStats->showResult(*result);
                m_plots->showResult(result);
                statusBar()->showMessage(tr("%1 · simulated %2 of flight in %3 ms")
                                             .arg(display::outcomeLabel(result->outcome),
                                                  display::duration(result->duration),
                                                  QLocale().toString(milliseconds, 'f', 1)));
            });
}

// Window size, position and panel layout persist between runs.
void MainWindow::restoreLayout() {
    const QSettings settings;
    if (!restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray())) {
        resize(1500, 900);
    }
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    event->accept();
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this, tr("About %1").arg(QApplication::applicationName()),
        tr("<h3>%1 %2</h3>"
           "<p>A rocket launch and orbit simulator for building intuition "
           "about orbital mechanics.</p>"
           "<p>Free software under the GNU General Public License, "
           "version 3 or later.</p>")
            .arg(QApplication::applicationName(), QApplication::applicationVersion()));
}
