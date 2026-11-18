// SPDX-License-Identifier: GPL-3.0-or-later
#include "globe_view.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/earth.hpp"
#include "orbit/core/ephemeris.hpp"
#include "polyline_geometry.hpp"
#include "scene_coordinates.hpp"

#include <QComboBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMetaObject>
#include <QQmlError>
#include <QQuaternion>
#include <QQuickItem>
#include <QQuickWidget>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantList>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace {

constexpr int kSliderSteps = 2000;

/// Playback speeds offered: simulated seconds per real second.
constexpr std::array kSpeeds{1, 10, 60, 300, 1000, 5000, 20000, 100000};

// Line colours: burns warm, coasting cool, the ground track yellow.
const QColor kBurnColor(0xff, 0xb3, 0x47);
const QColor kCoastColor(0x38, 0xbd, 0xf8);
const QColor kGroundTrackColor(0xfa, 0xcc, 0x15);

const QColor kGhostColor(0x94, 0xa3, 0xb8);
const QColor kSpentStageColor(0xcb, 0xd5, 0xe1); // dropped stages, falling back

/// The ground track floats this far above the surface so it isn't hidden by it.
constexpr double kGroundTrackLift = 3000.0; // m

/// How many recent flights stay visible as ghosts, and how often a new ghost
/// may be added while a slider is dragged. Without the limit, dragging would
/// fill the history with near-identical paths within a second.
constexpr std::size_t kMaxGhosts = 8;
constexpr qint64 kMinGhostIntervalMs = 150;

/// The Moon keeps one face towards the Earth. Its texture's longitude 0 is
/// the centre of that near side, and its spin axis is within 1.5° of the
/// ecliptic pole. So: local +x (longitude 0, see GlobeGeometry) towards the
/// Earth, local +y (north) towards ecliptic north, and local +z = x × y
/// completes the frame. (The small wobble called libration is left out.)
QQuaternion moonOrientation(double epoch) {
    const orbit::Vec3 toEarth = -orbit::normalized(orbit::moonPosition(epoch));
    constexpr double obliquity = 23.43928 * std::numbers::pi / 180.0;
    const orbit::Vec3 eclipticNorth{0.0, -std::sin(obliquity), std::cos(obliquity)};
    const orbit::Vec3 north =
        orbit::normalized(eclipticNorth - orbit::dot(eclipticNorth, toEarth) * toEarth);
    const QVector3D x = scene::fromPhysics(toEarth).normalized();
    const QVector3D y = scene::fromPhysics(north).normalized();
    return QQuaternion::fromAxes(x, y, QVector3D::crossProduct(x, y));
}

