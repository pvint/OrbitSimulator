// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/demo.hpp"
#include "orbit/core/simulation.hpp"
#include "run_set.hpp"

#include <QElapsedTimer>
#include <QTimer>
#include <QVector3D>
#include <QWidget>

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

class PolylineGeometry;
class QBoxLayout;
class QComboBox;
class QLabel;
class QQuickItem;
class QQuickWidget;
class QSlider;
class QStackedWidget;
class QToolButton;

/// The 3D view (qml/GlobeScene.qml, or for a natural body going round the
/// Sun, the solar system in qml/SolarScene.qml) plus a playback bar
/// underneath: play/pause, a time slider, playback speed, and what the camera
/// circles.
///
/// It shows the selected flight in full (trajectory, ground track, playback),
/// and alongside it either the other flights of a sweep in their sweep
/// colours, or, for single flights, fading "ghosts" of the last few flights,
/// so you can see how a change moved the path.
///
/// During playback the rocket moves along its path and the Earth turns at
/// its real rate (times the playback speed), so you can watch the ground
/// track drift west orbit after orbit.
class GlobeView : public QWidget {
    Q_OBJECT

public:
    explicit GlobeView(QWidget* parent = nullptr);

    void showRunSet(std::shared_ptr<const RunSet> runs, int selected);
    void clearGhosts();

    /// Start from a fresh view (and playback speed) when the next result
    /// arrives, e.g. after opening a scenario.
    void resetViewOnNextResult();

    /// Demo tour: when the next result arrives, set up the camera and
    /// playback as the step asks, and start playing.
    void applyDemoStepOnNextResult(const orbit::DemoStep& step);
    void showCaption(const QString& title, const QString& text, int step, int count);
    void hideCaption();

signals:
    // The demo caption's buttons.
    void demoPreviousRequested();
    void demoNextRequested();
    void demoEndRequested();

private:
    /// One model from the scene's pool of extra paths.
    struct ExtraPath {
        QObject* model = nullptr;
        PolylineGeometry* line = nullptr;
    };
    /// One of the two QML scenes, and the parts of it C++ fills in.
    struct Scene {
        QQuickWidget* widget = nullptr;
        QQuickItem* root = nullptr;
        PolylineGeometry* trajectory = nullptr;
        std::vector<ExtraPath> extraPaths;
    };

    void buildPlaybackBar(QBoxLayout* layout);
    void connectScene();
    void connectSolarScene();
    static void findSceneParts(Scene& scene);
    /// Show the Earth's scene or the solar system's, as the result needs.
    void useSolarScene(bool solar);
    /// Physics position (m) → scene coordinates of the scene in use.
    QVector3D toScene(const orbit::Vec3& r) const;
    void updateSolarLines();
    void updatePlanetOrbits();
    void setSolarPlaybackTime(double epoch);
    void resetSolarView();
    double solarViewDistance() const;
    void updateSelectedLines();
    void updateExtraPaths();
    void updateMoonOrbit();
    double farthestDistance() const;
    /// How far away to look from to see the whole flight, in the scene's
    /// units (km in the Earth's scene, millions of km in the solar one).
    double viewDistance() const;
    double viewFromAboveDistance() const;
    orbit::Vec3 orbitNormalAt(double t) const;
    void lookAtOrbit(bool edgeOn, double t, double distanceKm);
    void lookFromNorth(double distanceKm);
    void setPlaybackTime(double t);
    void togglePlaying();
    void onTick();
    void resetView();
    void resetBodyView();
    void applyDemoStep(const orbit::DemoStep& step);
    void play();

    QStackedWidget* m_stack = nullptr;
    Scene m_globe;
    Scene m_solarSystem;
    bool m_solar = false; ///< showing the solar system
    // The scene in use (m_globe's or m_solarSystem's parts).
    QQuickItem* m_scene = nullptr;
    PolylineGeometry* m_trajectory = nullptr;
    std::vector<ExtraPath>* m_extraPaths = nullptr;

    PolylineGeometry* m_groundTrack = nullptr;
    PolylineGeometry* m_moonOrbit = nullptr;
    double m_moonOrbitEpoch = -1e30; ///< launch epoch the Moon's path was drawn for
    std::array<PolylineGeometry*, 8> m_planetOrbits{};
    double m_planetOrbitEpoch = -1e30; ///< epoch the planets' orbits were drawn for

    QToolButton* m_playButton = nullptr;
    QSlider* m_timeSlider = nullptr;
    QLabel* m_timeLabel = nullptr;
    QComboBox* m_speed = nullptr;
    QComboBox* m_centre = nullptr;

    std::shared_ptr<const RunSet> m_runs;
    int m_selected = 0;
    std::shared_ptr<const orbit::SimResult> m_result; ///< the selected flight

    /// Recent single flights, newest first.
    std::deque<std::shared_ptr<const orbit::SimResult>> m_ghosts;
    QElapsedTimer m_sinceLastGhost;

    double m_time = 0.0; ///< playback time, s after liftoff
    bool m_resetPending = false;
    std::optional<orbit::DemoStep> m_pendingDemoStep;
    QTimer m_timer;
    QElapsedTimer m_clock;
};
