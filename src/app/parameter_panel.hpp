// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/mission_parameters.hpp"
#include "orbit/core/simulation.hpp"

#include <QWidget>

#include <array>
#include <string>
#include <string_view>
#include <utility>
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

    void buildVehicleChoice(QBoxLayout* layout);
    void buildBodyChoice(QBoxLayout* layout);
    void buildDerivedSection(QBoxLayout* layout);
    void buildBodyDerivedSection(QBoxLayout* layout);
    void buildLunarButtons(QGridLayout* grid);
    void onValueEdited(const Row& row, double value);
    void refreshWidgets();
    void refreshDerived();
    void refreshBodyDerived();

    orbit::Mission m_mission;
    std::vector<Row> m_rows;
    std::string m_lockedKey;
    QComboBox* m_vehicle = nullptr;
    QComboBox* m_bodyGiven = nullptr;
    QGroupBox* m_bodyGroup = nullptr; ///< holds the "Given as" choice
    /// Every parameter group, and the parameters in it (for showing only
    /// those that the mission uses: a rocket's, or a natural body's).
    std::vector<std::pair<QGroupBox*, const orbit::ParameterInfo*>> m_groups;
    QComboBox* m_plan = nullptr;
    QGroupBox* m_lunarGroup = nullptr;
    /// "Stage 1" ... "Stage 3": the upper ones are hidden until fitted.
    std::array<QGroupBox*, orbit::kMaxStages> m_stageGroups{};
    QPushButton* m_aimButton = nullptr;
    QPushButton* m_bestTimeButton = nullptr;
    QDateEdit* m_launchDate = nullptr;
    QDateEdit* m_epochDate = nullptr;
    QGroupBox* m_derived = nullptr;
    QGroupBox* m_bodyDerived = nullptr;

    QLabel* m_liftoffMass = nullptr;
    QLabel* m_liftoffThrust = nullptr;
    QLabel* m_burnTime = nullptr;
    QLabel* m_massFlow = nullptr;
    QLabel* m_thrustToWeight = nullptr;
    QLabel* m_idealDeltaV = nullptr;
    QLabel* m_rotationBoost = nullptr;

    QLabel* m_bodyOrbitKind = nullptr;
    QLabel* m_bodyPeriapsisSpeed = nullptr;
    QLabel* m_bodyFarSpeed = nullptr;
    QLabel* m_bodyAim = nullptr;
};
