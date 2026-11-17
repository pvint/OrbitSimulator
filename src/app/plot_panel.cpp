// SPDX-License-Identifier: GPL-3.0-or-later
#include "plot_panel.hpp"

#include <QCheckBox>
#include <QLineSeries>
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
        if (!g.series || !g.xAxis || !g.yAxis) {
            qWarning() << "Plots.qml: graph" << names[i] << "is missing its series or axes";
            return;
        }
    }
    m_graphsFound = true;
}

void PlotPanel::showResult(std::shared_ptr<const orbit::SimResult> result) {
    m_result = std::move(result);
    refresh();
}

void PlotPanel::refresh() {
    if (!m_graphsFound || !m_result || m_result->samples.empty()) return;
    const orbit::SimResult& r = *m_result;

    // Time range: the whole flight, or up to the last engine event plus a margin.
    double tMax = r.duration;
    if (m_poweredOnly->isChecked() && !r.events.empty()) {
        tMax = std::min(r.duration, r.events.back().sample.t * 1.05);
    }
    tMax = std::max(tMax, 1.0);

    // Long flights read better in minutes.
    const bool minutes = tMax > 1200.0;
    const double timeScale = minutes ? 1.0 / 60.0 : 1.0;
    const NiceAxis xNice = niceAxis(tMax * timeScale);

    for (Graph& g : m_graphs) {
        QList<QPointF> points;
        points.reserve(static_cast<qsizetype>(r.samples.size()));
        double yMax = 0.0;
        for (const orbit::Sample& s : r.samples) {
            if (s.t > tMax) break;
            const double y = g.value(s);
            points.append({s.t * timeScale, y});
            yMax = std::max(yMax, y);
        }
        g.series->replace(points);

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
