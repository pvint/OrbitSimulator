// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/simulation.hpp"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <memory>

class PolylineGeometry;
class QBoxLayout;
class QCheckBox;
class QComboBox;
class QLabel;
class QQuickItem;
class QQuickWidget;
class QSlider;
class QToolButton;

/// The 3D view (qml/GlobeScene.qml) plus a playback bar underneath:
/// play/pause, a time slider, playback speed, and "follow rocket".
///
/// During playback the rocket moves along its path and the Earth turns at
/// its real rate (times the playback speed), so you can watch the ground
/// track drift west orbit after orbit.
class GlobeView : public QWidget {
    Q_OBJECT

public:
    explicit GlobeView(QWidget* parent = nullptr);

    void showResult(std::shared_ptr<const orbit::SimResult> result);

private:
    void buildPlaybackBar(QBoxLayout* layout);
    void connectScene();
    void updateLines();
    void setPlaybackTime(double t);
    void togglePlaying();
    void onTick();
    void resetView();

    QQuickWidget* m_view = nullptr;
    QQuickItem* m_scene = nullptr;
    PolylineGeometry* m_trajectory = nullptr;
    PolylineGeometry* m_groundTrack = nullptr;

    QToolButton* m_playButton = nullptr;
    QSlider* m_timeSlider = nullptr;
    QLabel* m_timeLabel = nullptr;
    QComboBox* m_speed = nullptr;
    QCheckBox* m_follow = nullptr;

    std::shared_ptr<const orbit::SimResult> m_result;
    double m_time = 0.0; ///< playback time, s after liftoff
    QTimer m_timer;
    QElapsedTimer m_clock;
};
