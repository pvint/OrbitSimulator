// SPDX-License-Identifier: GPL-3.0-or-later
// The Lost in Space game: its session (puzzle, burns, clock, hints) and the
// burn planner panel, offscreen.

#include "burn_planner.hpp"
#include "display_format.hpp"
#include "game_session.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/lost_in_space.hpp"

#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

namespace {

constexpr double kDay = 86400.0;

orbit::Puzzle easyPuzzle() {
    return orbit::makePuzzle(orbit::Difficulty::Easy, 1,
                             orbit::epochFromCalendar({2027, 1, 1, 0, 0, 0.0}));
}

} // namespace

class GameTests : public QObject {
    Q_OBJECT

private slots:
    void aPuzzleIsPredictedStraightAway() {
        GameSession session;
        session.setPuzzle(easyPuzzle());
        session.waitForIdle();
        QVERIFY(session.prediction());
        QVERIFY(!session.prediction()->goalMet); // no burns: still lost
        QCOMPARE(session.now(), session.startEpoch());
        QVERIFY(session.prediction()->samples.size() > 10);
    }

    void plannerAddsAndEditsABurn() {
        GameSession session;
        BurnPlanner planner(&session);
        session.setPuzzle(easyPuzzle());
        session.waitForIdle();
        auto* fuel = planner.findChild<QProgressBar*>(QStringLiteral("bar-fuel"));
        auto* editor = planner.findChild<QGroupBox*>(QStringLiteral("group-burn"));
        QVERIFY(fuel && editor);
        QCOMPARE(fuel->value(), fuel->maximum()); // nothing spent yet
        QVERIFY(!editor->isEnabled());

        planner.findChild<QPushButton*>(QStringLiteral("button-add-burn"))->click();
        QCOMPARE(session.burnCount(), 1);
        QCOMPARE(session.selected(), 0);
        QVERIFY(editor->isEnabled());

        planner.findChild<QDoubleSpinBox*>(QStringLiteral("spin-burn-prograde"))->setValue(250.0);
        QCOMPARE(session.burns()[0].prograde, 250.0);
        planner.findChild<QDoubleSpinBox*>(QStringLiteral("spin-burn-days"))->setValue(30.0);
        QVERIFY(std::abs(session.burns()[0].epoch - (session.now() + 30 * kDay)) < 1.0);
        // The calendar picker shows that moment (to the minute) and sets it too.
        auto* date = planner.findChild<QDateTimeEdit*>(QStringLiteral("edit-burn-date"));
        QVERIFY(date);
        QVERIFY(std::abs(display::epochFromDateTime(date->dateTime()) - session.burns()[0].epoch) <
                60.0);
        date->setDateTime(display::dateTime(session.now() + 90 * kDay));
        QVERIFY(std::abs(session.burns()[0].epoch - (session.now() + 90 * kDay)) < 60.0);
        QCOMPARE(planner.findChild<QDoubleSpinBox*>(QStringLiteral("spin-burn-days"))->value(),
                 std::round((session.burns()[0].epoch - session.now()) / kDay * 10.0) / 10.0);
        planner.findChild<QDoubleSpinBox*>(QStringLiteral("spin-burn-days"))->setValue(30.0);
        session.waitForIdle();
        QCOMPARE(session.prediction()->burns.size(), std::size_t{1});
        QVERIFY(std::abs(session.prediction()->burns[0].deltaV - 250.0) < 1e-6);
        QVERIFY(fuel->value() < fuel->maximum());

        planner.findChild<QPushButton*>(QStringLiteral("button-remove-burn"))->click();
        QCOMPARE(session.burnCount(), 0);
    }

