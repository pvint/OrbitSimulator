// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_window.hpp"

#include "burn_planner.hpp"
#include "display_format.hpp"
#include "game_session.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/orbit.hpp"
#include "orbit/core/solar_system.hpp"
#include "polyline_geometry.hpp"
#include "scene_coordinates.hpp"
#include "timeline_bar.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QQuickItem>
#include <QQuickWidget>
#include <QRandomGenerator>
#include <QStyle>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr double kDay = 86400.0;

/// Path colours: the flown part, then a new colour after each burn.
const QColor kFlownColor(0x47, 0x55, 0x69);
const std::array kSegmentColors{QColor(0x38, 0xbd, 0xf8), QColor(0xfb, 0xbf, 0x24),
                                QColor(0x34, 0xd3, 0x99), QColor(0xf4, 0x72, 0xb6),
                                QColor(0xa7, 0x8b, 0xfa)};
const std::array kPlanetColors{QColor(0xa8, 0xa2, 0x9e), QColor(0xe9, 0xc4, 0x6a),
                               QColor(0x4f, 0x9d, 0xe0), QColor(0xd9, 0x60, 0x3b),
                               QColor(0xd8, 0xa6, 0x6c), QColor(0xe3, 0xc9, 0x8f),
                               QColor(0x8f, 0xd3, 0xe0), QColor(0x5b, 0x7f, 0xe8)};

/// The ship's state at `epoch`, between the samples either side (straight
/// line: the samples are close enough that the curve between them doesn't
/// show at the solar system's scale).
orbit::VoyageSample sampleAt(const std::vector<orbit::VoyageSample>& samples, double epoch) {
    if (samples.empty()) return {};
    if (epoch <= samples.front().epoch) return samples.front();
    if (epoch >= samples.back().epoch) return samples.back();
    const auto after = std::ranges::upper_bound(samples, epoch, {}, &orbit::VoyageSample::epoch);
    const auto& b = *after;
    const auto& a = *(after - 1);
    const double span = b.epoch - a.epoch;
    const double f = span > 0.0 ? (epoch - a.epoch) / span : 1.0;
    return {epoch, a.r + f * (b.r - a.r), a.v + f * (b.v - a.v), a.mass + f * (b.mass - a.mass)};
}

/// The epoch now (s since J2000).
double epochNow() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDate d = now.date();
    const QTime t = now.time();
    return orbit::epochFromCalendar(
        {d.year(), d.month(), d.day(), t.hour(), t.minute(), static_cast<double>(t.second())});
}

QString goalText(orbit::Goal goal) {
    switch (goal) {
    case orbit::Goal::ReachEarth:
        return GameWindow::tr("Get back to the Earth: come within 925,000 km of it, where its "
                              "gravity takes over from the Sun's.");
    case orbit::Goal::Reentry:
        return GameWindow::tr("Get home: hit the Earth's atmosphere at an angle between 5° and "
                              "8°, slower than 13 km/s.");
    case orbit::Goal::EarthOrbit:
        return GameWindow::tr("Get home: brake into an orbit around the Earth, above the air.");
    }
    return {};
}

QString endingTitle(orbit::VoyageEnd end) {
    switch (end) {
    case orbit::VoyageEnd::TooSteep:
    case orbit::VoyageEnd::TooFast:
        return GameWindow::tr("Burned up");
    case orbit::VoyageEnd::Skipped:
        return GameWindow::tr("Skipped off into space");
    case orbit::VoyageEnd::HitSun:
        return GameWindow::tr("Into the Sun");
    default:
        return GameWindow::tr("Not quite");
    }
}

} // namespace

