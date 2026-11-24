// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/simulation.hpp"
#include "run_set.hpp"

#include <QWidget>

#include <array>
#include <functional>
#include <memory>
#include <vector>

class QCheckBox;
class QLineSeries;
class QQuickWidget;
class QValueAxis;

/// Altitude, speed, mass and dynamic pressure against time, drawn with Qt Graphs (a QML scene
/// in a QQuickWidget, see qml/Plots.qml). The selected flight is drawn
/// bold; in a sweep, the other flights appear faintly behind it. For a
/// natural body: distance from the Earth, speed, distance from the Moon, and
/// the energy relative to the Earth (below zero: captured).
class PlotPanel : public QWidget {
    Q_OBJECT

public:
    explicit PlotPanel(QWidget* parent = nullptr);

    void showRunSet(std::shared_ptr<const RunSet> runs, int selected);

private:
    /// One graph in the QML scene and how to get its value from a sample.
    struct Graph {
        QObject* item = nullptr;            ///< the TimeGraph in the QML scene
        QObject* view = nullptr;            ///< its GraphsView, for adding series
        QLineSeries* series = nullptr;      ///< the selected flight
        std::vector<QLineSeries*> overlays; ///< the other flights of a sweep
        QColor defaultColor;
        QValueAxis* xAxis = nullptr;
        QValueAxis* yAxis = nullptr;
        using Value = std::function<double(const orbit::SimResult&, const orbit::Sample&)>;
        Value value;     ///< for a rocket
        Value bodyValue; ///< for a natural body
        QString title;
        QString bodyTitle;
        bool airborneOnly = false; ///< time axis ends when the flights leave the air
    };

    void connectGraphs();
    void refresh();

    QCheckBox* m_poweredOnly = nullptr;
    QQuickWidget* m_view = nullptr;
    std::array<Graph, 4> m_graphs;
    bool m_graphsFound = false;
    std::shared_ptr<const RunSet> m_runs;
    int m_selected = 0;
};
