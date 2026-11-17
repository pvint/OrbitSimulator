// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/simulation.hpp"
#include "orbit/core/sweep.hpp"
#include "run_set.hpp"

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

/// Set up a parameter sweep and read its results: pick a parameter, a range
/// and a number of steps; the legend lists every flight with its colour and
/// outcome. Clicking a row selects that flight for the other panels.
class SweepPanel : public QWidget {
    Q_OBJECT

public:
    explicit SweepPanel(QWidget* parent = nullptr);

    /// The current mission, used to suggest a range around its value.
    void setMission(const orbit::Mission& mission);

    /// Fill the form from a sweep (e.g. one loaded from a scenario).
    void setSpec(const orbit::SweepSpec& spec);

    /// Show results in the legend, highlighting `selected`.
    void showRunSet(const RunSet& runs, int selected);

signals:
    void sweepRequested(const orbit::SweepSpec& spec);
    void singleFlightRequested();
    void runSelected(int index);

private:
    void suggestRange();
    orbit::SweepSpec currentSpec() const;

    orbit::Mission m_mission;
    QComboBox* m_parameter = nullptr;
    QDoubleSpinBox* m_from = nullptr;
    QDoubleSpinBox* m_to = nullptr;
    QSpinBox* m_steps = nullptr;
    QPushButton* m_run = nullptr;
    QPushButton* m_single = nullptr;
    QLabel* m_hint = nullptr;
    QTableWidget* m_legend = nullptr;
};
