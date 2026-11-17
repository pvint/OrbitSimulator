// SPDX-License-Identifier: GPL-3.0-or-later
#include "globe_view.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/earth.hpp"
#include "polyline_geometry.hpp"
#include "scene_coordinates.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr int kSliderSteps = 2000;

// Line colours: burns warm, coasting cool, the ground track yellow.
const QColor kBurnColor(0xff, 0xb3, 0x47);
const QColor kCoastColor(0x38, 0xbd, 0xf8);
const QColor kGroundTrackColor(0xfa, 0xcc, 0x15);

const QColor kGhostColor(0x94, 0xa3, 0xb8);

/// The ground track floats this far above the surface so it isn't hidden by it.
constexpr double kGroundTrackLift = 3000.0; // m

/// How many recent flights stay visible as ghosts, and how often a new ghost
/// may be added while a slider is dragged. Without the limit, dragging would
/// fill the history with near-identical paths within a second.
constexpr std::size_t kMaxGhosts = 8;
constexpr qint64 kMinGhostIntervalMs = 150;

QList<QVector3D> trajectoryPoints(const orbit::SimResult& result) {
    QList<QVector3D> points;
    points.reserve(static_cast<qsizetype>(result.samples.size()));
    for (const orbit::Sample& s : result.samples) points.append(scene::fromPhysics(s.state.r));
    return points;
}

QString formatMissionTime(double seconds) {
    const auto total = static_cast<long long>(std::floor(seconds));
    return QStringLiteral("T+%1:%2:%3")
        .arg(total / 3600, 2, 10, QLatin1Char('0'))
        .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

GlobeView::GlobeView(QWidget* parent) : QWidget(parent), m_view(new QQuickWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/OrbitSim/qml/GlobeScene.qml")));
    layout->addWidget(m_view, 1);
    buildPlaybackBar(layout);
    connectScene();

    m_timer.setInterval(16); // ~60 frames per second
    connect(&m_timer, &QTimer::timeout, this, &GlobeView::onTick);
}

void GlobeView::buildPlaybackBar(QBoxLayout* layout) {
    auto* bar = new QWidget(this);
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(6, 4, 6, 4);

    m_playButton = new QToolButton(bar);
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playButton->setToolTip(tr("Play / pause (Space)"));
    m_playButton->setShortcut(Qt::Key_Space);
    connect(m_playButton, &QToolButton::clicked, this, &GlobeView::togglePlaying);

    m_timeSlider = new QSlider(Qt::Horizontal, bar);
    m_timeSlider->setRange(0, kSliderSteps);
    m_timeSlider->setToolTip(tr("Time since liftoff"));
    connect(m_timeSlider, &QSlider::sliderMoved, this, [this](int step) {
        if (m_result) setPlaybackTime(m_result->duration * step / kSliderSteps);
    });

    m_timeLabel = new QLabel(formatMissionTime(0.0), bar);
    m_timeLabel->setMinimumWidth(
        m_timeLabel->fontMetrics().horizontalAdvance(QStringLiteral("T+00:00:00")) + 8);

    m_speed = new QComboBox(bar);
    m_speed->setToolTip(tr("Playback speed: simulated seconds per real second"));
    for (const int factor : {1, 10, 60, 300, 1000, 5000}) {
        m_speed->addItem(QStringLiteral("%1×").arg(factor), factor);
    }
    m_speed->setCurrentIndex(2); // 60×: a low orbit takes about 1.5 minutes

    m_follow = new QCheckBox(tr("Follow rocket"), bar);
    connect(m_follow, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_scene) return;
        m_scene->setProperty("followRocket", on);
        m_scene->setProperty("distance", on ? 2000.0 : 4.0 * orbit::kRadiusEarth / 1000.0);
    });

    auto* reset = new QToolButton(bar);
    reset->setText(tr("Reset view"));
    reset->setToolTip(tr("Look at the launch site from a few Earth radii away"));
    connect(reset, &QToolButton::clicked, this, &GlobeView::resetView);

    row->addWidget(m_playButton);
    row->addWidget(m_timeSlider, 1);
    row->addWidget(m_timeLabel);
    row->addWidget(m_speed);
    auto* clear = new QToolButton(bar);
    clear->setText(tr("Clear ghosts"));
    clear->setToolTip(tr("Remove the faded paths of earlier flights"));
    connect(clear, &QToolButton::clicked, this, &GlobeView::clearGhosts);

    row->addWidget(m_follow);
    row->addWidget(clear);
    row->addWidget(reset);
    layout->addWidget(bar);
}

