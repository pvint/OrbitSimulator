// SPDX-License-Identifier: GPL-3.0-or-later
#include "plot_panel.hpp"

#include <QCheckBox>
#include <QLineSeries>
#include <QMetaObject>
#include <QPointF>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVBoxLayout>
#include <QValueAxis>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

/// A round axis maximum at or above `max`, with a round tick spacing that
/// gives about five ticks: e.g. 7,321 → top 8,000, ticks every 2,000.
struct NiceAxis {
    double top;
    double tick;
};

NiceAxis niceAxis(double max) {
    if (!(max > 0.0)) return {1.0, 0.25};
    const double rough = max / 5.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    const double f = rough / magnitude;
    double factor = 1.0;
    if (f > 5.0) {
        factor = 10.0;
    } else if (f > 2.0) {
        factor = 5.0;
    } else if (f > 1.0) {
        factor = 2.0;
    }
    const double tick = factor * magnitude;
    // The small epsilon stops a maximum of exactly 200 being pushed up to 250.
    return {std::ceil(max / tick - 1e-9) * tick, tick};
}

QString labelFormatFor(double tick) {
    if (tick >= 1.0) return QStringLiteral("%.0f");
    if (tick >= 0.1) return QStringLiteral("%.1f");
    return QStringLiteral("%.2f");
}

/// Make sure the graph has at least `count` faint overlay series.
void ensureOverlays(QObject* view, QLineSeries* selected, std::vector<QLineSeries*>& overlays,
                    std::size_t count) {
    if (overlays.size() >= count) return;
    while (overlays.size() < count) {
        auto* series = new QLineSeries(view);
        series->setWidth(1.0);
        series->setOpacity(0.45);
        QMetaObject::invokeMethod(view, "addSeries", Q_ARG(QObject*, series));
        overlays.push_back(series);
    }
    // Series are drawn in the order they were added: move the selected
    // flight's series back to the end so it stays on top.
    QMetaObject::invokeMethod(view, "removeSeries", Q_ARG(QObject*, selected));
    QMetaObject::invokeMethod(view, "addSeries", Q_ARG(QObject*, selected));
}

} // namespace

PlotPanel::PlotPanel(QWidget* parent)
    : QWidget(parent), m_poweredOnly(new QCheckBox(tr("Zoom to powered flight"), this)),
      m_view(new QQuickWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_poweredOnly->setToolTip(tr("Show only the time up to the last engine event"));
    connect(m_poweredOnly, &QCheckBox::toggled, this, &PlotPanel::refresh);
    layout->addWidget(m_poweredOnly);

    m_view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_view->setMinimumHeight(200);
    m_view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/OrbitSim/qml/Plots.qml")));
    layout->addWidget(m_view, 1);

    m_graphs[0].value = [](const orbit::Sample& s) { return s.altitude() / 1000.0; };
    m_graphs[1].value = [](const orbit::Sample& s) { return s.speed(); };
    m_graphs[2].value = [](const orbit::Sample& s) { return s.state.m / 1000.0; };
    connectGraphs();
}

/// Find the series and axes of the three graphs in the QML scene.
void PlotPanel::connectGraphs() {
    QQuickItem* root = m_view->rootObject();
    if (root == nullptr) {
        for (const QQmlError& error : m_view->errors()) qWarning().noquote() << error.toString();
        return;
    }
    const std::array<const char*, 3> names{"altitudeGraph", "speedGraph", "massGraph"};
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto* graph = root->property(names[i]).value<QObject*>();
        Graph& g = m_graphs[i];
        g.item = graph;
        g.series = graph ? qobject_cast<QLineSeries*>(graph->property("series").value<QObject*>())
                         : nullptr;
        g.xAxis =
            graph ? qobject_cast<QValueAxis*>(graph->property("xAxis").value<QObject*>()) : nullptr;
        g.yAxis =
            graph ? qobject_cast<QValueAxis*>(graph->property("yAxis").value<QObject*>()) : nullptr;
        g.view = graph ? graph->property("view").value<QObject*>() : nullptr;
        if (g.series) g.defaultColor = g.series->color();
        if (!g.series || !g.xAxis || !g.yAxis || !g.view) {
            qWarning() << "Plots.qml: graph" << names[i] << "is missing its series or axes";
            return;
        }
    }
    m_graphsFound = true;
}

void PlotPanel::showRunSet(std::shared_ptr<const RunSet> runs, int selected) {
    if (!runs || runs->runs.empty()) return;
    m_runs = std::move(runs);
    m_selected = std::clamp(selected, 0, static_cast<int>(m_runs->runs.size()) - 1);
    refresh();
}

void PlotPanel::refresh() {
    if (!m_graphsFound || !m_runs) return;
    const FlightRun& selected = m_runs->runs[static_cast<std::size_t>(m_selected)];
    const orbit::SimResult& r = *selected.result;
    if (r.samples.empty()) return;

    // Time range: the selected flight, or up to its last engine event plus a margin.
    double tMax = r.duration;
    if (m_poweredOnly->isChecked() && !r.events.empty()) {
        tMax = std::min(r.duration, r.events.back().sample.t * 1.05);
    }
    tMax = std::max(tMax, 1.0);

    // Long flights read better in minutes.
    const bool minutes = tMax > 1200.0;
    const double timeScale = minutes ? 1.0 / 60.0 : 1.0;
    const NiceAxis xNice = niceAxis(tMax * timeScale);

    const std::size_t overlayCount = m_runs->isSweep() ? m_runs->runs.size() - 1 : 0;

    for (Graph& g : m_graphs) {
        double yMax = 0.0;
        const auto pointsFor = [&](const orbit::SimResult& result) {
            QList<QPointF> points;
            points.reserve(static_cast<qsizetype>(result.samples.size()));
            for (const orbit::Sample& s : result.samples) {
                if (s.t > tMax) break;
                const double y = g.value(s);
                points.append({s.t * timeScale, y});
                yMax = std::max(yMax, y);
            }
            return points;
        };

        g.series->replace(pointsFor(r));
        g.series->setColor(m_runs->isSweep() ? selected.color : g.defaultColor);

        ensureOverlays(g.view, g.series, g.overlays, overlayCount);
        std::size_t used = 0;
        for (std::size_t i = 0; i < m_runs->runs.size() && m_runs->isSweep(); ++i) {
            if (std::cmp_equal(i, m_selected)) continue;
            QLineSeries* overlay = g.overlays[used++];
            overlay->replace(pointsFor(*m_runs->runs[i].result));
            overlay->setColor(m_runs->runs[i].color);
            overlay->setVisible(true);
        }
        for (std::size_t i = used; i < g.overlays.size(); ++i) {
            g.overlays[i]->clear();
            g.overlays[i]->setVisible(false);
        }

        g.xAxis->setRange(0.0, tMax * timeScale);
        g.xAxis->setTickInterval(xNice.tick);
        g.xAxis->setLabelFormat(labelFormatFor(xNice.tick));
        g.item->setProperty("timeUnit", minutes ? tr("min") : tr("s"));

        const NiceAxis yNice = niceAxis(yMax);
        g.yAxis->setRange(0.0, yNice.top);
        g.yAxis->setTickInterval(yNice.tick);
        g.yAxis->setLabelFormat(labelFormatFor(yNice.tick));
    }
}
