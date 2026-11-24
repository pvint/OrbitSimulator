// SPDX-License-Identifier: GPL-3.0-or-later
#include "globe_view.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/earth.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/orbit.hpp"
#include "orbit/core/solar_system.hpp"
#include "polyline_geometry.hpp"
#include "scene_coordinates.hpp"

#include <QComboBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMetaObject>
#include <QQmlError>
#include <QQuaternion>
#include <QQuickItem>
#include <QQuickWidget>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
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
/// (The fastest are for natural bodies, followed for months.)
/// (The fastest are for natural bodies, followed for months, or around the
/// Sun for decades: 50 million × shows Halley's 76-year orbit in 48 s.)
constexpr std::array kSpeeds{1,     10,     60,     300,     1000,     5000,
                             20000, 100000, 500000, 2000000, 10000000, 50000000};

// Line colours: burns warm, coasting cool, the ground track yellow.
const QColor kBurnColor(0xff, 0xb3, 0x47);
const QColor kCoastColor(0x38, 0xbd, 0xf8);
const QColor kGroundTrackColor(0xfa, 0xcc, 0x15);

const QColor kGhostColor(0x94, 0xa3, 0xb8);
const QColor kSpentStageColor(0xcb, 0xd5, 0xe1); // dropped stages, falling back

/// The ground track floats this far above the surface so it isn't hidden by it.
constexpr double kGroundTrackLift = 3000.0; // m

/// A natural body's ground track is drawn only while it is this close to
/// the Earth's centre (m). Further out the Earth turns beneath a body that
/// hardly moves, and the track would just wind round and round the globe.
constexpr double kBodyGroundTrackRange = 1.0e8;

const QColor kBodyColor(0xd6, 0xc7, 0xa1); // a natural body: rock

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

/// A path's points in scene coordinates: the Earth's scene (km) or the
/// solar system's (millions of km).
QList<QVector3D> trajectoryPoints(const std::vector<orbit::Sample>& samples, bool solar) {
    QList<QVector3D> points;
    points.reserve(static_cast<qsizetype>(samples.size()));
    for (const orbit::Sample& s : samples) {
        points.append(solar ? scene::fromHeliocentric(s.state.r) : scene::fromPhysics(s.state.r));
    }
    return points;
}

bool isSolar(const orbit::SimResult& result) {
    return result.frame == orbit::ResultFrame::SunEclipticJ2000;
}

/// Planet colours, as in SolarScene.qml (for their orbits).
const std::array<QColor, 8> kPlanetColors{QColor(0xa8, 0xa2, 0x9e), QColor(0xe9, 0xc4, 0x6a),
                                          QColor(0x4f, 0x9d, 0xe0), QColor(0xd9, 0x60, 0x3b),
                                          QColor(0xd8, 0xa6, 0x6c), QColor(0xe3, 0xc9, 0x8f),
                                          QColor(0x8f, 0xd3, 0xe0), QColor(0x5b, 0x7f, 0xe8)};

QString formatMissionTime(double seconds) {
    const auto total = static_cast<long long>(std::floor(seconds));
    return QStringLiteral("T+%1:%2:%3")
        .arg(total / 3600, 2, 10, QLatin1Char('0'))
        .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

/// A natural body's time is a date: "2029-04-13 21:45 UTC".
QString formatUtc(double epoch) {
    // The start of the minute, plus a second so rounding inside the calendar
    // conversion can't tip it back into the minute before.
    const orbit::CalendarTime t = orbit::calendarFromEpoch(std::floor(epoch / 60.0) * 60.0 + 1.0);
    return QStringLiteral("%1-%2-%3 %4:%5 UTC")
        .arg(t.year)
        .arg(t.month, 2, 10, QLatin1Char('0'))
        .arg(t.day, 2, 10, QLatin1Char('0'))
        .arg(t.hour, 2, 10, QLatin1Char('0'))
        .arg(t.minute, 2, 10, QLatin1Char('0'));
}

bool isNaturalBody(const orbit::SimResult& result) {
    return result.mission.vehicle == orbit::Vehicle::NaturalBody;
}

} // namespace

