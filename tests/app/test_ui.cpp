// SPDX-License-Identifier: GPL-3.0-or-later
// The app's panels and simulation controller, driven the way a user would.

#include "flight_stats_panel.hpp"
#include "orbit/core/angles.hpp"
#include "parameter_panel.hpp"
#include "scene_coordinates.hpp"
#include "simulation_controller.hpp"
#include "sweep_panel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>

#include <cmath>
#include <memory>

Q_DECLARE_METATYPE(std::shared_ptr<const RunSet>)
Q_DECLARE_METATYPE(orbit::SweepSpec)

class UiTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<std::shared_ptr<const RunSet>>(); }

    static std::shared_ptr<const RunSet> runsAt(const QSignalSpy& spy, qsizetype i) {
        return spy.at(i).at(0).value<std::shared_ptr<const RunSet>>();
    }

    void controllerSimulatesTheDefaultMission() {
        SimulationController controller;
        QSignalSpy spy(&controller, &SimulationController::resultReady);
        controller.run(orbit::defaultMission());

        QVERIFY(spy.wait(5000));
        const auto runs = runsAt(spy, 0);
        QVERIFY(runs && !runs->isSweep());
        QCOMPARE(runs->runs.size(), std::size_t{1});
        // QVERIFY, not QCOMPARE: Qt Test would pick up orbit::toString(Outcome) by ADL.
        QVERIFY(runs->runs.front().result->outcome == orbit::Outcome::Orbit);
        QVERIFY(spy.at(0).at(1).toDouble() >= 0.0);
    }

    // Changing the mission while a run is going skips the requests in between:
    // the running one finishes, then only the newest one runs.
    void controllerLatestWins() {
        SimulationController controller;
        QSignalSpy spy(&controller, &SimulationController::resultReady);

        orbit::Mission m = orbit::defaultMission();
        for (int kick = 10; kick <= 30; kick += 5) {
            m.guidance.kickAngle = orbit::degToRad(kick);
            controller.run(m);
        }
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 5000);
        QTest::qWait(200); // and nothing more arrives
        QCOMPARE(spy.count(), 2);

        const auto& first = *runsAt(spy, 0)->runs.front().result;
        const auto& last = *runsAt(spy, 1)->runs.front().result;
        QCOMPARE(orbit::radToDeg(first.mission.guidance.kickAngle), 10.0);
        QCOMPARE(orbit::radToDeg(last.mission.guidance.kickAngle), 30.0);
    }

    void controllerRunsSweepsInOrderWithColours() {
        SimulationController controller;
        QSignalSpy spy(&controller, &SimulationController::resultReady);
        controller.run(orbit::defaultMission(), orbit::SweepSpec{"kick-angle", 10.0, 40.0, 7});

        QVERIFY(spy.wait(10000));
        const auto runs = runsAt(spy, 0);
        QVERIFY(runs->isSweep());
        QCOMPARE(runs->runs.size(), std::size_t{7});
        for (std::size_t i = 0; i < runs->runs.size(); ++i) {
            const FlightRun& run = runs->runs[i];
            QCOMPARE(run.value, 10.0 + 5.0 * static_cast<double>(i));
            QCOMPARE(orbit::radToDeg(run.result->mission.guidance.kickAngle), run.value);
        }
        // The two ends of the sweep get clearly different colours.
        QVERIFY(runs->runs.front().color != runs->runs.back().color);
    }

    void controllerReportsAnInvalidSweepAsEmpty() {
        SimulationController controller;
        QSignalSpy spy(&controller, &SimulationController::resultReady);
        controller.run(orbit::defaultMission(), orbit::SweepSpec{"no-such-thing", 0.0, 1.0, 3});
        QVERIFY(spy.wait(5000));
        QVERIFY(runsAt(spy, 0)->runs.empty());
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

    void flightPlanEnablesTheParametersThatApply() {
        ParameterPanel panel;
        auto* plan = panel.findChild<QComboBox*>(QStringLiteral("flightPlan"));
        auto* target = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-target-altitude"));
        auto* tli = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-tli-apoapsis"));
        auto* aim = panel.findChild<QPushButton*>(QStringLiteral("aimAtMoon"));
        QVERIFY(plan && target && tli && aim);
        QVERIFY(target->isEnabled());
        QVERIFY(!tli->isEnabled()); // lunar settings are off for a plain orbit
        QVERIFY(!aim->isEnabled());

        plan->setCurrentIndex(plan->findData(static_cast<int>(orbit::FlightPlan::BurnToDepletion)));
        QVERIFY(panel.mission().plan == orbit::FlightPlan::BurnToDepletion);
        QVERIFY(!target->isEnabled());

        plan->setCurrentIndex(plan->findData(static_cast<int>(orbit::FlightPlan::LunarOrbit)));
        QVERIFY(target->isEnabled()); // the parking orbit
        QVERIFY(tli->isEnabled());
        QVERIFY(aim->isEnabled());
    }

    void aimAtTheMoonSetsTheAzimuth() {
        ParameterPanel panel;
        QSignalSpy spy(&panel, &ParameterPanel::missionChanged);
        auto* plan = panel.findChild<QComboBox*>(QStringLiteral("flightPlan"));
        plan->setCurrentIndex(plan->findData(static_cast<int>(orbit::FlightPlan::LunarOrbit)));
        panel.findChild<QPushButton*>(QStringLiteral("aimAtMoon"))->click();
        // Apollo 11's launch time: about 72.5°, as Apollo 11 itself (72.0°).
        QVERIFY(std::abs(orbit::radToDeg(panel.mission().guidance.azimuth) - 72.5) < 1.0);
        QCOMPARE(spy.count(), 2); // the plan change, then the aim
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

    // Physics (z = north, metres) → scene (y = up, km) must be a rotation, not
    // a mirror image, or east and west would swap on the globe.
    void sceneMappingIsARotation() {
        const QVector3D x = scene::fromPhysics({1000.0, 0.0, 0.0});
        const QVector3D y = scene::fromPhysics({0.0, 1000.0, 0.0});
        const QVector3D z = scene::fromPhysics({0.0, 0.0, 1000.0});
        QCOMPARE(x, QVector3D(1, 0, 0));
        QCOMPARE(z, QVector3D(0, 1, 0));            // north pole is up
        QCOMPARE(QVector3D::crossProduct(x, y), z); // right-handed in, right-handed out
    }

    void sweptParameterIsLocked() {
        ParameterPanel panel;
        auto* kick = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-kick-angle"));
        auto* propellant = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-propellant"));
        panel.setLockedParameter("kick-angle");
        QVERIFY(!kick->isEnabled());
        panel.setLockedParameter("burn-time"); // burn time sets the propellant
        QVERIFY(kick->isEnabled());
        QVERIFY(!propellant->isEnabled());
        panel.setLockedParameter({});
        QVERIFY(propellant->isEnabled());
        // Derived parameters get no slider of their own.
        QVERIFY(!panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-burn-time")));
    }

    void sweepPanelRequestsTheSweepItShows() {
        SweepPanel panel;
        QSignalSpy spy(&panel, &SweepPanel::sweepRequested);
        panel.setSpec({"latitude", 0.0, 60.0, 7});
        panel.findChild<QPushButton*>(QStringLiteral("runSweep"))->click();

        QCOMPARE(spy.count(), 1);
        const auto spec = spy.at(0).at(0).value<orbit::SweepSpec>();
        QCOMPARE(QString::fromStdString(spec.parameter), QStringLiteral("latitude"));
        QCOMPARE(spec.from, 0.0);
        QCOMPARE(spec.to, 60.0);
        QCOMPARE(spec.steps, 7);
    }

    void sweepLegendListsFlightsAndReportsClicks() {
        SimulationController controller;
        QSignalSpy results(&controller, &SimulationController::resultReady);
        controller.run(orbit::defaultMission(), orbit::SweepSpec{"kick-angle", 10.0, 30.0, 5});
        QVERIFY(results.wait(10000));

        SweepPanel panel;
        QSignalSpy selected(&panel, &SweepPanel::runSelected);
        panel.showRunSet(*runsAt(results, 0), 2);
        auto* legend = panel.findChild<QTableWidget*>(QStringLiteral("sweepLegend"));
        QCOMPARE(legend->rowCount(), 5);
        QCOMPARE(legend->currentRow(), 2);
        QCOMPARE(selected.count(), 0); // showing results is not a user click

        legend->setCurrentCell(4, 1);
        QCOMPARE(selected.count(), 1);
        QCOMPARE(selected.at(0).at(0).toInt(), 4);
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
