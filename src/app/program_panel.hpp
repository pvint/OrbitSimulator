// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/simulation.hpp"

#include <QWidget>

#include <string>
#include <string_view>

class QLabel;
class QPushButton;
class QTableWidget;

/// The maneuver editor: the mission's flight program as a table of steps
/// (burn or coast, steering, end condition, value, timeout).
///
/// For the built-in flight plans it shows, read-only, the steps the plan
/// makes, which is a good way to learn what "Lunar orbit" actually does.
/// "Customize" copies them into a custom program that can be edited, with
/// steps added, removed and reordered.
class ProgramPanel : public QWidget {
    Q_OBJECT

public:
    explicit ProgramPanel(QWidget* parent = nullptr);

    /// Show this mission's program. Does not emit missionChanged.
    void setMission(const orbit::Mission& mission);
    const orbit::Mission& mission() const { return m_mission; }

    /// A swept step value ("step-3-value") is set by the sweep, so its cell
    /// is locked. Any other key (or none) unlocks it.
    void setLockedKey(std::string_view key);

signals:
    void missionChanged(const orbit::Mission& mission);

private:
    void rebuild();
    void addRow(int row, const orbit::FlightStep& step, bool editable);
    void changed();
    void customize();
    void addStep();
    void removeStep();
    void moveStep(int offset);
    int currentStep() const;

    orbit::Mission m_mission;
    orbit::FlightProgram m_shown; ///< the program the table shows
    std::string m_lockedKey;
    QLabel* m_hint = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_customize = nullptr;
    QPushButton* m_add = nullptr;
    QPushButton* m_remove = nullptr;
    QPushButton* m_up = nullptr;
    QPushButton* m_down = nullptr;
};
