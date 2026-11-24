// SPDX-License-Identifier: GPL-3.0-or-later
#include "plot_panel.hpp"

#include "orbit/core/constants.hpp"
#include "orbit/core/ephemeris.hpp"

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
#include <vector>

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

/// The time axis's unit, chosen so the numbers stay readable: seconds for
/// a launch, minutes for an orbit, hours or days for longer flights.
struct TimeUnit {
    double seconds; ///< in this unit
    const char* name;
};
TimeUnit timeUnitFor(double duration) {
    if (duration <= 1200.0) return {1.0, QT_TRANSLATE_NOOP("PlotPanel", "s")};
    if (duration <= 5.0 * 3600.0) return {60.0, QT_TRANSLATE_NOOP("PlotPanel", "min")};
    if (duration <= 72.0 * 3600.0) return {3600.0, QT_TRANSLATE_NOOP("PlotPanel", "h")};
    return {86400.0, QT_TRANSLATE_NOOP("PlotPanel", "days")};
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

    using orbit::Sample;
    using orbit::SimResult;
    m_graphs[0].value = [](const SimResult&, const Sample& s) { return s.altitude() / 1000.0; };
    m_graphs[1].value = [](const SimResult&, const Sample& s) { return s.speed(); };
    m_graphs[2].value = [](const SimResult&, const Sample& s) { return s.state.m / 1000.0; };
    m_graphs[3].value = [](const SimResult& r, const Sample& s) {
        return orbit::aerodynamicsAt(r, s).dynamicPressure / 1000.0;
    };
    m_graphs[3].airborneOnly = true;
    m_graphs[0].title = tr("Altitude (km)");
    m_graphs[1].title = tr("Speed, inertial (m/s)");
    m_graphs[2].title = tr("Mass (t)");
    m_graphs[3].title = tr("Dynamic pressure (kPa)");

    // A natural body: where it is, and whether the Earth holds on to it.
    // Distances in thousands of km, so the axis labels stay short.
    m_graphs[0].bodyValue = [](const SimResult&, const Sample& s) {
        return orbit::norm(s.state.r) / 1e6;
    };
    m_graphs[1].bodyValue = m_graphs[1].value;
    m_graphs[2].bodyValue = [](const SimResult& r, const Sample& s) {
        return orbit::norm(s.state.r - orbit::moonPosition(r.startEpoch + s.t)) / 1e6;
    };
    // ε = v²/2 − μ/r, in MJ/kg (= km²/s²).
    m_graphs[3].bodyValue = [](const SimResult&, const Sample& s) {
        return (orbit::normSquared(s.state.v) / 2.0 - orbit::kMuEarth / orbit::norm(s.state.r)) /
               1e6;
    };
    m_graphs[0].bodyTitle = tr("From the Earth (1000 km)");
    m_graphs[1].bodyTitle = m_graphs[1].title;
    m_graphs[2].bodyTitle = tr("From the Moon (1000 km)");
    m_graphs[3].bodyTitle = tr("Energy (MJ/kg; < 0: bound)");
    connectGraphs();
}

/// Find the series and axes of the graphs in the QML scene.
void PlotPanel::connectGraphs() {
    QQuickItem* root = m_view->rootObject();
    if (root == nullptr) {
        for (const QQmlError& error : m_view->errors()) qWarning().noquote() << error.toString();
        return;
    }
    const std::array<const char*, 4> names{"altitudeGraph", "speedGraph", "massGraph",
                                           "pressureGraph"};
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
    const bool body = r.mission.vehicle == orbit::Vehicle::NaturalBody;
    m_poweredOnly->setVisible(!body);

    // Time range: the selected flight, or up to its last engine event plus a margin.
    double tMax = r.duration;
    if (!body && m_poweredOnly->isChecked() && !r.events.empty()) {
        tMax = std::min(r.duration, r.events.back().sample.t * 1.05);
    }
    tMax = std::max(tMax, 1.0);

    // The dynamic pressure only matters in the air: a minute or two of a
    // flight that may last days. Its graph ends soon after the selected
    // flight was last in the air (q above 1 % of its peak).
    double tAir = 0.0;
    std::vector<double> q;
    q.reserve(r.samples.size());
    for (const orbit::Sample& s : r.samples) {
        q.push_back(orbit::aerodynamicsAt(r, s).dynamicPressure);
    }
    const double peak = std::ranges::max(q);
    for (std::size_t i = q.size(); i-- > 0;) {
        if (q[i] > 0.01 * peak) {
            tAir = r.samples[i].t;
            break;
        }
    }

    const std::size_t overlayCount = m_runs->isSweep() ? m_runs->runs.size() - 1 : 0;

    for (Graph& g : m_graphs) {
        const bool airborneOnly = g.airborneOnly && !body;
        const double tEnd = airborneOnly && tAir > 0.0 ? std::clamp(tAir * 1.2, 1.0, tMax) : tMax;
        const TimeUnit unit = timeUnitFor(tEnd);
        const double timeScale = 1.0 / unit.seconds;
        const NiceAxis xNice = niceAxis(tEnd * timeScale);
        const Graph::Value& value = body ? g.bodyValue : g.value;
        g.item->setProperty("title", body ? g.bodyTitle : g.title);

        double yMax = 0.0;
        double yMin = 0.0;
        const auto pointsFor = [&](const orbit::SimResult& result) {
            QList<QPointF> points;
            points.reserve(static_cast<qsizetype>(result.samples.size()));
            for (const orbit::Sample& s : result.samples) {
                if (s.t > tEnd) break;
                const double y = value(result, s);
                points.append({s.t * timeScale, y});
                yMax = std::max(yMax, y);
                yMin = std::min(yMin, y);
            }
            return points;
        };

        g.series->replace(pointsFor(r));
        // The dynamic pressure of a sweep's crashes and re-entries can be
        // twenty times the others' max-Q; scale to the selected flight, and
        // let the rest run off the top.
        const double selectedMax = yMax;
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

        g.xAxis->setRange(0.0, tEnd * timeScale);
        g.xAxis->setTickInterval(xNice.tick);
        g.xAxis->setLabelFormat(labelFormatFor(xNice.tick));
        g.item->setProperty("timeUnit", tr(unit.name));

        // From zero (or below, for an energy that goes negative) to a round
        // number above the top, with the same tick spacing either side.
        NiceAxis yNice = niceAxis(airborneOnly ? selectedMax * 1.2 : yMax);
        double bottom = 0.0;
        if (yMin < 0.0) {
            // Ticks spaced for the larger of the two sides.
            yNice.tick = niceAxis(std::max(yMax, -yMin)).tick;
            yNice.top = std::ceil(yMax / yNice.tick - 1e-9) * yNice.tick;
            bottom = -std::ceil(-yMin / yNice.tick - 1e-9) * yNice.tick;
        }
        g.yAxis->setRange(bottom, yNice.top);
        g.yAxis->setTickInterval(yNice.tick);
        g.yAxis->setLabelFormat(labelFormatFor(yNice.tick));
    }
}
