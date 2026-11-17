// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/simulation.hpp"

#include <QFutureWatcher>
#include <QObject>

#include <memory>

/// Runs simulations on a worker thread, so the window never freezes, and
/// reports each result back on the GUI thread.
///
/// "Latest wins": at most one simulation runs at a time. If the mission
/// changes while one is running (a slider being dragged), the newest mission
/// is remembered and run as soon as the current run finishes. Missions in
/// between are skipped, since nobody will look at them.
class SimulationController : public QObject {
    Q_OBJECT

public:
    explicit SimulationController(QObject* parent = nullptr);
    ~SimulationController() override;

    SimulationController(const SimulationController&) = delete;
    SimulationController& operator=(const SimulationController&) = delete;
    SimulationController(SimulationController&&) = delete;
    SimulationController& operator=(SimulationController&&) = delete;

    /// Simulate this mission (now, or once the current run is done).
    void setMission(const orbit::Mission& mission);
    const orbit::Mission& mission() const { return m_mission; }

signals:
    /// A finished simulation and how long it took to compute.
    void resultReady(std::shared_ptr<const orbit::SimResult> result, double milliseconds);

private:
    struct Run {
        std::shared_ptr<const orbit::SimResult> result;
        double milliseconds = 0.0;
    };

    void startRun();
    void onRunFinished();

    orbit::Mission m_mission;
    bool m_pending = false; ///< the mission changed while a run was going
    QFutureWatcher<Run> m_watcher;
};
