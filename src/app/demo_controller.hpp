// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/demo.hpp"

#include <QObject>
#include <QTimer>

class GlobeView;
class MainWindow;

/// Runs a demo tour (orbit/core/demo.hpp): for each step, opens its scenario,
/// sets up the camera and playback, and shows the caption; moves on after
/// the step's time, or when the caption's buttons say so.
class DemoController : public QObject {
    Q_OBJECT

public:
    DemoController(MainWindow& window, GlobeView& globe);

    /// Start the built-in tour (demos/tour.json) at step `first` (0-based).
    /// Returns false, after telling the user why, if it can't be read.
    bool start(int first = 0);
    void stop();
    bool isRunning() const { return m_index >= 0; }

private:
    void show(int index);

    MainWindow& m_window;
    GlobeView& m_globe;
    orbit::Demo m_demo;
    int m_index = -1; ///< current step, or −1 when not running
    QTimer m_timer;
};