    void burnsBehindTheClockAreLocked() {
        GameSession session;
        session.setPuzzle(easyPuzzle());
        const int index = session.addBurn(session.now() + 10 * kDay);
        session.setBurn(index, {session.now() + 10 * kDay, 100.0, 0.0, 0.0});
        session.waitForIdle();
        session.setNow(session.now() + 20 * kDay);
        QVERIFY(session.isLocked(index));
        session.setBurn(index, {session.now() + 5 * kDay, 999.0, 0.0, 0.0});
        QCOMPARE(session.burns()[0].prograde, 100.0); // unchanged
        session.removeBurn(index);
        QCOMPARE(session.burnCount(), 1);
        // A new burn can't go into the past.
        const int later = session.addBurn(session.startEpoch());
        QCOMPARE(session.burns()[static_cast<std::size_t>(later)].epoch, session.now());
        session.restart();
        QVERIFY(!session.isLocked(index));
    }

    void theHintFindsTheWayHome() {
        GameSession session;
        BurnPlanner planner(&session);
        session.setPuzzle(easyPuzzle());
        session.waitForIdle();
        QSignalSpy hints(&session, &GameSession::hintReady);
        planner.findChild<QPushButton*>(QStringLiteral("button-hint"))->click();
        session.waitForIdle();
        QCOMPARE(hints.count(), 1);
        QCOMPARE(session.hintsUsed(), 1);
        QVERIFY(session.burnCount() >= 1);
        QVERIFY(session.prediction()->goalMet);
        // The clock stops at the end of a flight that ends.
        session.setNow(session.prediction()->endEpoch + 100 * kDay);
        QCOMPARE(session.now(), session.prediction()->endEpoch);
    }

    void hintsBringYouHomeFromJupiter() {
        // From Jupiter the path meets the Earth too fast to land: the hints
        // plan the trip, aim it, then brake near the Earth.
        GameSession session;
        session.setPuzzle(orbit::makePuzzle(orbit::Difficulty::Normal, 5,
                                            orbit::epochFromCalendar({2027, 1, 1, 0, 0, 0.0}),
                                            orbit::Region::Jupiter));
        session.waitForIdle();
        QStringList messages;
        connect(&session, &GameSession::hintReady, this,
                [&messages](const QString& m) { messages.append(m); });
        for (int i = 0; i < 4 && !session.prediction()->goalMet; ++i) {
            session.requestHint();
            session.waitForIdle();
        }
        QVERIFY2(session.prediction()->goalMet, qPrintable(messages.join(QLatin1Char('\n'))));
        QVERIFY(session.prediction()->entrySpeed < 13000.0);
        QVERIFY(messages.join(QString()).contains(QStringLiteral("heat shield")));
        QVERIFY(session.prediction()->deltaVLeft > 0.0);
    }

    void aimingAtAPlanetIsAToolNotAHint() {
        // From the asteroid belt, a burn aimed at Jupiter: the path then
        // passes it where asked, and the pass is recorded as a flyby.
        GameSession session;
        session.setPuzzle(orbit::makePuzzle(orbit::Difficulty::Easy, 3,
                                            orbit::epochFromCalendar({2027, 1, 1, 0, 0, 0.0}),
                                            orbit::Region::AsteroidBelt));
        session.waitForIdle();
        const int index = session.addBurn(session.now() + 30 * kDay);
        session.waitForIdle();
        QStringList messages;
        connect(&session, &GameSession::aimReady, this, [&messages](bool aimed, const QString& m) {
            QVERIFY(aimed);
            messages.append(m);
        });
        const double rj = orbit::bodyRadius(orbit::Body::Jupiter);
        session.aimBurn(index, {orbit::Body::Jupiter, 15.0 * rj, orbit::FlybySide::Faster});
        session.waitForIdle();
        QCOMPARE(messages.size(), 1);
        QVERIFY2(messages[0].contains(QStringLiteral("You pass Jupiter")), qPrintable(messages[0]));
        QCOMPARE(session.hintsUsed(), 0);
        const auto pass =
            orbit::passAfter(*session.prediction(), orbit::Body::Jupiter, session.burns()[0].epoch);
        QVERIFY(pass);
        QVERIFY2(std::abs(pass->distance / rj - 15.0) < 0.2,
                 qPrintable(messages[0] + QString::number(pass->distance / rj) +
                            QStringLiteral(" flybys ") +
                            QString::number(session.prediction()->flybys.size())));
        QVERIFY(!session.prediction()->flybys.empty());
    }