GameWindow::GameWindow(QWidget* parent)
    : QWidget(parent), m_session(new GameSession(this)), m_planner(new BurnPlanner(m_session)),
      m_view(new QQuickWidget) {
    setWindowTitle(tr("Lost in Space — Orbit Simulator"));
    resize(1400, 860);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* left = new QVBoxLayout;
    left->setSpacing(0);
    m_view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/OrbitSim/qml/VoyageScene.qml")));
    m_view->setMinimumSize(500, 400);
    left->addWidget(m_view, 1);
    m_timeline = new TimelineBar;
    left->addWidget(m_timeline);
    buildControls(left);
    layout->addLayout(left, 1);
    m_planner->setFixedWidth(330);
    layout->addWidget(m_planner);

    connectScene();
    connect(m_session, &GameSession::puzzleChanged, this, &GameWindow::onPuzzleChanged);
    connect(m_session, &GameSession::predictionChanged, this, &GameWindow::onPredictionChanged);
    connect(m_session, &GameSession::nowChanged, this, &GameWindow::onNowChanged);
    connect(m_timeline, &TimelineBar::dateRequested, this, &GameWindow::pickDate);
    connect(m_timeline, &TimelineBar::timeSelected, this, [this](double epoch) {
        // Looking around by hand: stop the clock, and let the ending card come again.
        setFlying(false);
        m_endingShown = false;
        m_session->setNow(epoch);
    });
    connect(m_session, &GameSession::burnsChanged, this, &GameWindow::updateMarkers);
    connect(m_session, &GameSession::selectionChanged, this, &GameWindow::updateMarkers);
    connect(m_session, &GameSession::hintReady, this,
            [this](const QString& message) { showCard(tr("Hint"), message, tr("OK")); });
    connect(m_session, &GameSession::busyChanged, this, [this](bool busy) {
        if (!m_scene) return;
        QString text;
        if (busy) {
            text =
                m_welcomePending ? tr("Finding you somewhere to be lost…") : tr("Working it out…");
        }
        m_scene->setProperty("busyText", text);
    });

    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &GameWindow::onTick);
    updatePlanetOrbits();
    onNowChanged();
}

void GameWindow::buildControls(QBoxLayout* layout) {
    auto* bar = new QWidget(this);
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(8, 6, 8, 6);

    m_restart =
        new QPushButton(style()->standardIcon(QStyle::SP_MediaSkipBackward), tr("Restart"), bar);
    m_restart->setObjectName(QStringLiteral("button-restart"));
    m_restart->setToolTip(tr("Back to the start: every burn can be changed again."));
    m_fly = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), tr("Fly"), bar);
    m_fly->setObjectName(QStringLiteral("button-fly"));
    m_fly->setToolTip(tr("Let time run: the ship follows its path, and burns fire as it "
                         "reaches them (Space)."));
    m_fly->setShortcut(Qt::Key_Space);
    m_next =
        new QPushButton(style()->standardIcon(QStyle::SP_MediaSkipForward), tr("Next burn"), bar);
    m_next->setObjectName(QStringLiteral("button-next-burn"));
    m_next->setToolTip(tr("Jump ahead to the next burn."));
    m_speed = new QComboBox(bar);
    m_speed->setObjectName(QStringLiteral("combo-speed"));
    for (const int days : {1, 5, 20, 60}) {
        m_speed->addItem(days == 1 ? tr("1 day a second") : tr("%1 days a second").arg(days), days);
    }
    m_speed->setCurrentIndex(2);
    m_centre = new QComboBox(bar);
    m_centre->setObjectName(QStringLiteral("combo-centre"));
    m_centre->addItems({tr("Centre: Sun"), tr("Centre: You"), tr("Centre: Earth")});
    m_newGame = new QPushButton(tr("New game"), bar);
    m_newGame->setObjectName(QStringLiteral("button-new-game"));

    row->addWidget(m_restart);
    row->addWidget(m_fly);
    row->addWidget(m_next);
    row->addWidget(m_speed);
    row->addStretch(1);
    row->addWidget(m_centre);
    row->addWidget(m_newGame);
    layout->addWidget(bar);

    connect(m_restart, &QPushButton::clicked, this, [this] {
        setFlying(false);
        m_endingShown = false;
        m_session->restart();
    });
    connect(m_fly, &QPushButton::clicked, this, [this] { setFlying(!m_flying); });
    connect(m_next, &QPushButton::clicked, this, &GameWindow::jumpToNextBurn);
    connect(m_newGame, &QPushButton::clicked, this, [this] {
        setFlying(false);
        if (!m_scene) return;
        m_scene->setProperty("cardMode", QStringLiteral("start"));
        m_scene->setProperty("cardTitle", tr("Lost in space"));
        m_scene->setProperty("cardText", tr("Choose how hard:"));
    });
    connect(m_centre, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_scene) return;
        m_scene->setProperty("followTarget", index);
        if (index == 0) {
            resetView();
        } else {
            m_scene->setProperty("panOffset", QVector3D());
            m_scene->setProperty("distance", 40.0); // million km
        }
    });
}