GlobeView::GlobeView(QWidget* parent) : QWidget(parent), m_stack(new QStackedWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Two scenes, one shown at a time: the Earth's, and the solar system's
    // for natural bodies going round the Sun.
    for (auto [scene, file] :
         {std::pair{&m_globe, "GlobeScene.qml"}, std::pair{&m_solarSystem, "SolarScene.qml"}}) {
        scene->widget = new QQuickWidget(m_stack);
        scene->widget->setResizeMode(QQuickWidget::SizeRootObjectToView);
        scene->widget->setSource(
            QUrl(QStringLiteral("qrc:/qt/qml/OrbitSim/qml/") + QLatin1String(file)));
        m_stack->addWidget(scene->widget);
    }
    layout->addWidget(m_stack, 1);
    buildPlaybackBar(layout);
    connectScene();
    connectSolarScene();
    useSolarScene(false);

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
    m_timeLabel->setObjectName(QStringLiteral("playbackTime"));
    m_timeLabel->setMinimumWidth(
        m_timeLabel->fontMetrics().horizontalAdvance(QStringLiteral("2029-04-13 21:45 UTC")) + 8);

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
        // Earth / rocket / Moon: m; Sun / body / Earth: millions of km.
        const double earthView = std::max(4.0 * orbit::kRadiusEarth, 1.4 * farthestDistance());
        const std::array<double, 3> distances =
            m_solar ? std::array<double, 3>{solarViewDistance(), 20.0, 5.0}
                    : std::array<double, 3>{earthView / 1000.0, 2.0e3, 1.2e4};
        m_scene->setProperty("distance", distances.at(static_cast<std::size_t>(index)));
    });

    // Click for the starting view; the arrow offers the others. Each circles
    // the Earth.
    auto* reset = new QToolButton(bar);
    reset->setObjectName(QStringLiteral("resetView"));
    reset->setText(tr("Reset view"));
    reset->setToolTip(tr("Look at the launch site from a few Earth radii away. The arrow "
                         "offers other views."));
    reset->setPopupMode(QToolButton::MenuButtonPopup);
    connect(reset, &QToolButton::clicked, this, &GlobeView::resetView);
    auto* views = new QMenu(reset);
    views->addAction(tr("Reset view"), this, &GlobeView::resetView);
    views->addAction(tr("Edge-on to the orbit"), this, [this] {
        m_centre->setCurrentIndex(0);
        lookAtOrbit(true, m_time, 0.0);
    });
    views->addAction(tr("Above the orbit"), this, [this] {
        m_centre->setCurrentIndex(0);
        lookAtOrbit(false, m_time, 0.0);
    });
    // (In the solar system, "north" is ecliptic north: the frame's z.)
    views->addAction(tr("From the north"), this, [this] {
        m_centre->setCurrentIndex(0);
        lookFromNorth(viewFromAboveDistance());
    });
    reset->setMenu(views);

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

/// The parts every scene has: its root, the trajectory, the pool of extra
/// paths, and the demo caption's buttons (QML signals, connected by name).
void GlobeView::findSceneParts(Scene& scene) {
    scene.root = scene.widget->rootObject();
    if (!scene.root) {
        for (const QQmlError& error : scene.widget->errors()) {
            qWarning().noquote() << error.toString();
        }
        return;
    }
    scene.trajectory =
        qobject_cast<PolylineGeometry*>(scene.root->property("trajectory").value<QObject*>());
    const int count = scene.root->property("extraPathCount").toInt();
    for (int i = 0; i < count; ++i) {
        QVariant model;
        QMetaObject::invokeMethod(scene.root, "extraPath", Q_RETURN_ARG(QVariant, model),
                                  Q_ARG(QVariant, i));
        auto* object = model.value<QObject*>();
        auto* line =
            object ? qobject_cast<PolylineGeometry*>(object->property("line").value<QObject*>())
                   : nullptr;
        if (!line) break;
        scene.extraPaths.push_back({object, line});
    }
}