    void hintsTakeYouFromLowOrbitToMars() {
        // Leaving the Earth from 200 km up: the hints plan the departure at
        // the right point in the orbit, aim it, and (on Hard) brake into
        // orbit around Mars.
        GameSession session;
        session.setPuzzle(orbit::makePuzzle(
            {orbit::Difficulty::Hard, orbit::Region::Earth, true, orbit::Body::Mars}, 3,
            orbit::epochFromCalendar({2027, 1, 1, 0, 0, 0.0})));
        session.waitForIdle();
        QVERIFY(!session.prediction()->goalMet);
        QStringList messages;
        connect(&session, &GameSession::hintReady, this,
                [&messages](const QString& m) { messages.append(m); });
        for (int i = 0; i < 4 && !session.prediction()->goalMet; ++i) {
            session.requestHint();
            session.waitForIdle();
        }
        QVERIFY2(session.prediction()->goalMet, qPrintable(messages.join(QLatin1Char('\n'))));
        QCOMPARE(session.prediction()->end, orbit::VoyageEnd::OrbitingPlanet);
        QVERIFY(messages.first().contains(QStringLiteral("leaves your orbit")));
        // The departure burn's directions are relative to the Earth.
        QCOMPARE(session.prediction()->burns.front().planet, orbit::Body::Earth);
    }

    void hintsTakeYouIntoOrbitAroundTheMoon() {
        // From 200 km up to an orbit around the Moon: the transfer, timed so
        // the Moon is there on arrival, then braking at the low point.
        GameSession session;
        session.setPuzzle(orbit::makePuzzle(
            {orbit::Difficulty::Hard, orbit::Region::Earth, true, orbit::Body::Moon}, 7,
            orbit::epochFromCalendar({2027, 1, 1, 0, 0, 0.0})));
        session.waitForIdle();
        QVERIFY(!session.prediction()->goalMet);
        QStringList messages;
        connect(&session, &GameSession::hintReady, this,
                [&messages](const QString& m) { messages.append(m); });
        for (int i = 0; i < 4 && !session.prediction()->goalMet; ++i) {
            session.requestHint();
            session.waitForIdle();
        }
        QVERIFY2(session.prediction()->goalMet, qPrintable(messages.join(QLatin1Char('\n'))));
        QCOMPARE(session.prediction()->end, orbit::VoyageEnd::OrbitingPlanet);
        QVERIFY2(messages.first().contains(QStringLiteral("The Moon goes round the Earth")),
                 qPrintable(messages.first()));
        QVERIFY(session.prediction()->deltaVLeft > 0.0);
    }

    void hintsTakeYouToTitanInTwoSteps() {
        // Another planet's moon: to Saturn first, crossing Titan's orbit,
        // then a correction a month out that meets Titan itself.
        GameSession session;
        session.setPuzzle(orbit::makePuzzle(
            {orbit::Difficulty::Normal, orbit::Region::Earth, true, orbit::Body::Titan}, 3,
            orbit::epochFromCalendar({2027, 1, 1, 0, 0, 0.0})));
        session.waitForIdle();
        QStringList messages;
        connect(&session, &GameSession::hintReady, this,
                [&messages](const QString& m) { messages.append(m); });
        for (int i = 0; i < 4 && !session.prediction()->goalMet; ++i) {
            session.requestHint();
            session.waitForIdle();
        }
        QVERIFY2(session.prediction()->goalMet, qPrintable(messages.join(QLatin1Char('\n'))));
        QCOMPARE(session.prediction()->end, orbit::VoyageEnd::PassedClose);
        QVERIFY2(messages.size() >= 2, qPrintable(messages.join(QLatin1Char('\n'))));
        QVERIFY2(messages[0].contains(QStringLiteral("get to Saturn first")),
                 qPrintable(messages[0]));
        QVERIFY2(messages[1].contains(QStringLiteral("Titan goes round Saturn")),
                 qPrintable(messages[1]));
        QCOMPARE(session.burnCount(), 2);
    }
};

QTEST_MAIN(GameTests)
#include "test_game.moc"