void GameWindow::connectScene() {
    m_scene = m_view->rootObject();
    if (!m_scene) {
        qWarning() << "VoyageScene.qml did not load:" << m_view->errors();
        return;
    }
    m_path = qobject_cast<PolylineGeometry*>(m_scene->property("path").value<QObject*>());
    m_arrivalLine =
        qobject_cast<PolylineGeometry*>(m_scene->property("arrivalLine").value<QObject*>());
    m_startOrbit =
        qobject_cast<PolylineGeometry*>(m_scene->property("startOrbit").value<QObject*>());
    for (std::size_t i = 0; i < m_planetOrbits.size(); ++i) {
        QVariant model;
        QMetaObject::invokeMethod(m_scene, "planetOrbit", Q_RETURN_ARG(QVariant, model),
                                  Q_ARG(QVariant, static_cast<int>(i)));
        auto* object = model.value<QObject*>();
        m_planetOrbits.at(i) =
            object ? qobject_cast<PolylineGeometry*>(object->property("line").value<QObject*>())
                   : nullptr;
    }
    connect(m_scene, SIGNAL(difficultyChosen(int)), this, SLOT(chooseDifficulty(int)));
    connect(m_scene, SIGNAL(cardAccepted()), this, SLOT(acceptCard()));

    m_scene->setProperty(
        "cardText",
        tr("You wake up somewhere between the planets, far from home. There's some fuel left "
           "in the tanks: enough, if you use it well.<br><br>Plan burns, watch where they take "
           "you, and find your way back to the Earth. The physics is real: the Sun and every "
           "planet pull on you all the way.<br><br>Choose how hard:"));
    resetView();
}

void GameWindow::chooseDifficulty(int index) {
    const auto difficulty = static_cast<orbit::Difficulty>(std::clamp(index, 0, 2));
    startPuzzle(difficulty, QRandomGenerator::global()->generate64());
}

void GameWindow::acceptCard() {
    if (m_scene) m_scene->setProperty("cardMode", QString());
}

void GameWindow::startPuzzle(orbit::Difficulty difficulty, std::uint64_t seed) {
    setFlying(false);
    m_endingShown = false;
    m_welcomePending = true;
    if (m_scene) m_scene->setProperty("cardMode", QString());
    m_session->newPuzzle(difficulty, seed, epochNow());
}

void GameWindow::onPuzzleChanged() {
    const orbit::Puzzle& puzzle = m_session->puzzle();
    const orbit::Voyage& v = puzzle.voyage;
    m_planner->setEnabled(true);
    if (m_scene) m_scene->setProperty("goalText", goalText(v.goal));
    updatePlanetOrbits();
    updateStartOrbit();
    m_centre->setCurrentIndex(0);
    resetView();

    if (m_welcomePending) {
        m_welcomePending = false;
        const orbit::OrbitalElements el =
            orbit::elementsFromState(v.position, v.velocity, orbit::kMuSun, orbit::kRadiusSun);
        const double fromEarth =
            orbit::norm(v.position - orbit::planetPosition(orbit::Planet::Earth, v.startEpoch));
        showCard(tr("Lost!"),
                 tr("Your ship is %1 from the Sun and %2 from the Earth, on an orbit that takes "
                    "%3 days to go round (the Earth's takes 365). You have <b>%4</b> of Δv in "
                    "the tanks: that's how much you can change your speed, in all.<br><br>"
                    "<b>Goal:</b> %5<br><br>Add a burn on the right and watch your path change, "
                    "or press <b>Hint</b> if you're stuck. Then <b>Fly</b>.")
                     .arg(display::longDistance(orbit::norm(v.position)),
                          display::longDistance(fromEarth),
                          QString::number(std::lround(el.period / kDay)),
                          display::speed(puzzle.budget), goalText(v.goal)),
                 tr("Let's go"));
    }
}

void GameWindow::showCard(const QString& title, const QString& text, const QString& button) {
    if (!m_scene || !m_cardsEnabled) return;
    m_scene->setProperty("cardTitle", title);
    m_scene->setProperty("cardText", text);
    m_scene->setProperty("cardButton", button);
    m_scene->setProperty("cardMode", QStringLiteral("message"));
}