QList<QVector3D> trajectoryPoints(const std::vector<orbit::Sample>& samples) {
    QList<QVector3D> points;
    points.reserve(static_cast<qsizetype>(samples.size()));
    for (const orbit::Sample& s : samples) points.append(scene::fromPhysics(s.state.r));
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
    for (const int factor : kSpeeds) {
        m_speed->addItem(QStringLiteral("%1×").arg(QLocale().toString(factor)), factor);
    }
    m_speed->setCurrentIndex(2); // 60×: a low orbit takes about 1.5 minutes

    // What the camera circles. Switching also picks a sensible distance.
    m_centre = new QComboBox(bar);
    m_centre->setToolTip(tr("What the camera circles around"));
    m_centre->addItems({tr("Centre: Earth"), tr("Centre: Rocket"), tr("Centre: Moon")});
    connect(m_centre, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_scene) return;
        m_scene->setProperty("followTarget", index);
        m_scene->setProperty("panOffset", QVector3D());
        const double earthView = std::max(4.0 * orbit::kRadiusEarth, 1.4 * farthestDistance());
        const std::array<double, 3> distances{earthView, 2.0e6, 1.2e7}; // m
        m_scene->setProperty("distance", distances.at(static_cast<std::size_t>(index)) / 1000.0);
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

    row->addWidget(m_centre);
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
    m_moonOrbit = qobject_cast<PolylineGeometry*>(m_scene->property("moonOrbit").value<QObject*>());
    // The demo caption's buttons (QML signals, so connected by name).
    connect(m_scene, SIGNAL(demoPrevious()), this, SIGNAL(demoPreviousRequested()));
    connect(m_scene, SIGNAL(demoNext()), this, SIGNAL(demoNextRequested()));
    connect(m_scene, SIGNAL(demoEnd()), this, SIGNAL(demoEndRequested()));
    // The Moon's colour map is optional (see src/app/CMakeLists.txt).
    if (QFile::exists(QStringLiteral(":/textures/moon.jpg"))) {
        m_scene->setProperty("moonTexture", QStringLiteral("qrc:/textures/moon.jpg"));
    }
    if (!m_trajectory || !m_groundTrack || !m_moonOrbit) {
        qWarning() << "GlobeScene.qml: a line geometry was not found";
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
    if (first || m_resetPending) {
        resetView();
        m_resetPending = false;
    }
    if (m_pendingDemoStep) {
        applyDemoStep(*m_pendingDemoStep);
        m_pendingDemoStep.reset();
    }
}

void GlobeView::resetViewOnNextResult() { m_resetPending = true; }

void GlobeView::applyDemoStepOnNextResult(const orbit::DemoStep& step) { m_pendingDemoStep = step; }

void GlobeView::showCaption(const QString& title, const QString& text, int step, int count) {
    if (!m_scene) return;
    m_scene->setProperty("captionTitle", title);
    m_scene->setProperty("captionText", text);
    m_scene->setProperty("captionStep", QStringLiteral("%1 / %2").arg(step).arg(count));
    m_scene->setProperty("captionVisible", true);
}

void GlobeView::hideCaption() {
    if (m_scene) m_scene->setProperty("captionVisible", false);
}

/// Camera and playback for a demo step (after resetView() has fitted the
/// flight, which the "auto" view keeps).
void GlobeView::applyDemoStep(const orbit::DemoStep& step) {
    if (!m_scene || !m_result) return;
    using orbit::DemoView;
    const auto lookFrom = [this](const orbit::Vec3& direction, double distanceKm) {
        QMetaObject::invokeMethod(
            m_scene, "lookAt", Q_ARG(QVariant, QVariant::fromValue(scene::fromPhysics(direction))),
            Q_ARG(QVariant, distanceKm));
    };
    const double epoch = m_result->mission.launchEpoch + step.startTime;

    m_centre->setCurrentIndex(static_cast<int>(step.centre));
    switch (step.view) {
    case DemoView::Auto:
        if (step.distance > 0.0) m_scene->setProperty("distance", step.distance);
        break;
    case DemoView::LaunchSite: {
        const orbit::LaunchSite& site = m_result->mission.site;
        const orbit::Vec3 pad =
            orbit::rotateZ(orbit::positionFromLatLon(site.latitude, site.longitude, 1.0),
                           orbit::earthRotationAngleAt(epoch));
        lookFrom(pad, step.distance > 0.0 ? step.distance : 3.2 * orbit::kRadiusEarth / 1000.0);
        break;
    }
    case DemoView::North:
        lookFrom({0.0, 0.0, 1.0},
                 step.distance > 0.0 ? step.distance : 4.0 * orbit::kRadiusEarth / 1000.0);
        break;
    case DemoView::Moon:
        // From the Earth's side of the Moon.
        lookFrom(-orbit::moonPosition(epoch), step.distance > 0.0 ? step.distance : 30000.0);
        break;
    }
    // The centre may have set its own distance; the step's wins.
    if (step.distance > 0.0) m_scene->setProperty("distance", step.distance);

    if (step.speed > 0.0) {
        // The listed speed nearest to the one asked for.
        std::size_t best = 0;
        for (std::size_t i = 1; i < kSpeeds.size(); ++i) {
            if (std::abs(std::log(kSpeeds.at(i) / step.speed)) <
                std::abs(std::log(kSpeeds.at(best) / step.speed))) {
                best = i;
            }
        }
        m_speed->setCurrentIndex(static_cast<int>(best));
    }
    setPlaybackTime(step.startTime);
    play();
}

void GlobeView::play() {
    if (!m_timer.isActive()) togglePlaying();
}

/// Furthest the selected flight gets from the Earth's centre (m).
double GlobeView::farthestDistance() const {
    double farthest = 0.0;
    if (m_result) {
        for (const orbit::Sample& s : m_result->samples) {
            farthest = std::max(farthest, orbit::norm(s.state.r));
        }
    }
    return farthest;
}

/// The Moon's path over about a month around the launch, so its orbit is
/// visible behind any flight.
void GlobeView::updateMoonOrbit() {
    if (!m_moonOrbit || !m_result || m_result->mission.launchEpoch == m_moonOrbitEpoch) return;
    m_moonOrbitEpoch = m_result->mission.launchEpoch;
    QList<QVector3D> points;
    const QColor grey(0xb0, 0xb8, 0xc8);
    for (int hour = -24 * 3; hour <= 24 * 28; hour += 2) {
        points.append(scene::fromPhysics(orbit::moonPosition(m_moonOrbitEpoch + hour * 3600.0)));
    }
    m_moonOrbit->setPoints(points, QList<QColor>(points.size(), grey));
}

void GlobeView::clearGhosts() {
    m_ghosts.clear();
    updateExtraPaths();
}

/// The selected flight: trajectory (inertial frame) and ground track
/// (Earth-fixed frame).
void GlobeView::updateSelectedLines() {
    if (!m_trajectory || !m_groundTrack || !m_result) return;
    updateMoonOrbit();
    const auto& samples = m_result->samples;
    const double theta0 = orbit::earthRotationAngleAt(m_result->mission.launchEpoch);

    // Single flights: burns orange, coasting blue. In a sweep the selected
    // flight keeps its sweep colour (lighter where the engine burns) and is
    // drawn thicker than the others.
    const bool sweep = m_runs->isSweep();
    const QColor runColor = m_runs->runs[static_cast<std::size_t>(m_selected)].color;
    const QColor burnColor = sweep ? runColor.lighter(150) : kBurnColor;
    const QColor coastColor = sweep ? runColor : kCoastColor;
    m_scene->setProperty("trajectoryWidth", sweep ? 3.0 : 2.0);

    const QList<QVector3D> path = trajectoryPoints(m_result->samples);
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

/// The other flights of a sweep, or the ghosts of recent single flights,
/// and the selected flight's dropped stages.
void GlobeView::updateExtraPaths() {
    std::size_t used = 0;
    const auto show = [&](const std::vector<orbit::Sample>& samples, const QColor& color,
                          double opacity) {
        if (used >= m_extraPaths.size()) return;
        const ExtraPath& path = m_extraPaths[used++];
        const QList<QVector3D> points = trajectoryPoints(samples);
        path.line->setPoints(points, QList<QColor>(points.size(), color));
        path.model->setProperty("lineOpacity", opacity);
        path.model->setProperty("visible", true);
    };

    if (m_runs && m_runs->isSweep()) {
        for (std::size_t i = 0; i < m_runs->runs.size(); ++i) {
            if (std::cmp_equal(i, m_selected)) continue;
            show(m_runs->runs[i].result->samples, m_runs->runs[i].color, 0.6);
        }
    } else {
        for (std::size_t i = 0; i < m_ghosts.size(); ++i) {
            // Newest ghost faintly visible, older ones fading out.
            const double age = static_cast<double>(i) / static_cast<double>(kMaxGhosts);
            show(m_ghosts[i]->samples, kGhostColor, 0.5 * (1.0 - age));
        }
    }
    if (m_result) {
        for (const orbit::SpentStage& spent : m_result->spentStages) {
            show(spent.samples, kSpentStageColor, 0.8);
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
    const double theta = orbit::earthRotationAngle(
        m_time, orbit::earthRotationAngleAt(m_result->mission.launchEpoch));
    m_scene->setProperty("rocketPosition", scene::fromPhysics(state.r));
    // Dropped stages, from separation until they come down.
    QVariantList stages;
    for (const orbit::SpentStage& spent : m_result->spentStages) {
        if (m_time < spent.samples.front().t || m_time > spent.samples.back().t) continue;
        stages.append(
            QVariant::fromValue(scene::fromPhysics(orbit::stateAt(spent.samples, m_time).r)));
    }
    m_scene->setProperty("stagePositions", stages);
    m_scene->setProperty("earthRotation", orbit::radToDeg(theta));
    const double epoch = m_result->mission.launchEpoch + m_time;
    m_scene->setProperty("moonPosition", scene::fromPhysics(orbit::moonPosition(epoch)));
    m_scene->setProperty("moonRotation", moonOrientation(epoch));

    // Turn the light's −z axis onto the sunlight's direction of travel.
    const QVector3D towardsSun =
        scene::fromPhysics(orbit::normalized(orbit::sunPosition(epoch))).normalized();
    m_scene->setProperty("sunlightRotation",
                         QQuaternion::rotationTo(QVector3D(0.0F, 0.0F, -1.0F), -towardsSun));

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

/// A good starting view of the selected flight, and a playback speed that
/// shows all of it in about a minute and a half.
void GlobeView::resetView() {
    if (!m_scene || !m_result) return;
    m_centre->setCurrentIndex(0); // circle the Earth

    const double farthest = farthestDistance();
    if (farthest > 5.0e7) {
        // A long flight (to the Moon): look down on the plane it flies in,
        // from far enough away to see all of it. The plane comes from the
        // first sample well clear of the Earth.
        orbit::Vec3 normal{0.0, 0.0, 1.0};
        for (const orbit::Sample& s : m_result->samples) {
            if (s.altitude() > 2.0e7) {
                normal = orbit::normalized(orbit::cross(s.state.r, s.state.v));
                break;
            }
        }
        QMetaObject::invokeMethod(m_scene, "lookAt",
                                  Q_ARG(QVariant, QVariant::fromValue(scene::fromPhysics(normal))),
                                  Q_ARG(QVariant, 2.4 * farthest / 1000.0));
    } else {
        // Look at the launch site, where it is at the current playback time.
        const orbit::LaunchSite& site = m_result->mission.site;
        const double theta = orbit::earthRotationAngle(
            m_time, orbit::earthRotationAngleAt(m_result->mission.launchEpoch));
        const orbit::Vec3 siteEci = orbit::rotateZ(
            orbit::positionFromLatLon(site.latitude, site.longitude, orbit::kRadiusEarth), theta);
        QMetaObject::invokeMethod(m_scene, "lookAt",
                                  Q_ARG(QVariant, QVariant::fromValue(scene::fromPhysics(siteEci))),
                                  Q_ARG(QVariant, 3.2 * orbit::kRadiusEarth / 1000.0));
    }

    // Slowest speed that plays the whole flight within about 90 seconds.
    std::size_t index = kSpeeds.size() - 1;
    for (std::size_t i = 0; i < kSpeeds.size(); ++i) {
        if (m_result->duration / kSpeeds.at(i) <= 90.0) {
            index = i;
            break;
        }
    }
    m_speed->setCurrentIndex(static_cast<int>(index));
}
