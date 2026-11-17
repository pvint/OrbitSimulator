// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/simulation.hpp"
#include "orbit/core/sweep.hpp"
#include "run_set.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QThreadPool>

#include <memory>
#include <optional>

/// Runs simulations on worker threads, so the window never freezes, and
/// reports each result back on the GUI thread.
///
/// A request is a mission, optionally with a sweep. A sweep's flights run in
/// parallel on all CPU cores.
///
/// "Latest wins": at most one request is computed at a time. If a new request
/// arrives while one is running (a slider being dragged), it is remembered
/// and run as soon as the current one finishes. Requests in between are
/// skipped, since nobody will look at them.
class SimulationController : public QObject {
    Q_OBJECT

public:
    explicit SimulationController(QObject* parent = nullptr);
    ~SimulationController() override;

    SimulationController(const SimulationController&) = delete;
    SimulationController& operator=(const SimulationController&) = delete;
    SimulationController(SimulationController&&) = delete;
    SimulationController& operator=(SimulationController&&) = delete;

    /// Simulate this mission, or the sweep over it (now, or once the current
    /// request is done).
    void run(const orbit::Mission& mission, const std::optional<orbit::SweepSpec>& sweep = {});

signals:
    /// A finished request and how long it took to compute.
    void resultReady(std::shared_ptr<const RunSet> runs, double milliseconds);

private:
    struct Request {
        orbit::Mission mission;
        std::optional<orbit::SweepSpec> sweep;
    };
    struct Output {
        std::shared_ptr<const RunSet> runs;
        double milliseconds = 0.0;
    };

    void start(const Request& request);
    void onFinished();

    std::optional<Request> m_pending; ///< arrived while a request was running
    QFutureWatcher<Output> m_watcher;
    /// Sweep flights run here, not in the global pool that runs the request
    /// itself, so a request never waits for threads it is occupying.
    QThreadPool m_sweepPool;
};
