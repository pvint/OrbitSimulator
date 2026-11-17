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

/// Altitude, speed and mass against time, drawn with Qt Graphs (a QML scene
/// in a QQuickWidget, see qml/Plots.qml). The selected flight is drawn
/// bold; in a sweep, the other flights appear faintly behind it.
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
        std::function<double(const orbit::Sample&)> value;
    };

    void connectGraphs();
    void refresh();

    QCheckBox* m_poweredOnly = nullptr;
    QQuickWidget* m_view = nullptr;
    std::array<Graph, 3> m_graphs;
    bool m_graphsFound = false;
    std::shared_ptr<const RunSet> m_runs;
    int m_selected = 0;
};
