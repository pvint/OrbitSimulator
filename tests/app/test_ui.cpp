// SPDX-License-Identifier: GPL-3.0-or-later
// The app's panels and simulation controller, driven the way a user would.

#include "flight_stats_panel.hpp"
#include "orbit/core/angles.hpp"
#include "parameter_panel.hpp"
#include "simulation_controller.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSignalSpy>
#include <QSlider>
#include <QTest>

#include <memory>

Q_DECLARE_METATYPE(std::shared_ptr<const orbit::SimResult>)

class UiTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<std::shared_ptr<const orbit::SimResult>>(); }

    void controllerSimulatesTheDefaultMission() {
        SimulationController controller;
        QSignalSpy spy(&controller, &SimulationController::resultReady);
        controller.setMission(orbit::defaultMission());

        QVERIFY(spy.wait(5000));
        const auto result = spy.at(0).at(0).value<std::shared_ptr<const orbit::SimResult>>();
        QVERIFY(result);
        // QVERIFY, not QCOMPARE: Qt Test would pick up orbit::toString(Outcome) by ADL.
        QVERIFY(result->outcome == orbit::Outcome::Orbit);
        QVERIFY(spy.at(0).at(1).toDouble() >= 0.0);
    }

    // Changing the mission while a run is going skips the missions in between:
    // the running one finishes, then only the newest one runs.
    void controllerLatestWins() {
        SimulationController controller;
        QSignalSpy spy(&controller, &SimulationController::resultReady);

        orbit::Mission m = orbit::defaultMission();
        for (int kick = 10; kick <= 30; kick += 5) {
            m.guidance.kickAngle = orbit::degToRad(kick);
            controller.setMission(m);
        }
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 5000);
        QTest::qWait(200); // and nothing more arrives
        QCOMPARE(spy.count(), 2);

        const auto first = spy.at(0).at(0).value<std::shared_ptr<const orbit::SimResult>>();
        const auto last = spy.at(1).at(0).value<std::shared_ptr<const orbit::SimResult>>();
        QCOMPARE(orbit::radToDeg(first->mission.guidance.kickAngle), 10.0);
        QCOMPARE(orbit::radToDeg(last->mission.guidance.kickAngle), 30.0);
    }

    void spinBoxEditsTheMission() {
        ParameterPanel panel;
        QSignalSpy spy(&panel, &ParameterPanel::missionChanged);
        auto* spin = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-kick-angle"));
        auto* slider = panel.findChild<QSlider*>(QStringLiteral("slider-kick-angle"));
        QVERIFY(spin && slider);

        spin->setValue(30.0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(orbit::radToDeg(panel.mission().guidance.kickAngle), 30.0);
        // The slider follows (kick angle steps are 0.25°, so 30° is step 120).
        QCOMPARE(slider->value(), 120);
    }

    void sliderEditsTheMissionInDisplayUnits() {
        ParameterPanel panel;
        auto* slider = panel.findChild<QSlider*>(QStringLiteral("slider-propellant"));
        auto* spin = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-propellant"));
        QVERIFY(slider && spin);

        slider->setValue(100); // propellant steps are 0.5 t from 0 → 50 t
        QCOMPARE(panel.mission().rocket.propellantMass, 50000.0);
        QCOMPARE(spin->value(), 50.0);
    }

    void autoCircularizeToggleDisablesTargetAltitude() {
        ParameterPanel panel;
        auto* check = panel.findChild<QCheckBox*>(QStringLiteral("autoCircularize"));
        auto* target = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-target-altitude"));
        QVERIFY(check && target);
        QVERIFY(target->isEnabled());

        check->setChecked(false);
        QVERIFY(!panel.mission().autoCircularize);
        QVERIFY(!target->isEnabled());
    }

    void setMissionUpdatesWidgetsWithoutEmitting() {
        ParameterPanel panel;
        QSignalSpy spy(&panel, &ParameterPanel::missionChanged);
        orbit::Mission m = orbit::defaultMission();
        m.site.latitude = orbit::degToRad(-12.5);
        panel.setMission(m);

        QCOMPARE(spy.count(), 0);
        QCOMPARE(panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-latitude"))->value(), -12.5);
    }

    void statsPanelShowsTheOutcome() {
        FlightStatsPanel stats;
        stats.showResult(orbit::simulate(orbit::defaultMission()));
        auto* outcome = stats.findChild<QLabel*>(QStringLiteral("outcome"));
        QVERIFY(outcome);
        QVERIFY(outcome->text().contains(QStringLiteral("Orbit")));
    }
};

QTEST_MAIN(UiTests)
#include "test_ui.moc"