/// Find the two line geometries in the QML scene.
void GlobeView::connectScene() {
    m_scene = m_view->rootObject();
    if (!m_scene) {
        for (const QQmlError& error : m_view->errors()) qWarning().noquote() << error.toString();
        return;
    }
    m_trajectory =
        qobject_cast<PolylineGeometry*>(m_scene->property("trajectory").value<QObject*>());
    m_groundTrack =
        qobject_cast<PolylineGeometry*>(m_scene->property("groundTrack").value<QObject*>());
    if (!m_trajectory || !m_groundTrack) {
        qWarning() << "GlobeScene.qml: trajectory or ground track geometry not found";
    }

    const int count = m_scene->property("extraPathCount").toInt();
    for (int i = 0; i < count; ++i) {
        QVariant model;
        QMetaObject::invokeMethod(m_scene, "extraPath", Q_RETURN_ARG(QVariant, model),
                                  Q_ARG(QVariant, i));
        auto* object = model.value<QObject*>();
        auto* line =
            object ? qobject_cast<PolylineGeometry*>(object->property("line").value<QObject*>())
                   : nullptr;
        if (!line) break;
        m_extraPaths.push_back({object, line});
    }
}

void GlobeView::showRunSet(std::shared_ptr<const RunSet> runs, int selected) {
    if (!runs || runs->runs.empty()) return;
    const bool first = !m_result;

    // Ghosts: when one single flight replaces another, keep the old one.
    if (runs->isSweep()) {
        m_ghosts.clear();
    } else if (m_result && m_runs && !m_runs->isSweep() &&
               (!m_sinceLastGhost.isValid() || m_sinceLastGhost.elapsed() >= kMinGhostIntervalMs)) {
        m_ghosts.push_front(m_result);
        if (m_ghosts.size() > kMaxGhosts) m_ghosts.pop_back();
        m_sinceLastGhost.start();
    }

    m_runs = std::move(runs);
    m_selected = std::clamp(selected, 0, static_cast<int>(m_runs->runs.size()) - 1);
    m_result = m_runs->runs[static_cast<std::size_t>(m_selected)].result;

    updateSelectedLines();
    updateExtraPaths();
    // Keep the playback time while the user tweaks parameters, clamped to
    // the new flight.
    setPlaybackTime(std::min(m_time, m_result->duration));
    if (first) resetView();
}

void GlobeView::clearGhosts() {
    m_ghosts.clear();
    updateExtraPaths();
}

/// The selected flight: trajectory (inertial frame) and ground track
/// (Earth-fixed frame).
void GlobeView::updateSelectedLines() {
    if (!m_trajectory || !m_groundTrack || !m_result) return;
    const auto& samples = m_result->samples;
    const double theta0 = m_result->mission.earthRotationAngleAtLaunch;

    // Single flights: burns orange, coasting blue. In a sweep the selected
    // flight keeps its sweep colour (lighter where the engine burns) and is
    // drawn thicker than the others.
    const bool sweep = m_runs->isSweep();
    const QColor runColor = m_runs->runs[static_cast<std::size_t>(m_selected)].color;
    const QColor burnColor = sweep ? runColor.lighter(150) : kBurnColor;
    const QColor coastColor = sweep ? runColor : kCoastColor;
    m_scene->setProperty("trajectoryWidth", sweep ? 3.0 : 2.0);

    const QList<QVector3D> path = trajectoryPoints(*m_result);
    QList<QColor> pathColors;
    QList<QVector3D> track;
    pathColors.reserve(path.size());
    track.reserve(path.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const orbit::Sample& s = samples[i];
        // A segment's colour is set by its first point, so colour each point
        // by the stretch leading up to it.
        const bool burning = i > 0 ? samples[i - 1].burning : s.burning;
        pathColors.append(burning ? burnColor : coastColor);

        const orbit::Vec3 ecef =
            orbit::eciToEcef(s.state.r, orbit::earthRotationAngle(s.t, theta0));
        track.append(
            scene::fromPhysics((orbit::kRadiusEarth + kGroundTrackLift) * orbit::normalized(ecef)));
    }
    m_trajectory->setPoints(path, pathColors);
    m_groundTrack->setPoints(track, QList<QColor>(track.size(), kGroundTrackColor));

    const orbit::LaunchSite& site = m_result->mission.site;
    m_scene->setProperty("launchSitePosition",
                         scene::fromPhysics(orbit::positionFromLatLon(site.latitude, site.longitude,
                                                                      orbit::kRadiusEarth)));
}

