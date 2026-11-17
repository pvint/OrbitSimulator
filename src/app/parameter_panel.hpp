// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/mission_parameters.hpp"
#include "orbit/core/simulation.hpp"

#include <QWidget>

#include <string>
#include <string_view>
#include <vector>

class QBoxLayout;
class QComboBox;
class QGridLayout;
class QGroupBox;
class QPushButton;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QSlider;

/// Sliders and number boxes for every mission parameter, grouped under
/// headings, plus values derived from them (burn time, thrust-to-weight, ...).
/// Built from orbit::missionParameters(), so new parameters appear here
/// automatically.
class ParameterPanel : public QWidget {
    Q_OBJECT

public:
    explicit ParameterPanel(QWidget* parent = nullptr);

    const orbit::Mission& mission() const { return m_mission; }

    /// Show this mission in the widgets. Does not emit missionChanged.
    void setMission(const orbit::Mission& mission);

    /// Grey out the controls of the parameter being swept (or unlock all with
    /// an empty key). Sweeping burn time locks the propellant, which it sets.
    void setLockedParameter(std::string_view key);

signals:
    /// The user changed something.
    void missionChanged(const orbit::Mission& mission);

private:
    /// One slider + spin box pair, bound to a parameter.
    struct Row {
        const orbit::ParameterInfo* info;
        QWidget* label;
        QSlider* slider;
        QDoubleSpinBox* spinBox;
    };

    void buildDerivedSection(QBoxLayout* layout);
    void buildLunarButtons(QGridLayout* grid);
    void onValueEdited(const Row& row, double value);
    void refreshWidgets();
    void refreshDerived();

    orbit::Mission m_mission;
    std::vector<Row> m_rows;
    std::string m_lockedKey;
    QComboBox* m_plan = nullptr;
    QGroupBox* m_lunarGroup = nullptr;
    QPushButton* m_aimButton = nullptr;
    QPushButton* m_bestTimeButton = nullptr;
    QDateEdit* m_launchDate = nullptr;

    QLabel* m_burnTime = nullptr;
    QLabel* m_massFlow = nullptr;
    QLabel* m_thrustToWeight = nullptr;
    QLabel* m_idealDeltaV = nullptr;
    QLabel* m_rotationBoost = nullptr;
};
