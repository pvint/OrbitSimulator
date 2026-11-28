// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/asteroid_belt.hpp"
#include "orbit/core/lost_in_space.hpp"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

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
    void startPuzzle(const orbit::PuzzleSpec& spec, std::uint64_t seed);
    /// Start free roam in orbit around `body` (from the command line).
    void startFreeRoam(orbit::Body body);
    GameSession* session() const { return m_session; }
    /// Centre the view on a planet or a moon, as "Centre" does.
    void centreOnBody(orbit::Body body);
    /// Show the message cards (welcome, hints, endings). Off for screenshots.
    void setCardsEnabled(bool enabled) { m_cardsEnabled = enabled; }

    // Connected by name to VoyageScene.qml's signals.
    /// From the start card: where from (an orbit::Region, in orbit there
    /// or not, or around a moon: a body index, −1 for none), where to (a
    /// body index), how hard.
    Q_SLOT void chooseStart(int region, bool inOrbit, int startMoon, int destination,
                            int difficulty);
    /// From the start card: free roam around a body (index).
    Q_SLOT void chooseFreeRoam(int body);
    Q_SLOT void acceptCard(bool hideFromNowOn);
    Q_SLOT void showHiddenCards();
    /// Centre the view on what was double-clicked (index as the scene's
    /// hoverIndex: planets, the Sun, the ship, burns, flybys, moons).
    Q_SLOT void centreOn(int index);
    /// The camera zoomed: keep the scene precise near it.
    Q_SLOT void onZoomed();

protected:
    /// Arrows step the clock (see the .cpp); keys a focused control uses
    /// itself (spin boxes, sliders) never get here.
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildControls(class QBoxLayout* layout);
    void connectScene();
    void onPuzzleChanged();
    void onPredictionChanged();
    void onNowChanged();
    void onTick();
    void setFlying(bool flying);
    void jumpToNextBurn();
    void resetView(bool wholePath = false);
    /// What "Centre" offers: the Sun, you, the Earth, the destination, and
    /// any other planet double-clicked; selects `body` (or the Sun: -2,
    /// you: -1).
    void setCentreChoices(int select);
    void onCentreChanged();
    /// Follow a planet or a moon, close enough to see its sphere of influence.
    void centreOnPlanet(orbit::Body planet);
    void updatePlanetOrbits();
    void updateMoonOrbits();
    void updateBelt(double now);
    void updatePath();
    void updateStartOrbit();
    void updateMarkers();
    void updateTimeline();
    void pickDate();
    void updateInfo(double now, const orbit::VoyageSample& ship);
    QString closestPassRow(orbit::Body planet) const;
    void updateStatus();
    /// Free roam's status: what the path comes to (no goal to measure it by).
    QString freeRoamStatus(QColor& color) const;
    /// Free roam: centred on the body you start at, close enough to see
    /// your orbit around it.
    void frameFreeRoam();
    /// A message card. With a `key`, it offers "Don't show this again", and
    /// isn't shown once the player has said so (kept in the settings).
    void showCard(const QString& title, const QString& text, const QString& button,
                  const QString& key = {});
    void showEnding();
    static QString welcomeText(const orbit::Puzzle& puzzle);
    /// Where you are in orbit: how high, how long once round (and, at a
    /// moon, how it goes round its planet).
    static QString inOrbitText(const orbit::Voyage& voyage, orbit::Body home);
    static QString freeRoamWelcome(const orbit::Puzzle& puzzle);

    GameSession* m_session;
    BurnPlanner* m_planner;
    QQuickWidget* m_view;
    QQuickItem* m_scene = nullptr;
    PolylineGeometry* m_path = nullptr;
    PolylineGeometry* m_arrivalLine = nullptr;
    PolylineGeometry* m_startOrbit = nullptr;
    PolylineGeometry* m_belt = nullptr;
    std::vector<orbit::Asteroid> m_asteroids;
    double m_beltEpoch = -1e300; ///< when the belt was last drawn
    std::array<PolylineGeometry*, orbit::kAllPlanets.size()> m_planetOrbits{};
    std::array<PolylineGeometry*, orbit::kMoons.size()> m_moonOrbits{};

    TimelineBar* m_timeline;
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
    QString m_cardKey; ///< the card showing now, if it can be hidden
    /// Frame the whole path when the next current prediction comes.
    bool m_framePending = false;
    /// The sample the flown part of the path reaches (to recolour only when
    /// it moves).
    std::size_t m_pathSplit = 0;
    /// On a trip among a planet's moons, the path and its markers are drawn
    /// as seen from the planet (see drawn()); in free roam, as seen from
    /// whatever body the view is centred on.
    std::optional<orbit::Body> m_drawFrame;
    /// Where to draw a point the ship passes at `epoch` (heliocentric, m):
    /// where it is, or, with a planet as the frame, where it is relative to
    /// the planet then, around where the planet is now. A parked orbit is
    /// then a closed loop round the planet, not a smear along the planet's
    /// own path.
    orbit::Vec3 drawnPoint(const orbit::Vec3& r, double epoch) const;
    /// The same, in the scene (relative to the floating origin).
    QVector3D drawn(const orbit::Vec3& r, double epoch) const;

    // FLOATING ORIGIN (see VoyageScene.qml): scene positions are relative
    // to the point the camera follows, so a close view far from the Sun
    // keeps its precision.
    /// The point the camera follows at `now` (heliocentric, m): the Sun, the
    /// ship, or a planet or a moon.
    orbit::Vec3 followedPoint(double now) const;
    /// A heliocentric point in the scene, relative to m_origin.
    QVector3D toScene(const orbit::Vec3& r) const;
    /// How far the origin may move from a line's anchor before the line is
    /// rebuilt around a new one (m): a thousand times the camera's distance,
    /// so a float near the camera still resolves far below a pixel.
    double rebaseDistance() const;
    /// A point of the path, relative to the point the path is drawn around:
    /// the frame body now, or else m_pathAnchor.
    QVector3D pathVertex(const orbit::Vec3& r, double epoch) const;
    /// Move the scene to m_origin: the Sun, and the anchors of the planets'
    /// orbits, the belt and the path (rebuilt around new anchors if the
    /// origin is far from them).
    void placeScene(double now);
    orbit::Vec3 m_origin;       ///< heliocentric, m
    orbit::Vec3 m_staticAnchor; ///< the planets' orbits, the start orbit and the belt
    orbit::Vec3 m_pathAnchor;   ///< the path, when drawn as seen from the Sun
};
