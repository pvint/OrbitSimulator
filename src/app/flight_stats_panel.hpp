// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/simulation.hpp"

#include <QWidget>

class QGroupBox;
class QLabel;

/// Read-outs for one flight: outcome, the final orbit, and the engine events.
class FlightStatsPanel : public QWidget {
    Q_OBJECT

public:
    explicit FlightStatsPanel(QWidget* parent = nullptr);

    void showResult(const orbit::SimResult& result);

private:
    QLabel* m_outcome = nullptr;
    QLabel* m_periapsis = nullptr;
    QLabel* m_apoapsis = nullptr;
    QLabel* m_inclination = nullptr;
    QLabel* m_period = nullptr;
    QLabel* m_eccentricity = nullptr;
    QLabel* m_maxAltitude = nullptr;
    QLabel* m_deltaV = nullptr;
    QLabel* m_propellantLeft = nullptr;
    QLabel* m_maxQ = nullptr;
    QLabel* m_dragLoss = nullptr;
    QLabel* m_duration = nullptr;
    QLabel* m_events = nullptr;
    QGroupBox* m_stagesGroup = nullptr;
    QLabel* m_stages = nullptr;

    QGroupBox* m_moonGroup = nullptr;
    QLabel* m_moonClosest = nullptr;
    QLabel* m_moonClosestTime = nullptr;
    QLabel* m_moonOrbit = nullptr;
    QLabel* m_moonPeriod = nullptr;
};
