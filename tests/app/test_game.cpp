// SPDX-License-Identifier: GPL-3.0-or-later
// The Lost in Space game: its session (puzzle, burns, clock, hints) and the
// burn planner panel, offscreen.

#include "burn_planner.hpp"
#include "game_session.hpp"
#include "orbit/core/astro_time.hpp"

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
};

QTEST_MAIN(GameTests)
#include "test_game.moc"
