// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/lost_in_space.hpp"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <array>
#include <cstdint>

class BurnPlanner;
class GameSession;
class PolylineGeometry;
class QComboBox;
class QPushButton;
class QQuickItem;
class QQuickWidget;
class TimelineBar;

/// "Lost in space": a window of its own for the game. The solar system with
/// the ship's predicted path fills it; the burn planner sits on the right;
/// the clock's controls run along the bottom. Deliberately simple: the
/// physics is the full simulation, but the only things to decide are when
/// to burn and how hard.
class GameWindow : public QWidget {
    Q_OBJECT

public:
    explicit GameWindow(QWidget* parent = nullptr);

    /// Start a puzzle straight away (from the command line).
    void startPuzzle(orbit::Difficulty difficulty, std::uint64_t seed);
    GameSession* session() const { return m_session; }
    /// Show the message cards (welcome, hints, endings). Off for screenshots.
    void setCardsEnabled(bool enabled) { m_cardsEnabled = enabled; }

    // Connected by name to VoyageScene.qml's signals.
    Q_SLOT void chooseDifficulty(int index);
    Q_SLOT void acceptCard();

private:
    void buildControls(class QBoxLayout* layout);
    void connectScene();
    void onPuzzleChanged();
    void onPredictionChanged();
    void onNowChanged();
    void onTick();
    void setFlying(bool flying);
    void jumpToNextBurn();
    void resetView();
    void updatePlanetOrbits();
    void updatePath();
    void updateStartOrbit();
    void updateMarkers();
    void updateTimeline();
    void pickDate();
    void updateInfo(double now, const orbit::VoyageSample& ship);
    void updateStatus();
    void showCard(const QString& title, const QString& text, const QString& button);
    void showEnding();

    GameSession* m_session;
    BurnPlanner* m_planner;
    QQuickWidget* m_view;
    QQuickItem* m_scene = nullptr;
    PolylineGeometry* m_path = nullptr;
    PolylineGeometry* m_arrivalLine = nullptr;
    PolylineGeometry* m_startOrbit = nullptr;
    std::array<PolylineGeometry*, 8> m_planetOrbits{};

    TimelineBar* m_timeline = nullptr;
    QPushButton* m_restart = nullptr;
    QPushButton* m_fly = nullptr;
    QPushButton* m_next = nullptr;
    QPushButton* m_newGame = nullptr;
    QComboBox* m_speed = nullptr;
    QComboBox* m_centre = nullptr;

    QTimer m_timer;
    QElapsedTimer m_frameClock;
    bool m_flying = false;
    bool m_endingShown = false;
    bool m_welcomePending = false;
    bool m_cardsEnabled = true;
    /// The sample the flown part of the path reaches (to recolour only when
    /// it moves).
    std::size_t m_pathSplit = 0;
};