void GameWindow::setFlying(bool flying) {
    m_flying = flying && m_session->hasPuzzle();
    m_fly->setText(m_flying ? tr("Pause") : tr("Fly"));
    m_fly->setIcon(style()->standardIcon(m_flying ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    if (m_flying) {
        m_frameClock.start();
        m_timer.start();
    } else {
        m_timer.stop();
    }
}

void GameWindow::onTick() {
    const auto* prediction = m_session->prediction();
    if (!prediction) return;
    const double seconds = static_cast<double>(m_frameClock.restart()) / 1000.0;
    const double daysPerSecond = m_speed->currentData().toDouble();
    const double next = m_session->now() + seconds * daysPerSecond * kDay;
    m_session->setNow(std::min(next, prediction->endEpoch));
    if (m_session->now() >= prediction->endEpoch) setFlying(false);
}

namespace {
/// Big numbers in the style "5.97e24 kg" (planet masses don't fit in tonnes).
QString bigMass(double kilograms) {
    return QObject::tr("%1 kg").arg(
        QString::number(kilograms, 'e', 2).replace(QLatin1String("e+"), QLatin1String("e")));
}

QString infoRow(const QString& name, const QString& value) {
    return QStringLiteral(
               "<tr><td style='color:#94a3b8'>%1&nbsp;&nbsp;</td><td align='right'>%2</td></tr>")
        .arg(name, value);
}

QDateTime toDateTime(double epoch) {
    const orbit::CalendarTime t = orbit::calendarFromEpoch(std::floor(epoch / 60.0) * 60.0 + 1.0);
    return QDateTime(QDate(t.year, t.month, t.day), QTime(t.hour, t.minute), QTimeZone::UTC);
}

double toEpoch(const QDateTime& when) {
    const QDateTime utc = when.toUTC();
    return orbit::epochFromCalendar({utc.date().year(), utc.date().month(), utc.date().day(),
                                     utc.time().hour(), utc.time().minute(), 0.0});
}
} // namespace

/// A small dialog with a calendar drop-down: type a date and time (UTC) or pick one.
void GameWindow::pickDate() {
    const auto* prediction = m_session->prediction();
    if (!prediction || prediction->samples.empty()) return;
    const double end = prediction->end != orbit::VoyageEnd::Horizon
                           ? prediction->endEpoch
                           : prediction->samples.back().epoch;
    setFlying(false);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Go to a date"));
    auto* form = new QFormLayout(&dialog);
    auto* edit = new QDateTimeEdit(toDateTime(m_session->now()), &dialog);
    edit->setObjectName(QStringLiteral("edit-date"));
    edit->setCalendarPopup(true);
    edit->setTimeZone(QTimeZone::UTC);
    edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'"));
    edit->setDateTimeRange(toDateTime(m_session->startEpoch()), toDateTime(end));
    form->addRow(tr("Date and time:"), edit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    m_endingShown = false;
    m_session->setNow(toEpoch(edit->dateTime()));
}

/// What the hover card says about each object, at this moment. The numbers
/// are the ones you plan burns with: speeds (relative to the Sun, and to you)
/// and how far away things are.
void GameWindow::updateInfo(double now, const orbit::VoyageSample& ship) {
    // G, to turn a planet's μ = G·M back into a mass (CODATA 2018).
    constexpr double kG = 6.67430e-11;
    constexpr double kSunMass = orbit::kMuSun / kG;
    const auto* prediction = m_session->prediction();
    QVariantList texts;
    auto card = [](const QString& title, const QString& rows) {
        return QStringLiteral("<b>%1</b><table>%2</table>").arg(title, rows);
    };
    for (const orbit::Planet planet : orbit::kAllPlanets) {
        const orbit::Vec3 r = orbit::planetPosition(planet, now);
        const orbit::Vec3 v = orbit::planetVelocity(planet, now);
        const auto index = static_cast<int>(planet);
        texts.append(card(
            m_scene->property("planetNames").toList().value(index).toString(),
            infoRow(tr("Speed (Sun)"), display::speed(orbit::norm(v))) +
                infoRow(tr("Distance from Sun"), display::longDistance(orbit::norm(r))) +
                infoRow(tr("Mass"), bigMass(orbit::planetMu(planet) / kG)) +
                infoRow(tr("Radius"), display::distance(orbit::planetRadius(planet))) +
                infoRow(tr("Distance from you"), display::longDistance(orbit::norm(r - ship.r))) +
                infoRow(tr("Speed vs you"), display::speed(orbit::norm(v - ship.v)))));
    }
    texts.append(card(tr("Sun"), infoRow(tr("Mass"), bigMass(kSunMass)) +
                                     infoRow(tr("Radius"), display::distance(orbit::kRadiusSun)) +
                                     infoRow(tr("Distance from you"),
                                             display::longDistance(orbit::norm(ship.r)))));
    const orbit::Vec3 earthR = orbit::planetPosition(orbit::Planet::Earth, now);
    const orbit::Vec3 earthV = orbit::planetVelocity(orbit::Planet::Earth, now);
    QString rows =
        infoRow(tr("Speed (Sun)"), display::speed(orbit::norm(ship.v))) +
        infoRow(tr("Speed (Earth)"), display::speed(orbit::norm(ship.v - earthV))) +
        infoRow(tr("Distance from Sun"), display::longDistance(orbit::norm(ship.r))) +
        infoRow(tr("Distance from Earth"), display::longDistance(orbit::norm(ship.r - earthR))) +
        infoRow(tr("Mass"), display::mass(ship.mass));
    if (prediction)
        rows += infoRow(tr("Δv left after burns"), display::speed(prediction->deltaVLeft));
    texts.append(card(tr("You"), rows));
    m_scene->setProperty("infoTexts", texts);
}

void GameWindow::jumpToNextBurn() {
    for (const orbit::Burn& b : m_session->burns()) {
        if (b.epoch > m_session->now()) {
            m_session->setNow(b.epoch);
            m_session->select(static_cast<int>(&b - m_session->burns().data()));
            return;
        }
    }
}

/// From ecliptic north, tilted a little, far enough to see the ship's orbit
/// and the Earth's.
void GameWindow::resetView() {
    if (!m_scene) return;
    double reach = 1.2 * orbit::kAstronomicalUnit;
    if (m_session->hasPuzzle()) {
        const orbit::Voyage& v = m_session->puzzle().voyage;
        const orbit::OrbitalElements el =
            orbit::elementsFromState(v.position, v.velocity, orbit::kMuSun, orbit::kRadiusSun);
        reach = std::max(reach, el.bound ? el.apoapsis : orbit::norm(v.position));
    }
    m_scene->setProperty("followTarget", 0);
    QMetaObject::invokeMethod(
        m_scene, "lookAt",
        Q_ARG(QVariant, QVariant::fromValue(scene::fromPhysics({0.0, -0.45, 1.0}).normalized())),
        Q_ARG(QVariant, 2.4 * reach / scene::kSolarMetresPerUnit));
}

void GameWindow::updatePlanetOrbits() {
    const double epoch = m_session->hasPuzzle() ? m_session->startEpoch() : epochNow();
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

/// The orbit the ship starts on, faint: where it would go without burns.
void GameWindow::updateStartOrbit() {
    if (!m_startOrbit || !m_session->hasPuzzle()) return;
    const orbit::Voyage& v = m_session->puzzle().voyage;
    const orbit::OrbitalElements el =
        orbit::elementsFromState(v.position, v.velocity, orbit::kMuSun, orbit::kRadiusSun);
    QList<QVector3D> points;
    constexpr int kPoints = 360;
    for (int i = 0; i <= kPoints; ++i) {
        const double nu = 2.0 * std::numbers::pi * i / kPoints;
        points.append(scene::fromHeliocentric(
            orbit::stateFromPeriapsis(el.periapsis, el.eccentricity, el.inclination, el.raan,
                                      el.argumentOfPeriapsis, nu, orbit::kMuSun)
                .r));
    }
    m_startOrbit->setPoints(points, QList<QColor>(points.size(), QColor(0x94, 0xa3, 0xb8)));
}

void GameWindow::onPredictionChanged() {
    updateTimeline();
    m_pathSplit = static_cast<std::size_t>(-1);
    updatePath();
    updateMarkers();
    updateStatus();
    onNowChanged();
}

/// The path, flown part faint, the rest in a new colour after each burn.
void GameWindow::updatePath() {
    const auto* prediction = m_session->prediction();
    if (!m_path || !prediction) return;
    const auto& samples = prediction->samples;
    const double now = m_session->now();
    const auto split = static_cast<std::size_t>(
        std::ranges::upper_bound(samples, now, {}, &orbit::VoyageSample::epoch) - samples.begin());
    if (split == m_pathSplit) return;
    m_pathSplit = split;

    QList<QVector3D> points;
    QList<QColor> colors;
    points.reserve(static_cast<qsizetype>(samples.size()));
    colors.reserve(static_cast<qsizetype>(samples.size()));
    std::size_t burn = 0;
    const auto& burns = prediction->burns;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        // A burn's two samples share its epoch; the second starts the new colour.
        while (burn < burns.size() && (samples[i].epoch > burns[burn].burn.epoch ||
                                       (samples[i].epoch == burns[burn].burn.epoch && i > 0 &&
                                        samples[i - 1].epoch == samples[i].epoch))) {
            ++burn;
        }
        points.append(scene::fromHeliocentric(samples[i].r));
        colors.append(i < split ? kFlownColor : kSegmentColors.at(burn % kSegmentColors.size()));
    }
    m_path->setPoints(points, colors);
}

/// Burn markers and the closest-approach markers.
void GameWindow::updateTimeline() {
    const auto* prediction = m_session->prediction();
    if (!prediction || prediction->samples.empty()) return;
    const double end = prediction->end != orbit::VoyageEnd::Horizon
                           ? prediction->endEpoch
                           : prediction->samples.back().epoch;
    m_timeline->setRange(m_session->startEpoch(), end);
    std::vector<double> burns;
    for (const orbit::Burn& b : m_session->burns()) burns.push_back(b.epoch);
    m_timeline->setBurns(std::move(burns));
}

void GameWindow::updateMarkers() {
    updateTimeline();
    const auto* prediction = m_session->prediction();
    if (!m_scene || !prediction) return;
    QVariantList positions;
    QVariantList fired;
    for (int i = 0; i < m_session->burnCount(); ++i) {
        const orbit::Burn& b = m_session->burns()[static_cast<std::size_t>(i)];
        positions.append(
            QVariant::fromValue(scene::fromHeliocentric(sampleAt(prediction->samples, b.epoch).r)));
        fired.append(m_session->isLocked(i));
    }
    m_scene->setProperty("burnPositions", positions);
    m_scene->setProperty("burnFired", fired);
    m_scene->setProperty("selectedBurn", m_session->selected());

    const orbit::EarthApproach& c = prediction->closest;
    const bool show = c.valid && c.epoch > m_session->now();
    m_scene->setProperty("arrivalVisible", show);
    if (show) {
        const orbit::Vec3 earth = orbit::planetPosition(orbit::Planet::Earth, c.epoch);
        const QVector3D ship = scene::fromHeliocentric(earth + c.relativePosition);
        m_scene->setProperty("arrivalShip", ship);
        m_scene->setProperty("arrivalEarth", scene::fromHeliocentric(earth));
        // Close in, how high above the ground matters; far out, how far.
        const bool near = c.distance < orbit::sphereOfInfluence(orbit::Planet::Earth, c.epoch);
        m_scene->setProperty(
            "arrivalLabel",
            near ? tr("closest to Earth: %1 up, %2")
                       .arg(display::longDistance(c.distance - orbit::kRadiusEarth),
                            display::date(c.epoch))
                 : tr("closest to Earth: %1 away, %2")
                       .arg(display::longDistance(c.distance), display::date(c.epoch)));
        if (m_arrivalLine) {
            const QColor color(0x93, 0xc5, 0xfd);
            m_arrivalLine->setPoints({ship, scene::fromHeliocentric(earth)}, {color, color});
        }
    }
}

/// What the predicted path comes to, in the top-right corner.
void GameWindow::updateStatus() {
    const auto* p = m_session->prediction();
    if (!m_scene || !p) return;
    using orbit::VoyageEnd;
    const orbit::Goal goal = m_session->puzzle().voyage.goal;
    QString text;
    QColor color(0xcb, 0xd5, 0xe1);
    const QString when = display::date(p->endEpoch);
    if (p->goalMet) {
        color = QColor(0x86, 0xef, 0xac);
        switch (goal) {
        case orbit::Goal::ReachEarth:
            text = tr("<b>On course for home!</b> You reach the Earth on %1.").arg(when);
            break;
        case orbit::Goal::Reentry:
            text = tr("<b>On course for home!</b> You enter the air on %1 at %2, %3 below "
                      "level.")
                       .arg(when, display::speed(p->entrySpeed), display::angle(-p->entryAngle));
            break;
        case orbit::Goal::EarthOrbit:
            text = tr("<b>On course for home!</b> You settle into orbit around the Earth on %1.")
                       .arg(when);
            break;
        }
    } else if (p->end == VoyageEnd::TooSteep || p->end == VoyageEnd::TooFast ||
               p->end == VoyageEnd::Skipped || p->end == VoyageEnd::HitSun) {
        color = QColor(0xfc, 0xa5, 0xa5);
        text = tr("<b>Trouble ahead:</b> %1 on %2.")
                   .arg(QString::fromUtf8(orbit::describe(p->end).data()).toLower(), when);
        if (p->entrySpeed > 0.0) {
            text += QLatin1Char(' ') +
                    tr("You'd hit the air at %1, %2 below level.")
                        .arg(display::speed(p->entrySpeed), display::angle(-p->entryAngle));
        }
    } else if (p->end == VoyageEnd::Captured) {
        color = QColor(0xfd, 0xe6, 0x8a);
        text = tr("You'd end up in orbit around the Earth. To land, add a burn that lowers "
                  "your lowest point into the air.");
    } else if (p->closest.valid) {
        const double altitude = p->closest.distance - orbit::kRadiusEarth;
        const QString day = display::date(p->closest.epoch);
        if (p->closest.distance < orbit::sphereOfInfluence(orbit::Planet::Earth, p->endEpoch)) {
            text = tr("You pass <b>%1 above the Earth</b> on %2: close!")
                       .arg(display::longDistance(altitude), day);
            if (goal == orbit::Goal::EarthOrbit) {
                text += QLatin1Char(' ') + tr("Brake there to stay.");
            }
        } else {
            text = tr("Closest to the Earth: <b>%1</b> away, on %2.")
                       .arg(display::longDistance(p->closest.distance), day);
        }
    }
    if (std::ranges::any_of(p->burns, [](const auto& b) { return b.shortOfFuel; })) {
        text += QStringLiteral("<br>") + tr("<b>The tanks run dry before the burns are done.</b>");
    }
    m_scene->setProperty("statusText", text);
    m_scene->setProperty("statusColor", color);
}

void GameWindow::onNowChanged() {
    if (!m_scene) return;
    const double now = m_session->hasPuzzle() ? m_session->now() : epochNow();
    QVariantList planets;
    for (const orbit::Planet planet : orbit::kAllPlanets) {
        planets.append(
            QVariant::fromValue(scene::fromHeliocentric(orbit::planetPosition(planet, now))));
    }
    m_scene->setProperty("planetPositions", planets);
    m_scene->setProperty("dateText", m_session->hasPuzzle() ? display::date(now) : QString());

    m_timeline->setNow(now);
    const auto* prediction = m_session->prediction();
    if (!prediction) return;
    const orbit::VoyageSample ship = sampleAt(prediction->samples, now);
    m_scene->setProperty("shipPosition", scene::fromHeliocentric(ship.r));
    // Your own speed: the number to compare with a burn's size. Δv is a change
    // of velocity, so a burn of 1,000 m/s adds or removes up to that much.
    const double sunSpeed = orbit::norm(ship.v);
    const double earthSpeed =
        orbit::norm(ship.v - orbit::planetVelocity(orbit::Planet::Earth, now));
    updateInfo(now, ship);
    m_scene->setProperty("speedText",
                         tr("Speed: %1 (Sun), %2 (Earth)")
                             .arg(display::speed(sunSpeed), display::speed(earthSpeed)));
    updatePath();
    updateMarkers();
    // The end of a flight that ends: show how it went (once).
    if (prediction->end != orbit::VoyageEnd::Horizon && m_session->predictionCurrent() &&
        now >= prediction->endEpoch && !m_endingShown) {
        showEnding();
    }
}

void GameWindow::showEnding() {
    const auto* p = m_session->prediction();
    m_endingShown = true;
    setFlying(false);
    const double days = (p->endEpoch - m_session->startEpoch()) / kDay;
    if (p->goalMet) {
        const double budget = m_session->puzzle().budget;
        showCard(tr("Home!"),
                 tr("You made it in %1 days, with %2 of Δv left (%3 % of what you had). "
                    "Hints used: %4.")
                     .arg(std::lround(days))
                     .arg(display::speed(p->deltaVLeft))
                     .arg(std::lround(100.0 * p->deltaVLeft / budget))
                     .arg(m_session->hintsUsed()),
                 tr("OK"));
    } else {
        showCard(endingTitle(p->end),
                 tr("%1 after %2 days. Press <b>Restart</b> to go back to the beginning and "
                    "change your burns, or start a <b>New game</b>.")
                     .arg(QString::fromUtf8(orbit::describe(p->end).data()))
                     .arg(std::lround(days)),
                 tr("OK"));
    }
}