/// The other flights of a sweep, or the ghosts of recent single flights.
void GlobeView::updateExtraPaths() {
    std::size_t used = 0;
    const auto show = [&](const orbit::SimResult& result, const QColor& color, double opacity) {
        if (used >= m_extraPaths.size()) return;
        const ExtraPath& path = m_extraPaths[used++];
        const QList<QVector3D> points = trajectoryPoints(result);
        path.line->setPoints(points, QList<QColor>(points.size(), color));
        path.model->setProperty("lineOpacity", opacity);
        path.model->setProperty("visible", true);
    };

    if (m_runs && m_runs->isSweep()) {
        for (std::size_t i = 0; i < m_runs->runs.size(); ++i) {
            if (std::cmp_equal(i, m_selected)) continue;
            show(*m_runs->runs[i].result, m_runs->runs[i].color, 0.6);
        }
    } else {
        for (std::size_t i = 0; i < m_ghosts.size(); ++i) {
            // Newest ghost faintly visible, older ones fading out.
            const double age = static_cast<double>(i) / static_cast<double>(kMaxGhosts);
            show(*m_ghosts[i], kGhostColor, 0.5 * (1.0 - age));
        }
    }
    for (std::size_t i = used; i < m_extraPaths.size(); ++i) {
        m_extraPaths[i].model->setProperty("visible", false);
    }
}

void GlobeView::setPlaybackTime(double t) {
    if (!m_result || !m_scene) return;
    m_time = std::clamp(t, 0.0, m_result->duration);

    const orbit::State state = orbit::stateAt(*m_result, m_time);
    const double theta =
        orbit::earthRotationAngle(m_time, m_result->mission.earthRotationAngleAtLaunch);
    m_scene->setProperty("rocketPosition", scene::fromPhysics(state.r));
    m_scene->setProperty("earthRotation", orbit::radToDeg(theta));

    m_timeLabel->setText(formatMissionTime(m_time));
    if (!m_timeSlider->isSliderDown() && m_result->duration > 0.0) {
        m_timeSlider->setValue(
            static_cast<int>(std::lround(m_time / m_result->duration * kSliderSteps)));
    }
}

void GlobeView::togglePlaying() {
    if (m_timer.isActive()) {
        m_timer.stop();
        m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        return;
    }
    if (!m_result) return;
    if (m_time >= m_result->duration) setPlaybackTime(0.0); // replay from the start
    m_clock.start();
    m_timer.start();
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
}

void GlobeView::onTick() {
    // Advance by real elapsed time × speed, so playback runs at the same rate
    // however busy the computer is.
    const double realSeconds = static_cast<double>(m_clock.restart()) / 1000.0;
    const double speed = m_speed->currentData().toDouble();
    setPlaybackTime(m_time + realSeconds * speed);
    if (m_result && m_time >= m_result->duration) togglePlaying(); // stop at the end
}

void GlobeView::resetView() {
    if (!m_scene || !m_result) return;
    // The launch site, where it is at the current playback time.
    const orbit::LaunchSite& site = m_result->mission.site;
    const double theta =
        orbit::earthRotationAngle(m_time, m_result->mission.earthRotationAngleAtLaunch);
    const orbit::Vec3 siteEci = orbit::rotateZ(
        orbit::positionFromLatLon(site.latitude, site.longitude, orbit::kRadiusEarth), theta);
    m_follow->setChecked(false);
    QMetaObject::invokeMethod(m_scene, "lookAt",
                              Q_ARG(QVariant, QVariant::fromValue(scene::fromPhysics(siteEci))));
}