/// The Earth's scene: also the ground track, and the Moon's orbit.
void GlobeView::connectScene() {
    findSceneParts(m_globe);
    QQuickItem* root = m_globe.root;
    if (!root) return;
    m_groundTrack =
        qobject_cast<PolylineGeometry*>(root->property("groundTrack").value<QObject*>());
    m_moonOrbit = qobject_cast<PolylineGeometry*>(root->property("moonOrbit").value<QObject*>());
    connect(root, SIGNAL(demoPrevious()), this, SIGNAL(demoPreviousRequested()));
    connect(root, SIGNAL(demoNext()), this, SIGNAL(demoNextRequested()));
    connect(root, SIGNAL(demoEnd()), this, SIGNAL(demoEndRequested()));
    // The Moon's colour map is optional (see src/app/CMakeLists.txt).
    if (QFile::exists(QStringLiteral(":/textures/moon.jpg"))) {
        root->setProperty("moonTexture", QStringLiteral("qrc:/textures/moon.jpg"));
    }
    if (!m_globe.trajectory || !m_groundTrack || !m_moonOrbit) {
        qWarning() << "GlobeScene.qml: a line geometry was not found";
    }
}

/// The solar system's scene: also the planets' orbits.
void GlobeView::connectSolarScene() {
    findSceneParts(m_solarSystem);
    QQuickItem* root = m_solarSystem.root;
    if (!root) return;
    connect(root, SIGNAL(demoPrevious()), this, SIGNAL(demoPreviousRequested()));
    connect(root, SIGNAL(demoNext()), this, SIGNAL(demoNextRequested()));
    connect(root, SIGNAL(demoEnd()), this, SIGNAL(demoEndRequested()));
    for (std::size_t i = 0; i < m_planetOrbits.size(); ++i) {
        QVariant model;
        QMetaObject::invokeMethod(root, "planetOrbit", Q_RETURN_ARG(QVariant, model),
                                  Q_ARG(QVariant, static_cast<int>(i)));
        auto* object = model.value<QObject*>();
        m_planetOrbits.at(i) =
            object ? qobject_cast<PolylineGeometry*>(object->property("line").value<QObject*>())
                   : nullptr;
    }
    if (!m_solarSystem.trajectory || !m_planetOrbits.back()) {
        qWarning() << "SolarScene.qml: a line geometry was not found";
    }
}

void GlobeView::useSolarScene(bool solar) {
    m_solar = solar;
    Scene& scene = solar ? m_solarSystem : m_globe;
    m_scene = scene.root;
    m_trajectory = scene.trajectory;
    m_extraPaths = &scene.extraPaths;
    m_stack->setCurrentWidget(scene.widget);
    const QSignalBlocker block(m_centre);
    m_centre->setCurrentIndex(0);
    m_centre->setItemText(0, solar ? tr("Centre: Sun") : tr("Centre: Earth"));
    m_centre->setItemText(2, solar ? tr("Centre: Earth") : tr("Centre: Moon"));
}

QVector3D GlobeView::toScene(const orbit::Vec3& r) const {
    return m_solar ? scene::fromHeliocentric(r) : scene::fromPhysics(r);
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
    // Around the Sun: the solar system's scene; anything else: the Earth's.
    if (isSolar(*m_result) != m_solar) {
        useSolarScene(isSolar(*m_result));
        m_resetPending = true;
    }

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
    // On both scenes, so it stays when the next step switches between them.
    for (QQuickItem* root : {m_globe.root, m_solarSystem.root}) {
        if (!root) continue;
        root->setProperty("captionTitle", title);
        root->setProperty("captionText", text);
        root->setProperty("captionStep", QStringLiteral("%1 / %2").arg(step).arg(count));
        root->setProperty("captionVisible", true);
    }
}

