// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/simulation.hpp"
#include "run_set.hpp"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <deque>
#include <memory>
#include <vector>

class PolylineGeometry;
class QBoxLayout;
class QComboBox;
class QLabel;
class QQuickItem;
class QQuickWidget;
class QSlider;
class QToolButton;

/// The 3D view (qml/GlobeScene.qml) plus a playback bar underneath:
/// play/pause, a time slider, playback speed, and "follow rocket".
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

private:
    /// One model from the scene's pool of extra paths.
    struct ExtraPath {
        QObject* model = nullptr;
        PolylineGeometry* line = nullptr;
    };

    void buildPlaybackBar(QBoxLayout* layout);
    void connectScene();
    void updateSelectedLines();
    void updateExtraPaths();
    void updateMoonOrbit();
    double farthestDistance() const;
    void setPlaybackTime(double t);
    void togglePlaying();
    void onTick();
    void resetView();

    QQuickWidget* m_view = nullptr;
    QQuickItem* m_scene = nullptr;
    PolylineGeometry* m_trajectory = nullptr;
    PolylineGeometry* m_groundTrack = nullptr;
    PolylineGeometry* m_moonOrbit = nullptr;
    double m_moonOrbitEpoch = -1e30; ///< launch epoch the Moon's path was drawn for
    std::vector<ExtraPath> m_extraPaths;

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
    QTimer m_timer;
    QElapsedTimer m_clock;
};
