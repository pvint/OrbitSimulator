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

namespace {

constexpr int kSliderSteps = 2000;

// Line colours: burns warm, coasting cool, the ground track yellow.
const QColor kBurnColor(0xff, 0xb3, 0x47);
const QColor kCoastColor(0x38, 0xbd, 0xf8);
const QColor kGroundTrackColor(0xfa, 0xcc, 0x15);

/// The ground track floats this far above the surface so it isn't hidden by it.
constexpr double kGroundTrackLift = 3000.0; // m

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
    row->addWidget(m_follow);
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
}

void GlobeView::showResult(std::shared_ptr<const orbit::SimResult> result) {
    const bool first = !m_result;
    m_result = std::move(result);
    updateLines();
    // Keep the playback time while the user tweaks parameters, clamped to
    // the new flight.
    setPlaybackTime(std::min(m_time, m_result->duration));
    if (first) resetView();
}

/// Build the trajectory (inertial frame) and ground track (Earth-fixed frame)
/// from the samples.
void GlobeView::updateLines() {
    if (!m_trajectory || !m_groundTrack || !m_result) return;
    const auto& samples = m_result->samples;
    const double theta0 = m_result->mission.earthRotationAngleAtLaunch;

    QList<QVector3D> path;
    QList<QColor> pathColors;
    QList<QVector3D> track;
    QList<QColor> trackColors;
    path.reserve(static_cast<qsizetype>(samples.size()));
    pathColors.reserve(path.capacity());
    track.reserve(path.capacity());
    trackColors.reserve(path.capacity());

    for (std::size_t i = 0; i < samples.size(); ++i) {
        const orbit::Sample& s = samples[i];
        path.append(scene::fromPhysics(s.state.r));
        // A segment's colour is set by its first point, so colour each point
        // by the stretch leading up to it.
        const bool burning = i > 0 ? samples[i - 1].burning : s.burning;
        pathColors.append(burning ? kBurnColor : kCoastColor);

        const orbit::Vec3 ecef =
            orbit::eciToEcef(s.state.r, orbit::earthRotationAngle(s.t, theta0));
        track.append(
            scene::fromPhysics((orbit::kRadiusEarth + kGroundTrackLift) * orbit::normalized(ecef)));
        trackColors.append(kGroundTrackColor);
    }
    m_trajectory->setPoints(path, pathColors);
    m_groundTrack->setPoints(track, trackColors);

    const orbit::LaunchSite& site = m_result->mission.site;
    m_scene->setProperty("launchSitePosition",
                         scene::fromPhysics(orbit::positionFromLatLon(site.latitude, site.longitude,
                                                                      orbit::kRadiusEarth)));
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