void GlobeView::hideCaption() {
    for (QQuickItem* root : {m_globe.root, m_solarSystem.root}) {
        if (root) root->setProperty("captionVisible", false);
    }
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
    const double epoch = m_result->startEpoch + step.startTime;
    // Demo distances are in km; the solar system's scene is in millions.
    const double stepDistance = m_solar ? step.distance / 1e6 : step.distance;

    m_centre->setCurrentIndex(static_cast<int>(step.centre));
    switch (step.view) {
    case DemoView::Auto:
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
        lookFromNorth(step.distance > 0.0 ? step.distance : 4.0 * orbit::kRadiusEarth / 1000.0);
        break;
    case DemoView::Moon:
        // From the Earth's side of the Moon.
        lookFrom(-orbit::moonPosition(epoch), step.distance > 0.0 ? step.distance : 30000.0);
        break;
    case DemoView::EdgeOn:
    case DemoView::AboveOrbit:
        lookAtOrbit(step.view == DemoView::EdgeOn, step.startTime, step.distance);
        break;
    }
    // The centre may have set its own distance; the step's wins.
    if (step.distance > 0.0) m_scene->setProperty("distance", stepDistance);

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
    if (!m_moonOrbit || !m_result || m_result->startEpoch == m_moonOrbitEpoch) return;
    m_moonOrbitEpoch = m_result->startEpoch;
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
    if (m_solar) {
        updateSolarLines();
        return;
    }
    if (!m_trajectory || !m_groundTrack || !m_result) return;
    updateMoonOrbit();
    const auto& samples = m_result->samples;
    const double theta0 = orbit::earthRotationAngleAt(m_result->startEpoch);
    const bool body = isNaturalBody(*m_result);

    // Single flights: burns orange, coasting blue. In a sweep the selected
    // flight keeps its sweep colour (lighter where the engine burns) and is
    // drawn thicker than the others.
    const bool sweep = m_runs->isSweep();
    const QColor runColor = m_runs->runs[static_cast<std::size_t>(m_selected)].color;
    const QColor burnColor = sweep ? runColor.lighter(150) : kBurnColor;
    const QColor coastColor = sweep ? runColor : kCoastColor;
    m_scene->setProperty("trajectoryWidth", sweep ? 3.0 : 2.0);

    const QList<QVector3D> path = trajectoryPoints(m_result->samples, m_solar);
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

        if (body && orbit::norm(s.state.r) > kBodyGroundTrackRange) continue;
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
    m_scene->setProperty("launchSiteVisible", !body);
    m_scene->setProperty("vehicleColor", body ? kBodyColor : QColor(Qt::white));
    m_centre->setItemText(1, body ? tr("Centre: Body") : tr("Centre: Rocket"));
    m_timeSlider->setToolTip(body ? tr("Time") : tr("Time since liftoff"));
}

