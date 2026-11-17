// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation_controller.hpp"

#include <QElapsedTimer>
#include <QtConcurrentMap>
#include <QtConcurrentRun>

#include <stdexcept>
#include <vector>

SimulationController::SimulationController(QObject* parent) : QObject(parent) {
    connect(&m_watcher, &QFutureWatcher<Output>::finished, this, &SimulationController::onFinished);
}

SimulationController::~SimulationController() {
    // Don't leave a worker thread running into a destroyed object.
    m_watcher.waitForFinished();
    m_sweepPool.waitForDone();
}

void SimulationController::run(const orbit::Mission& mission,
                               const std::optional<orbit::SweepSpec>& sweep) {
    const Request request{mission, sweep};
    if (m_watcher.isRunning()) {
        m_pending = request;
    } else {
        start(request);
    }
}

void SimulationController::start(const Request& request) {
    // The worker gets its own copy of the request, so nothing is shared
    // between threads while it runs.
    m_watcher.setFuture(QtConcurrent::run([request, pool = &m_sweepPool] {
        QElapsedTimer timer;
        timer.start();
        auto runs = std::make_shared<RunSet>();
        runs->sweep = request.sweep;

        if (!request.sweep) {
            runs->runs.push_back(
                {0.0, QColor(0x38, 0xbd, 0xf8),
                 std::make_shared<const orbit::SimResult>(orbit::simulate(request.mission))});
        } else {
            try {
                const std::vector<orbit::Mission> missions =
                    orbit::sweepMissions(request.mission, *request.sweep);
                const std::vector<double> values = orbit::sweepValues(*request.sweep);
                const auto results = QtConcurrent::blockingMapped<QList<orbit::SimResult>>(
                    pool, missions, [](const orbit::Mission& m) { return orbit::simulate(m); });
                const auto count = static_cast<qsizetype>(missions.size());
                for (qsizetype i = 0; i < count; ++i) {
                    const double t =
                        count > 1 ? static_cast<double>(i) / static_cast<double>(count - 1) : 0.5;
                    runs->runs.push_back({values[static_cast<std::size_t>(i)], sweepColor(t),
                                          std::make_shared<const orbit::SimResult>(results[i])});
                }
            } catch (const std::invalid_argument&) {
                // Unknown sweep parameter: report an empty set rather than
                // throwing across threads. (The UI only offers valid parameters.)
                runs->runs.clear();
            }
        }
        return Output{std::move(runs), static_cast<double>(timer.nsecsElapsed()) / 1e6};
    }));
}

// Runs on the GUI thread (QFutureWatcher delivers its signals there).
void SimulationController::onFinished() {
    const Output output = m_watcher.result();
    emit resultReady(output.runs, output.milliseconds);
    if (m_pending) {
        const Request next = *m_pending;
        m_pending.reset();
        start(next);
    }
}
