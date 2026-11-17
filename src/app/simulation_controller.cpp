// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation_controller.hpp"

#include <QElapsedTimer>
#include <QtConcurrentRun>

SimulationController::SimulationController(QObject* parent) : QObject(parent) {
    connect(&m_watcher, &QFutureWatcher<Run>::finished, this, &SimulationController::onRunFinished);
}

SimulationController::~SimulationController() {
    // Don't leave a worker thread running into a destroyed object.
    m_watcher.waitForFinished();
}

void SimulationController::setMission(const orbit::Mission& mission) {
    m_mission = mission;
    if (m_watcher.isRunning()) {
        m_pending = true;
    } else {
        startRun();
    }
}

void SimulationController::startRun() {
    // The worker gets its own copy of the mission, so nothing is shared
    // between threads while it runs.
    m_watcher.setFuture(QtConcurrent::run([mission = m_mission] {
        QElapsedTimer timer;
        timer.start();
        auto result = std::make_shared<const orbit::SimResult>(orbit::simulate(mission));
        return Run{std::move(result), static_cast<double>(timer.nsecsElapsed()) / 1e6};
    }));
}

// Runs on the GUI thread (QFutureWatcher delivers its signals there).
void SimulationController::onRunFinished() {
    const Run run = m_watcher.result();
    emit resultReady(run.result, run.milliseconds);
    if (m_pending) {
        m_pending = false;
        startRun();
    }
}