/// The other flights of a sweep, or the ghosts of recent single flights,
/// and the selected flight's dropped stages.
void GlobeView::updateExtraPaths() {
    std::size_t used = 0;
    const auto show = [&](const std::vector<orbit::Sample>& samples, const QColor& color,
                          double opacity) {
        if (used >= m_extraPaths->size()) return;
        const ExtraPath& path = (*m_extraPaths)[used++];
        const QList<QVector3D> points = trajectoryPoints(samples, m_solar);
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
            // Only ghosts drawn in the same frame (around the Earth, or the Sun).
            if (m_result && isSolar(*m_ghosts[i]) != isSolar(*m_result)) continue;
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
    for (std::size_t i = used; i < m_extraPaths->size(); ++i) {
        (*m_extraPaths)[i].model->setProperty("visible", false);
    }
}

void GlobeView::setPlaybackTime(double t) {
    if (!m_result || !m_scene) return;
    m_time = std::clamp(t, 0.0, m_result->duration);
    if (m_solar) {
        setSolarPlaybackTime(m_result->startEpoch + m_time);
        return;
    }

    const orbit::State state = orbit::stateAt(*m_result, m_time);
    const double theta =
        orbit::earthRotationAngle(m_time, orbit::earthRotationAngleAt(m_result->startEpoch));
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
    const double epoch = m_result->startEpoch + m_time;
    m_scene->setProperty("moonPosition", scene::fromPhysics(orbit::moonPosition(epoch)));
    m_scene->setProperty("moonRotation", moonOrientation(epoch));

    // Turn the light's −z axis onto the sunlight's direction of travel.
    const QVector3D towardsSun =
        scene::fromPhysics(orbit::normalized(orbit::sunPosition(epoch))).normalized();
    m_scene->setProperty("sunlightRotation",
                         QQuaternion::rotationTo(QVector3D(0.0F, 0.0F, -1.0F), -towardsSun));

    m_timeLabel->setText(isNaturalBody(*m_result) ? formatUtc(epoch) : formatMissionTime(m_time));
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

/// How far away to look from to see the whole flight (km): a few Earth
/// radii, or further for long flights such as to the Moon.
double GlobeView::viewDistance() const {
    if (m_solar) return solarViewDistance();
    const double farthest = farthestDistance();
    if (m_result && m_result->encounter) {
        // A natural body: close enough to see its closest approach, which is
        // what it is about; the rest runs off towards the edges.
        // One that never comes within the Moon's distance is seen whole.
        const double closest = orbit::norm(m_result->encounter->closest.state.r);
        if (closest > 4.0e8) return 1.3 * farthest / 1000.0;
        return std::clamp(4.0 * closest, 4.0 * orbit::kRadiusEarth, 2.4 * farthest) / 1000.0;
    }
    return (farthest > 5.0e7 ? 2.4 * farthest : 3.2 * orbit::kRadiusEarth) / 1000.0;
}

/// The same, but far enough to see a low orbit from above (km). From
/// closer, the near side of the Earth bulges out past a low orbit and hides
/// it: the Earth's outline is asin(R/d) across, the orbit only atan(a/d).
double GlobeView::viewFromAboveDistance() const {
    if (m_solar) return solarViewDistance();
    return std::max(viewDistance(), 6.0 * orbit::kRadiusEarth / 1000.0);
}

// --- The solar system ---------------------------------------------------------

/// Far enough (millions of km) to see the whole path, and at least the
/// Earth's orbit, so there is always something to measure it against.
double GlobeView::solarViewDistance() const {
    const double reach = std::max(farthestDistance(), 1.2 * orbit::kAstronomicalUnit);
    return 2.6 * reach / scene::kSolarMetresPerUnit;
}

/// The planets' orbits, from their elements at the middle of the flight
/// (they change too slowly to matter over a few centuries).
void GlobeView::updatePlanetOrbits() {
    const double epoch = m_result->startEpoch + 0.5 * m_result->duration;
    if (std::abs(epoch - m_planetOrbitEpoch) < 365.25 * 86400.0) return;
    m_planetOrbitEpoch = epoch;
    for (const orbit::Planet planet : orbit::kAllPlanets) {
        PolylineGeometry* line = m_planetOrbits.at(static_cast<std::size_t>(planet));
        if (!line) continue;
        const orbit::PlanetOrbit o = orbit::planetOrbit(planet, epoch);
        const double q = o.semiMajorAxis * (1.0 - o.eccentricity);
        QList<QVector3D> points;
        constexpr int kPoints = 360;
        for (int i = 0; i <= kPoints; ++i) {
            const double nu = 2.0 * std::numbers::pi * i / kPoints;
            points.append(scene::fromHeliocentric(
                orbit::stateFromPeriapsis(q, o.eccentricity, o.inclination, o.raan,
                                          o.argumentOfPeriapsis, nu, orbit::kMuSun)
                    .r));
        }
        line->setPoints(points, QList<QColor>(points.size(),
                                              kPlanetColors.at(static_cast<std::size_t>(planet))));
    }
}

/// A body going round the Sun: its path, in the sweep's colour or blue, and
/// the planets' orbits.
void GlobeView::updateSolarLines() {
    if (!m_trajectory || !m_result) return;
    updatePlanetOrbits();
    const bool sweep = m_runs->isSweep();
    const QColor color =
        sweep ? m_runs->runs[static_cast<std::size_t>(m_selected)].color : kCoastColor;
    m_scene->setProperty("trajectoryWidth", sweep ? 3.0 : 2.0);
    const QList<QVector3D> path = trajectoryPoints(m_result->samples, true);
    m_trajectory->setPoints(path, QList<QColor>(path.size(), color));
    m_scene->setProperty("vehicleColor", kBodyColor);
    m_centre->setItemText(1, tr("Centre: Body"));
    m_timeSlider->setToolTip(tr("Time"));
}

void GlobeView::setSolarPlaybackTime(double epoch) {
    m_scene->setProperty("bodyPosition",
                         scene::fromHeliocentric(orbit::stateAt(*m_result, m_time).r));
    QVariantList planets;
    for (const orbit::Planet planet : orbit::kAllPlanets) {
        planets.append(
            QVariant::fromValue(scene::fromHeliocentric(orbit::planetPosition(planet, epoch))));
    }
    m_scene->setProperty("planetPositions", planets);
    m_timeLabel->setText(formatUtc(epoch));
    if (!m_timeSlider->isSliderDown() && m_result->duration > 0.0) {
        m_timeSlider->setValue(
            static_cast<int>(std::lround(m_time / m_result->duration * kSliderSteps)));
    }
}

/// From ecliptic north, tilted a little so the orbits read as ellipses in
/// space rather than a flat drawing, far enough to see the whole path.
void GlobeView::resetSolarView() {
    const orbit::Vec3 from{0.0, -0.55, 1.0}; // physics: mostly ecliptic north (z)
    QMetaObject::invokeMethod(
        m_scene, "lookAt",
        // A direction: unscaled (the solar scaling would
        // shrink it to nothing).
        Q_ARG(QVariant, QVariant::fromValue(scene::fromPhysics(from).normalized())),
        Q_ARG(QVariant, solarViewDistance()));
}

/// The plane the selected flight is in at time t, as its unit normal (the
/// direction of r × v). While the rocket is still climbing the plane isn't
/// settled, so below 150 km this uses the first moment the flight gets
/// above that (or its highest point, if it never does).
orbit::Vec3 GlobeView::orbitNormalAt(double t) const {
    constexpr double kSettledAltitude = 150e3; // m
    orbit::State state = orbit::stateAt(*m_result, t);
    if (orbit::norm(state.r) - orbit::kRadiusEarth < kSettledAltitude) {
        const std::vector<orbit::Sample>& samples = m_result->samples;
        auto it = std::ranges::find_if(
            samples, [](const orbit::Sample& s) { return s.altitude() >= kSettledAltitude; });
        if (it == samples.end()) {
            it = std::ranges::max_element(samples, {}, &orbit::Sample::altitude);
        }
        if (it != samples.end()) state = it->state;
    }
    const orbit::Vec3 h = orbit::cross(state.r, state.v);
    return orbit::norm(h) > 0.0 ? orbit::normalized(h) : orbit::Vec3{0.0, 0.0, 1.0};
}

/// Look at the selected flight's orbit from above its plane, or edge-on.
/// Edge-on looks along the line of nodes, where the orbit crosses the
/// equator: both planes contain that line, so both show as lines and the
/// angle between them is the inclination. A distance of 0 picks one.
void GlobeView::lookAtOrbit(bool edgeOn, double t, double distanceKm) {
    if (!m_scene || !m_result) return;
    if (distanceKm <= 0.0) distanceKm = edgeOn ? viewDistance() : viewFromAboveDistance();
    orbit::Vec3 from = orbitNormalAt(t);
    if (edgeOn) {
        from = orbit::cross(orbit::Vec3{0.0, 0.0, 1.0}, from);
        if (orbit::norm(from) < 1e-6) from = {1.0, 0.0, 0.0}; // equatorial: any side will do
        // From the node on the rocket's side, so it passes in front.
        if (orbit::dot(from, orbit::stateAt(*m_result, t).r) < 0.0) from = -from;
    }
    QMetaObject::invokeMethod(m_scene, "lookAt",
                              Q_ARG(QVariant, QVariant::fromValue(scene::fromPhysics(from))),
                              Q_ARG(QVariant, distanceKm));
}

void GlobeView::lookFromNorth(double distanceKm) {
    if (!m_scene) return;
    QMetaObject::invokeMethod(
        m_scene, "lookAt",
        Q_ARG(QVariant, QVariant::fromValue(scene::fromPhysics(orbit::Vec3{0.0, 0.0, 1.0}))),
        Q_ARG(QVariant, distanceKm));
}

/// A natural body's starting view. One that comes close (within the Moon's
/// distance) is seen from above the plane of its closest approach, centred
/// between the Earth and that point, so the bend of its path is in the
/// middle. One that stays far is seen whole, from ecliptic north: such
/// bodies go round the Sun near the plane of the Earth's orbit.
void GlobeView::resetBodyView() {
    if (!m_scene || !m_result || !m_result->encounter) return;
    const orbit::State& closest = m_result->encounter->closest.state;
    const double closestDistance = orbit::norm(closest.r);
    const auto lookFrom = [this](const orbit::Vec3& direction, double distanceKm) {
        QMetaObject::invokeMethod(
            m_scene, "lookAt", Q_ARG(QVariant, QVariant::fromValue(scene::fromPhysics(direction))),
            Q_ARG(QVariant, distanceKm));
    };
    if (closestDistance < 4.0e8) {
        const orbit::Vec3 h = orbit::cross(closest.r, closest.v);
        lookFrom(orbit::norm(h) > 0.0 ? orbit::normalized(h) : orbit::Vec3{0.0, 0.0, 1.0},
                 viewDistance());
        m_scene->setProperty("panOffset", scene::fromPhysics(0.6 * closest.r));
        return;
    }
    // The middle of the path's bounding box, and how far it reaches from there.
    orbit::Vec3 low = m_result->samples.front().state.r;
    orbit::Vec3 high = low;
    for (const orbit::Sample& s : m_result->samples) {
        low = {std::min(low.x, s.state.r.x), std::min(low.y, s.state.r.y),
               std::min(low.z, s.state.r.z)};
        high = {std::max(high.x, s.state.r.x), std::max(high.y, s.state.r.y),
                std::max(high.z, s.state.r.z)};
    }
    const orbit::Vec3 middle = 0.5 * (low + high);
    constexpr double obliquity = 23.43928 * std::numbers::pi / 180.0;
    const orbit::Vec3 eclipticNorth{0.0, -std::sin(obliquity), std::cos(obliquity)};
    lookFrom(eclipticNorth, 0.9 * orbit::norm(high - low) / 1000.0);
    m_scene->setProperty("panOffset", scene::fromPhysics(middle));
}

/// A good starting view of the selected flight, and a playback speed that
/// shows all of it in about a minute and a half.
void GlobeView::resetView() {
    if (!m_scene || !m_result) return;
    m_centre->setCurrentIndex(0); // circle the Earth (or the Sun)

    const double farthest = farthestDistance();
    if (m_solar) {
        resetSolarView();
    } else if (m_result->encounter) {
        resetBodyView();
    } else if (farthest > 5.0e7) {
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
                                  Q_ARG(QVariant, viewDistance()));
    } else {
        // Look at the launch site, where it is at the current playback time.
        const orbit::LaunchSite& site = m_result->mission.site;
        const double theta =
            orbit::earthRotationAngle(m_time, orbit::earthRotationAngleAt(m_result->startEpoch));
        const orbit::Vec3 siteEci = orbit::rotateZ(
            orbit::positionFromLatLon(site.latitude, site.longitude, orbit::kRadiusEarth), theta);
        QMetaObject::invokeMethod(m_scene, "lookAt",
                                  Q_ARG(QVariant, QVariant::fromValue(scene::fromPhysics(siteEci))),
                                  Q_ARG(QVariant, viewDistance()));
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
