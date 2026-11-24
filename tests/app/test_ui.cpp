// SPDX-License-Identifier: GPL-3.0-or-later
// The app's panels and simulation controller, driven the way a user would.

#include "flight_stats_panel.hpp"
#include "orbit/core/angles.hpp"
#include "orbit/core/ephemeris.hpp"
#include "parameter_panel.hpp"
#include "program_panel.hpp"
#include "scene_coordinates.hpp"
#include "simulation_controller.hpp"
#include "sweep_panel.hpp"
#include "wheel_guard.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>
#include <QWheelEvent>

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
        // qWaitFor rather than QTRY_COMPARE: Qt 6.8's QTRY macros narrow a
        // duration to int in their own expansion, which -Wconversion flags.
        QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 2; }, 5000));
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
        QCOMPARE(panel.mission().rocket.stages[0].propellantMass, 50000.0);
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
        // Apollo 11's launch time: within a degree or two of Apollo 11's own
        // 72.0° (the exact value depends on how this rocket climbs).
        QVERIFY(std::abs(orbit::radToDeg(panel.mission().guidance.azimuth) - 72.0) < 1.5);
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

    void statsPanelShowsDragOnlyWithAir() {
        FlightStatsPanel stats;
        auto* maxQ = stats.findChild<QLabel*>(QStringLiteral("maxQ"));
        auto* dragLoss = stats.findChild<QLabel*>(QStringLiteral("dragLoss"));
        QVERIFY(maxQ && dragLoss);

        stats.showResult(orbit::simulate(orbit::defaultMission()));
        QVERIFY(maxQ->text().contains(QStringLiteral("kPa")));
        QVERIFY(maxQ->text().contains(QStringLiteral("Mach")));
        QVERIFY(dragLoss->text().contains(QStringLiteral("m/s")));

        orbit::Mission vacuum = orbit::defaultMission();
        vacuum.rocket.dragCoefficient = 0.0;
        stats.showResult(orbit::simulate(vacuum));
        QCOMPARE(maxQ->text(), QStringLiteral("—"));
        QCOMPARE(dragLoss->text(), QStringLiteral("—"));
    }

    void stageCountShowsTheStagesInUse() {
        ParameterPanel panel;
        auto* stages = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-stages"));
        auto* stage2 = panel.findChild<QGroupBox*>(QStringLiteral("stage-group-2"));
        auto* stage3 = panel.findChild<QGroupBox*>(QStringLiteral("stage-group-3"));
        QVERIFY(stages && stage2 && stage3);
        QVERIFY(!stage2->isVisibleTo(&panel));

        stages->setValue(2.0);
        QCOMPARE(panel.mission().rocket.stageCount, 2);
        QVERIFY(stage2->isVisibleTo(&panel));
        QVERIFY(!stage3->isVisibleTo(&panel));
        stages->setValue(3.0);
        QVERIFY(stage3->isVisibleTo(&panel));
    }

    void statsPanelListsDroppedStages() {
        FlightStatsPanel stats;
        auto* list = stats.findChild<QLabel*>(QStringLiteral("stages"));
        QVERIFY(list);
        stats.showResult(orbit::simulate(orbit::defaultMission()));
        QVERIFY(!list->isVisibleTo(&stats));

        orbit::Mission twoStage = orbit::defaultMission();
        twoStage.rocket.stageCount = 2;
        stats.showResult(orbit::simulate(twoStage));
        QVERIFY(list->isVisibleTo(&stats));
        QVERIFY(list->text().contains(QStringLiteral("Stage 1 dropped")));
        QVERIFY(list->text().contains(QStringLiteral("downrange")));
    }

    void programPanelShowsThePlanUntilCustomized() {
        ProgramPanel panel;
        QSignalSpy spy(&panel, &ProgramPanel::missionChanged);
        auto* table = panel.findChild<QTableWidget*>(QStringLiteral("programTable"));
        auto* customize = panel.findChild<QPushButton*>(QStringLiteral("customizeProgram"));
        QVERIFY(table && customize);
        QCOMPARE(table->rowCount(), 4); // the Orbit plan: ascent, coast, circularize, coast
        auto* value = panel.findChild<QDoubleSpinBox*>(QStringLiteral("stepValue-1"));
        QVERIFY(value);
        QVERIFY(!value->isEnabled());    // read-only until customized
        QCOMPARE(value->value(), 200.0); // km

        customize->click();
        // The table replaces its cell widgets; the old ones go at the next
        // event loop pass, so let them go before looking for the new ones.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(spy.count(), 1);
        QVERIFY(panel.mission().plan == orbit::FlightPlan::Custom);
        QCOMPARE(panel.mission().program.size(), std::size_t{4});

        value = panel.findChild<QDoubleSpinBox*>(QStringLiteral("stepValue-1"));
        QVERIFY(value && value->isEnabled());
        value->setValue(300.0);
        QCOMPARE(panel.mission().program.front().value, 300e3);

        table->setCurrentCell(3, 0);
        panel.findChild<QPushButton*>(QStringLiteral("addStep"))->click();
        QCOMPARE(panel.mission().program.size(), std::size_t{5});
        QCOMPARE(table->rowCount(), 5);
        QVERIFY(panel.mission().program.back().end == orbit::StepEnd::Duration);
        panel.findChild<QPushButton*>(QStringLiteral("removeStep"))->click();
        QCOMPARE(panel.mission().program.size(), std::size_t{4});

        // A swept step value is locked.
        panel.setLockedKey("step-1-value");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!panel.findChild<QDoubleSpinBox*>(QStringLiteral("stepValue-1"))->isEnabled());
    }

    void sweepPanelOffersCustomStepValues() {
        SweepPanel panel;
        auto* parameter = panel.findChild<QComboBox*>(QStringLiteral("sweepParameter"));
        QVERIFY(parameter);
        QCOMPARE(parameter->findData(QStringLiteral("step-1-value")), -1);

        orbit::Mission m = orbit::defaultMission();
        m.program = orbit::flightProgramFor(m);
        m.plan = orbit::FlightPlan::Custom;
        panel.setMission(m);
        QVERIFY(parameter->findData(QStringLiteral("step-1-value")) >= 0);
        // The current choice survives the list being rebuilt.
        QCOMPARE(parameter->currentData().toString(), QStringLiteral("kick-angle"));
    }

    void dragControlsChangeTheMission() {
        ParameterPanel panel;
        QSignalSpy spy(&panel, &ParameterPanel::missionChanged);
        auto* cd = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-drag-coefficient"));
        auto* diameter = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-diameter"));
        QVERIFY(cd && diameter);
        QCOMPARE(cd->value(), 0.3);

        cd->setValue(0.0);
        diameter->setValue(4.0);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(panel.mission().rocket.dragCoefficient, 0.0);
        QCOMPARE(panel.mission().rocket.diameter, 4.0);
    }

    // Natural bodies: the panel shows the body's groups instead of the
    // rocket's, and switching how it is given converts its values.
    void vehicleChoiceShowsTheBodysParameters() {
        ParameterPanel panel;
        QSignalSpy spy(&panel, &ParameterPanel::missionChanged);
        auto* vehicle = panel.findChild<QComboBox*>(QStringLiteral("vehicle"));
        auto* given = panel.findChild<QComboBox*>(QStringLiteral("bodyGiven"));
        auto* q = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-periapsis-distance"));
        auto* x = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-body-x"));
        auto* thrust = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-thrust"));
        auto* epoch = panel.findChild<QDateEdit*>(QStringLiteral("epochDate"));
        QVERIFY(vehicle && given && q && x && thrust && epoch);
        QVERIFY(thrust->isVisibleTo(&panel));
        QVERIFY(!q->isVisibleTo(&panel));

        vehicle->setCurrentIndex(vehicle->findData(static_cast<int>(orbit::Vehicle::NaturalBody)));
        QCOMPARE(spy.count(), 1);
        QVERIFY(panel.mission().vehicle == orbit::Vehicle::NaturalBody);
        QVERIFY(!thrust->isVisibleTo(&panel));
        QVERIFY(q->isVisibleTo(&panel));
        QVERIFY(!x->isVisibleTo(&panel)); // given by elements
        QCOMPARE(q->value(), 38011.0);    // Apophis, in km
        QCOMPARE(epoch->date(), QDate(2029, 4, 13));

        // To a state vector: the same path, now as x, y, z.
        given->setCurrentIndex(given->findData(static_cast<int>(orbit::BodyGiven::StateVector)));
        QVERIFY(panel.mission().body.given == orbit::BodyGiven::StateVector);
        QVERIFY(x->isVisibleTo(&panel));
        QVERIFY(!q->isVisibleTo(&panel));
        QCOMPARE(x->value(), std::round(orbit::bodyStateJ2000(orbit::apophisFlyby()).r.x / 1000.0));

        // Reset keeps the vehicle.
        QPushButton* reset = nullptr;
        for (auto* button : panel.findChildren<QPushButton*>()) {
            if (button->text().contains(QStringLiteral("Reset"))) reset = button;
        }
        QVERIFY(reset);
        reset->click();
        QVERIFY(panel.mission().vehicle == orbit::Vehicle::NaturalBody);
        QVERIFY(panel.mission().body.given == orbit::BodyGiven::Elements);
    }

    void statsPanelShowsANaturalBodysPassage() {
        FlightStatsPanel stats;
        auto* passage = stats.findChild<QGroupBox*>(QStringLiteral("passage"));
        auto* closest = stats.findChild<QLabel*>(QStringLiteral("closestApproach"));
        auto* aim = stats.findChild<QLabel*>(QStringLiteral("impactParameter"));
        auto* maxQ = stats.findChild<QLabel*>(QStringLiteral("maxQ"));
        QVERIFY(passage && closest && aim && maxQ);

        stats.showResult(orbit::simulate(orbit::defaultMission()));
        QVERIFY(!passage->isVisibleTo(&stats));

        orbit::Mission body = orbit::defaultMission();
        body.vehicle = orbit::Vehicle::NaturalBody;
        stats.showResult(orbit::simulate(body));
        QVERIFY(passage->isVisibleTo(&stats));
        QVERIFY(!maxQ->isVisibleTo(&stats));
        QVERIFY(
            closest->text().contains(QLocale().toString(38011.0, 'f', 0) + QStringLiteral(" km")));
        QVERIFY(aim->text().contains(QStringLiteral("km")));
        auto* outcome = stats.findChild<QLabel*>(QStringLiteral("outcome"));
        QVERIFY(outcome->text().contains(QStringLiteral("Flyby")));
    }

    void programPanelIsOffForANaturalBody() {
        ProgramPanel panel;
        orbit::Mission body = orbit::defaultMission();
        body.vehicle = orbit::Vehicle::NaturalBody;
        panel.setMission(body);
        QVERIFY(!panel.isEnabled());
        panel.setMission(orbit::defaultMission());
        QVERIFY(panel.isEnabled());
    }

    // Going round the Sun: switching the centre keeps the path (the values
    // become a heliocentric state vector), and the perihelion is in au.
    void centreChoiceMovesTheBodyToTheSun() {
        ParameterPanel panel;
        QSignalSpy spy(&panel, &ParameterPanel::missionChanged);
        auto* vehicle = panel.findChild<QComboBox*>(QStringLiteral("vehicle"));
        auto* centre = panel.findChild<QComboBox*>(QStringLiteral("bodyCentre"));
        auto* given = panel.findChild<QComboBox*>(QStringLiteral("bodyGiven"));
        auto* q = panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-periapsis-distance"));
        auto* perihelion =
            panel.findChild<QDoubleSpinBox*>(QStringLiteral("spin-perihelion-distance"));
        QVERIFY(vehicle && centre && given && q && perihelion);
        vehicle->setCurrentIndex(vehicle->findData(static_cast<int>(orbit::Vehicle::NaturalBody)));
        QVERIFY(!perihelion->isVisibleTo(&panel));

        centre->setCurrentIndex(centre->findData(static_cast<int>(orbit::BodyCentre::Sun)));
        QVERIFY(panel.mission().body.centre == orbit::BodyCentre::Sun);
        QVERIFY(panel.mission().body.given == orbit::BodyGiven::StateVector);
        QCOMPARE(given->currentData().toInt(), static_cast<int>(orbit::BodyGiven::StateVector));
        // About 1 au from the Sun: Apophis is passing the Earth.
        const orbit::Vec3 r = panel.mission().body.position;
        QVERIFY(std::abs(orbit::norm(r) / orbit::kAstronomicalUnit - 1.0) < 0.02);

        // As elements around the Sun: the perihelion in au, the km one hidden.
        given->setCurrentIndex(given->findData(static_cast<int>(orbit::BodyGiven::Elements)));
        QVERIFY(perihelion->isVisibleTo(&panel));
        QVERIFY(!q->isVisibleTo(&panel));
        QVERIFY(perihelion->value() > 0.5 && perihelion->value() < 1.0);
    }

    void statsPanelShowsAnOrbitAroundTheSun() {
        FlightStatsPanel stats;
        auto* sun = stats.findChild<QGroupBox*>(QStringLiteral("aroundTheSun"));
        auto* passage = stats.findChild<QGroupBox*>(QStringLiteral("passage"));
        auto* nearestEarth = stats.findChild<QLabel*>(QStringLiteral("nearestEarth"));
        QVERIFY(sun && passage && nearestEarth);

        orbit::Mission m = orbit::defaultMission();
        m.vehicle = orbit::Vehicle::NaturalBody;
        orbit::setBodyCentre(m.body, orbit::BodyCentre::Sun);
        m.body.timeBefore = 30.0 * 86400.0;
        m.body.timeAfter = 30.0 * 86400.0;
        stats.showResult(orbit::simulate(m));
        QVERIFY(sun->isVisibleTo(&stats));
        QVERIFY(!passage->isVisibleTo(&stats));
        // Apophis's flyby, seen from the Sun: 38,011 km from the Earth.
        QVERIFY(nearestEarth->text().contains(QLocale().toString(38011.0, 'f', 0)));
    }

    // Scrolling the parameter panel over a spin box scrolls the panel; the
    // value only changes with the wheel once the box has been clicked into.
    void wheelScrollsThePanelNotTheValues() {
        WheelGuard guard;
        qApp->installEventFilter(&guard);
        QScrollArea area;
        auto* panel = new ParameterPanel;
        area.setWidget(panel);
        area.setWidgetResizable(true);
        area.resize(320, 300);
        area.show();
        QVERIFY(QTest::qWaitForWindowExposed(&area));
        auto* spin = panel->findChild<QDoubleSpinBox*>(QStringLiteral("spin-kick-angle"));
        auto* slider = panel->findChild<QSlider*>(QStringLiteral("slider-kick-angle"));
        QVERIFY(spin && slider);
        // No focus from the wheel any more: click or Tab instead.
        QCOMPARE(spin->focusPolicy(), Qt::StrongFocus);

        const auto wheelAt = [](QWidget* target) {
            const QPointF pos = QRectF(target->rect()).center();
            QWheelEvent wheel(pos, target->mapToGlobal(pos), QPoint(), QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(target, &wheel);
        };
        const double before = spin->value();
        const int scrolled = area.verticalScrollBar()->value();
        // A spin box's text field gets the wheel first.
        auto* field = spin->findChild<QLineEdit*>();
        QVERIFY(field);
        wheelAt(field);
        QCOMPARE(spin->value(), before);
        wheelAt(slider); // the slider moves the same value
        QCOMPARE(spin->value(), before);
        QVERIFY(area.verticalScrollBar()->value() > scrolled);
        QVERIFY(!spin->hasFocus());
        qApp->removeEventFilter(&guard);
    }

    void sweepPanelOffersABodysParameters() {
        SweepPanel panel;
        orbit::Mission body = orbit::defaultMission();
        body.vehicle = orbit::Vehicle::NaturalBody;
        panel.setMission(body);
        auto* parameter = panel.findChild<QComboBox*>(QStringLiteral("sweepParameter"));
        QVERIFY(parameter);
        QVERIFY(parameter->findData(QStringLiteral("periapsis-distance")) >= 0);
        QCOMPARE(parameter->findData(QStringLiteral("kick-angle")), -1);
        QVERIFY(parameter->currentIndex() >= 0); // something is chosen
    }
};

QTEST_MAIN(UiTests)
#include "test_ui.moc"
