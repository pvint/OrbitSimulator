#include "main_window.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QQmlError>
#include <QQuickWidget>
#include <QSettings>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QApplication::applicationName());
    createSceneView();
    createMenus();
    createDocks();
    restoreLayout();
    statusBar()->showMessage(tr("Ready"));
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
    // Placeholders until the real panels arrive in M2.
    const auto addDock = [this](const QString& title, const QString& objectName,
                                Qt::DockWidgetArea area) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(objectName); // needed for saveState()/restoreState()
        auto* placeholder = new QLabel(tr("(coming in M2)"), dock);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setMinimumWidth(240);
        dock->setWidget(placeholder);
        addDockWidget(area, dock);
        m_viewMenu->addAction(dock->toggleViewAction());
    };
    addDock(tr("Parameters"), QStringLiteral("parametersDock"), Qt::LeftDockWidgetArea);
    addDock(tr("Flight"), QStringLiteral("flightDock"), Qt::RightDockWidgetArea);
}

// Window size, position and panel layout persist between runs.
void MainWindow::restoreLayout() {
    const QSettings settings;
    if (!restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray())) {
        resize(1400, 850);
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
           "about orbital mechanics.</p>")
            .arg(QApplication::applicationName(), QApplication::applicationVersion()));
}
