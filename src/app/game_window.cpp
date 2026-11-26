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
/// The epoch now (s since J2000).
double epochNow() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDate d = now.date();
    const QTime t = now.time();
    return orbit::epochFromCalendar(
        {d.year(), d.month(), d.day(), t.hour(), t.minute(), static_cast<double>(t.second())});
}

/// An orbit's period: hours for an orbit around the Earth, whole days up to
/// two years, then years ("—" if the path isn't a closed orbit).
QString periodText(double seconds) {
    constexpr double kYear = 365.25 * kDay;
    if (!std::isfinite(seconds) || seconds <= 0.0) return QStringLiteral("—");
    if (seconds < 3.0 * kDay) return display::duration(seconds);
    if (seconds < 2.0 * kYear) return QObject::tr("%1 days").arg(std::lround(seconds / kDay));
    return QObject::tr("%1 years").arg(QString::number(seconds / kYear, 'f', 1));
}

/// The period of a closed orbit with semi-major axis a around μ, by
/// Kepler's third law: T = 2π √(a³/μ).
double keplerPeriod(double semiMajorAxis, double mu) {
    return 2.0 * std::numbers::pi * std::sqrt(semiMajorAxis * semiMajorAxis * semiMajorAxis / mu);
}

/// A change of speed with its sign: "+5,612 m/s", "−830 m/s".
QString signedSpeed(double metresPerSecond) {
    return (metresPerSecond < 0.0 ? QStringLiteral("−") : QStringLiteral("+")) +
           display::speed(std::abs(metresPerSecond));
}

QString planetText(orbit::Planet planet) {
    return QString::fromUtf8(orbit::planetName(planet).data());
}

/// Where the ship is, to fit "You're %1".
QString regionPhrase(orbit::Region region) {
    switch (region) {
    case orbit::Region::BetweenPlanets:
        return QObject::tr("somewhere between the planets");
    case orbit::Region::Mercury:
        return QObject::tr("near Mercury");
    case orbit::Region::Earth:
        return QObject::tr("near the Earth");
    case orbit::Region::Venus:
        return QObject::tr("near Venus");
    case orbit::Region::Mars:
        return QObject::tr("near Mars");
    case orbit::Region::AsteroidBelt:
        return QObject::tr("in the asteroid belt");
    case orbit::Region::Jupiter:
        return QObject::tr("near Jupiter");
    case orbit::Region::Saturn:
        return QObject::tr("near Saturn");
    case orbit::Region::Uranus:
        return QObject::tr("near Uranus");
    case orbit::Region::Neptune:
        return QObject::tr("near Neptune");
    }
    return {};
}

QString goalText(orbit::Goal goal, orbit::Planet destination) {
    const QString name = planetText(destination);
    switch (goal) {
    case orbit::Goal::ReachEarth:
        return GameWindow::tr("Get back to the Earth: come within 925,000 km of it, where its "
                              "gravity takes over from the Sun's.");
    case orbit::Goal::Reentry:
        return GameWindow::tr("Get home: hit the Earth's atmosphere at an angle between 5° and "
                              "8°, slower than 13 km/s.");
    case orbit::Goal::EarthOrbit:
        return GameWindow::tr("Get home: brake into an orbit around the Earth, above the air.");
    case orbit::Goal::ReachPlanet:
        return GameWindow::tr("Get to %1: come within its sphere of influence, where its "
                              "gravity takes over from the Sun's.")
            .arg(name);
    case orbit::Goal::ClosePass:
        return GameWindow::tr("Fly close past %1, within %2 of its radii, like Voyager.")
            .arg(name)
            .arg(orbit::VoyageOptions{}.closePassRadii);
    case orbit::Goal::PlanetOrbit:
        return GameWindow::tr("Brake into an orbit around %1, like Cassini or Juno.").arg(name);
    }
    return {};
}

/// How the flight ended, with the planets named.
QString endText(const orbit::VoyageResult& p, orbit::Planet destination) {
    const QString name = planetText(destination);
    switch (p.end) {
    case orbit::VoyageEnd::ReachedPlanet:
        return GameWindow::tr("Reached %1").arg(name);
    case orbit::VoyageEnd::PassedClose:
        return GameWindow::tr("Flew close past %1").arg(name);
    case orbit::VoyageEnd::OrbitingPlanet:
        return GameWindow::tr("In orbit around %1").arg(name);
    case orbit::VoyageEnd::HitPlanet:
        return GameWindow::tr("Crashed into %1").arg(planetText(p.hitPlanet.value_or(destination)));
    default:
        return QString::fromUtf8(orbit::describe(p.end).data());
    }
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
    case orbit::VoyageEnd::HitPlanet:
        return GameWindow::tr("Crashed");
    default:
        return GameWindow::tr("Not quite");
    }
}

} // namespace

GameWindow::GameWindow(QWidget* parent)
    : QWidget(parent), m_session(new GameSession(this)), m_planner(new BurnPlanner(m_session)),
      m_view(new QQuickWidget), m_timeline(new TimelineBar) {
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
    connect(m_session, &GameSession::aimReady, this, [this](bool aimed, const QString& message) {
        // Show the whole new path once it is predicted.
        if (aimed) m_framePending = true;
        showCard(aimed ? tr("Aimed") : tr("Can't aim there"), message, tr("OK"));
    });
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
    for (const int days : {1, 5, 20, 60, 180, 365}) {
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
        m_scene->setProperty("cardText", tr("Where to this time?"));
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
    connect(m_scene, SIGNAL(startChosen(int, bool, int, int)), this,
            SLOT(chooseStart(int, bool, int, int)));
    connect(m_scene, SIGNAL(cardAccepted()), this, SLOT(acceptCard()));

    m_scene->setProperty(
        "cardText",
        tr("Plan burns, watch where they take you, and get where you're going on the fuel "
           "you have. The physics is real: the Sun and every planet pull on you all the way."));
    resetView();
}

void GameWindow::chooseStart(int region, bool inOrbit, int destination, int difficulty) {
    const int lastRegion = static_cast<int>(orbit::kAllRegions.size()) - 1;
    const int lastPlanet = static_cast<int>(orbit::kAllPlanets.size()) - 1;
    orbit::PuzzleSpec spec;
    spec.difficulty = static_cast<orbit::Difficulty>(std::clamp(difficulty, 0, 2));
    spec.from = orbit::kAllRegions.at(static_cast<std::size_t>(std::clamp(region, 0, lastRegion)));
    spec.inOrbit = inOrbit;
    spec.to =
        orbit::kAllPlanets.at(static_cast<std::size_t>(std::clamp(destination, 0, lastPlanet)));
    startPuzzle(spec, QRandomGenerator::global()->generate64());
}

void GameWindow::acceptCard() {
    if (m_scene) m_scene->setProperty("cardMode", QString());
}

void GameWindow::startPuzzle(const orbit::PuzzleSpec& spec, std::uint64_t seed) {
    const orbit::PuzzleSpec settled = orbit::settledSpec(spec);
    setFlying(false);
    m_endingShown = false;
    m_welcomePending = true;
    if (m_scene) {
        // The start card remembers the choice for next time.
        m_scene->setProperty("cardMode", QString());
        m_scene->setProperty("regionIndex", static_cast<int>(settled.from));
        m_scene->setProperty("inOrbit", settled.inOrbit);
        m_scene->setProperty("destinationIndex", static_cast<int>(settled.to));
    }
    m_session->newPuzzle(settled, seed, epochNow());
}

void GameWindow::onPuzzleChanged() {
    const orbit::Puzzle& puzzle = m_session->puzzle();
    const orbit::Voyage& v = puzzle.voyage;
    m_planner->setEnabled(true);
    if (m_scene) {
        m_scene->setProperty("goalText", goalText(v.goal, v.destination));
        m_scene->setProperty("destinationIndex", static_cast<int>(v.destination));
    }
    // "Centre" offers the destination too, when it isn't the Earth.
    while (m_centre->count() > 3) m_centre->removeItem(3);
    if (v.destination != orbit::Planet::Earth) {
        m_centre->addItem(tr("Centre: %1").arg(planetText(v.destination)));
    }
    updatePlanetOrbits();
    updateStartOrbit();
    m_centre->setCurrentIndex(0);
    resetView();
    // A clock that flies the way home in a minute and a half or so: the
    // slowest speed that does, and not slower than 20 days a second.
    const double tripDays = (puzzle.plan.arrival - v.startEpoch) / kDay;
    for (int i = 0; i < m_speed->count(); ++i) {
        const double daysPerSecond = m_speed->itemData(i).toDouble();
        m_speed->setCurrentIndex(i);
        if (daysPerSecond >= 20.0 && tripDays / daysPerSecond <= 90.0) break;
    }

    if (m_welcomePending) {
        m_welcomePending = false;
        showCard(puzzle.destination == orbit::Planet::Earth ? tr("Lost!") : tr("Ready to go"),
                 welcomeText(puzzle), tr("Let's go"));
    }
}

/// Where you are, what you have, and what to do.
QString GameWindow::welcomeText(const orbit::Puzzle& puzzle) {
    const orbit::Voyage& v = puzzle.voyage;
    const QString to = planetText(v.destination);
    const double fromTarget =
        orbit::norm(v.position - orbit::planetPosition(v.destination, v.startEpoch));
    QString where;
    if (const auto parked = orbit::parkedAround(v.startEpoch, v.position, v.velocity)) {
        // In orbit: how high, and how long once round.
        const orbit::Planet home = *parked;
        const orbit::Vec3 rel = v.position - orbit::planetPosition(home, v.startEpoch);
        const orbit::Vec3 relV = v.velocity - orbit::planetVelocity(home, v.startEpoch);
        const double radius = orbit::planetRadius(home);
        const orbit::OrbitalElements el =
            orbit::elementsFromState(rel, relV, orbit::planetMu(home), radius);
        where = el.apoapsis < 1.05 * el.periapsis
                    ? tr("You're in orbit around %1, %2 up, going round every %3.")
                          .arg(planetText(home), display::longDistance(el.periapsis - radius),
                               periodText(el.period))
                    : tr("You're in a long orbit around %1, from %2 to %3 above it, going "
                         "round every %4.")
                          .arg(planetText(home), display::longDistance(el.periapsis - radius),
                               display::longDistance(el.apoapsis - radius), periodText(el.period));
        where += QLatin1Char(' ') +
                 tr("%1 is %2 away. To leave, burn at the right point in your orbit: deep in "
                    "a planet's gravity, a little extra speed becomes a lot once you're out "
                    "(the Oberth effect).")
                     .arg(to, display::longDistance(fromTarget));
    } else {
        const orbit::OrbitalElements el =
            orbit::elementsFromState(v.position, v.velocity, orbit::kMuSun, orbit::kRadiusSun);
        const QString around = el.period > 2.0 * 365.25 * kDay
                                   ? tr("%1 to go round (the Earth's takes one)")
                                   : tr("%1 to go round (the Earth's takes 365 days)");
        where =
            tr("You're %1, %2 from the Sun and %3 from %4, on an orbit that takes %5.")
                .arg(regionPhrase(puzzle.region), display::longDistance(orbit::norm(v.position)),
                     display::longDistance(fromTarget), to, around.arg(periodText(el.period)));
    }
    return where + QLatin1Char(' ') +
           tr("You have <b>%1</b> of Δv in the tanks: that's how much you can change your "
              "speed, in all.<br><br><b>Goal:</b> %2<br><br>Add a burn on the right and watch "
              "your path change, or press <b>Hint</b> if you're stuck. Then <b>Fly</b>.")
               .arg(display::speed(puzzle.budget), goalText(v.goal, v.destination));
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
    auto* edit = new QDateTimeEdit(display::dateTime(m_session->now()), &dialog);
    edit->setObjectName(QStringLiteral("edit-date"));
    edit->setCalendarPopup(true);
    edit->setTimeZone(QTimeZone::UTC);
    edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'"));
    edit->setDateTimeRange(display::dateTime(m_session->startEpoch()), display::dateTime(end));
    form->addRow(tr("Date and time:"), edit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    m_endingShown = false;
    m_session->setNow(display::epochFromDateTime(edit->dateTime()));
}

/// What the hover card says about each object, at this moment. The numbers
/// are the ones you plan burns with: speeds (relative to the Sun, and to you)
/// and how far away things are.
/// The closest the predicted path comes to a planet, for its hover card.
QString GameWindow::closestPassRow(orbit::Planet planet) const {
    const auto* prediction = m_session->prediction();
    if (!prediction) return {};
    const orbit::PlanetPass& pass = prediction->closestPasses.at(static_cast<std::size_t>(planet));
    if (!pass.valid) return {};
    return infoRow(tr("Your closest pass"), tr("%1, %2").arg(display::longDistance(pass.distance),
                                                             display::date(pass.epoch)));
}

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
                infoRow(tr("Orbit period"),
                        periodText(keplerPeriod(orbit::planetOrbit(planet, now).semiMajorAxis,
                                                orbit::kMuSun))) +
                infoRow(tr("Mass"), bigMass(orbit::planetMu(planet) / kG)) +
                infoRow(tr("Radius"), display::distance(orbit::planetRadius(planet))) +
                infoRow(tr("Distance from you"), display::longDistance(orbit::norm(r - ship.r))) +
                infoRow(tr("Speed vs you"), display::speed(orbit::norm(v - ship.v))) +
                closestPassRow(planet)));
    }
    texts.append(card(tr("Sun"), infoRow(tr("Mass"), bigMass(kSunMass)) +
                                     infoRow(tr("Radius"), display::distance(orbit::kRadiusSun)) +
                                     infoRow(tr("Distance from you"),
                                             display::longDistance(orbit::norm(ship.r)))));
    const orbit::Vec3 earthR = orbit::planetPosition(orbit::Planet::Earth, now);
    const orbit::Vec3 earthV = orbit::planetVelocity(orbit::Planet::Earth, now);
    // The ship's orbit as it is now (where it would go with no more burns),
    // around the body whose pull dominates: a planet inside its sphere of
    // influence, otherwise the Sun.
    const auto near = orbit::dominantPlanet(now, ship.r);
    const orbit::OrbitalElements shipOrbit =
        near ? orbit::elementsFromState(ship.r - orbit::planetPosition(*near, now),
                                        ship.v - orbit::planetVelocity(*near, now),
                                        orbit::planetMu(*near), orbit::planetRadius(*near))
             : orbit::elementsFromState(ship.r, ship.v, orbit::kMuSun, orbit::kRadiusSun);
    const orbit::Planet destination = m_session->puzzle().voyage.destination;
    QString rows =
        infoRow(tr("Speed (Sun)"), display::speed(orbit::norm(ship.v))) +
        infoRow(tr("Speed (Earth)"), display::speed(orbit::norm(ship.v - earthV))) +
        infoRow(tr("Distance from Sun"), display::longDistance(orbit::norm(ship.r))) +
        infoRow(tr("Distance from Earth"), display::longDistance(orbit::norm(ship.r - earthR))) +
        (destination == orbit::Planet::Earth
             ? QString()
             : infoRow(tr("Distance from %1").arg(planetText(destination)),
                       display::longDistance(
                           orbit::norm(ship.r - orbit::planetPosition(destination, now))))) +
        infoRow(near ? tr("Orbit period (%1)").arg(planetText(*near)) : tr("Orbit period"),
                shipOrbit.bound ? periodText(shipOrbit.period) : tr("none: escaping")) +
        infoRow(tr("Mass"), display::mass(ship.mass));
    if (prediction) {
        rows += infoRow(tr("Δv left after burns"), display::speed(prediction->deltaVLeft));
    }
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
/// and the Earth's (and, with `wholePath`, all of the predicted path).
void GameWindow::resetView(bool wholePath) {
    if (!m_scene) return;
    double reach = 1.2 * orbit::kAstronomicalUnit;
    if (m_session->hasPuzzle()) {
        const orbit::Voyage& v = m_session->puzzle().voyage;
        const orbit::OrbitalElements el =
            orbit::elementsFromState(v.position, v.velocity, orbit::kMuSun, orbit::kRadiusSun);
        reach = std::max(reach, el.bound ? el.apoapsis : orbit::norm(v.position));
    }
    if (const auto* p = m_session->prediction(); wholePath && p) {
        for (const orbit::VoyageSample& s : p->samples) reach = std::max(reach, orbit::norm(s.r));
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
    if (m_framePending && m_session->predictionCurrent()) {
        m_framePending = false;
        m_centre->setCurrentIndex(0);
        resetView(true);
    }
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
        // Parked in orbit, the samples are a day apart: draw one turn of
        // the orbit instead, then go on from the end of the stretch.
        if (samples[i].parked && (i == 0 || samples[i - 1].parked != samples[i].parked)) {
            std::size_t last = i;
            while (last + 1 < samples.size() && samples[last + 1].parked == samples[i].parked) {
                ++last;
            }
            const orbit::Planet planet = samples[i].parked.value_or(orbit::Planet::Earth);
            const orbit::Vec3 rel = samples[i].r - orbit::planetPosition(planet, samples[i].epoch);
            const orbit::Vec3 relV = samples[i].v - orbit::planetVelocity(planet, samples[i].epoch);
            const double period = orbit::elementsFromState(rel, relV, orbit::planetMu(planet),
                                                           orbit::planetRadius(planet))
                                      .period;
            const double end =
                last + 1 < samples.size() ? samples[last + 1].epoch : samples[last].epoch;
            const double span = std::min(period, end - samples[i].epoch);
            const QColor color =
                i < split ? kFlownColor : kSegmentColors.at(burn % kSegmentColors.size());
            constexpr int kTurn = 96;
            for (int k = 0; k <= kTurn; ++k) {
                const double t = samples[i].epoch + span * k / kTurn;
                points.append(scene::fromHeliocentric(orbit::sampleAt(*prediction, t).r));
                colors.append(color);
            }
            i = last;
            continue;
        }
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
            QVariant::fromValue(scene::fromHeliocentric(orbit::sampleAt(*prediction, b.epoch).r)));
        fired.append(m_session->isLocked(i));
    }
    m_scene->setProperty("burnPositions", positions);
    m_scene->setProperty("burnFired", fired);

    // What hovering a burn shows. Directions are relative to the Sun, or to
    // the Earth inside its sphere of influence (as the report says).
    QVariantList burnTexts;
    for (int i = 0; i < m_session->burnCount(); ++i) {
        const orbit::Burn& b = m_session->burns()[static_cast<std::size_t>(i)];
        const auto index = static_cast<std::size_t>(i);
        const orbit::VoyageSample at = orbit::sampleAt(*prediction, b.epoch);
        QString rows = infoRow(tr("When"), display::date(b.epoch)) +
                       infoRow(tr("Size"), display::speed(orbit::burnSize(b))) +
                       infoRow(tr("Prograde"), display::speed(b.prograde)) +
                       infoRow(tr("Outward"), display::speed(b.radial)) +
                       infoRow(tr("North"), display::speed(b.normal));
        if (index < prediction->burns.size()) {
            const orbit::BurnReport& report = prediction->burns[index];
            rows += infoRow(tr("Directions vs"),
                            report.planet ? planetText(*report.planet) : tr("Sun"));
            if (report.shortOfFuel) {
                rows +=
                    infoRow(tr("Given"), tr("%1 (out of fuel)").arg(display::speed(report.deltaV)));
            }
        }
        rows += infoRow(tr("Speed before (Sun)"), display::speed(orbit::norm(at.v)));
        burnTexts.append(QStringLiteral("<b>%1</b><table>%2</table>")
                             .arg(m_session->isLocked(i) ? tr("Burn %1 (done)").arg(i + 1)
                                                         : tr("Burn %1").arg(i + 1),
                                  rows));
    }
    m_scene->setProperty("burnInfoTexts", burnTexts);
    m_scene->setProperty("selectedBurn", m_session->selected());

    // Flybys: each pass inside a planet's sphere of influence, with what it
    // did to the speed around the Sun (the closest pass by the Earth is the
    // arrival, shown on its own below).
    // The closest approach to the destination.
    const orbit::Planet to = m_session->puzzle().voyage.destination;
    const orbit::PlanetPass& c = prediction->closestPasses.at(static_cast<std::size_t>(to));
    const QVariantList names = m_scene->property("planetNames").toList();
    QVariantList flybyPositions;
    QVariantList flybyLabels;
    QVariantList flybyTexts;
    for (const orbit::PlanetPass& pass : prediction->flybys) {
        if (pass.planet == to) continue; // the arrival, shown on its own below
        const QString name = names.value(static_cast<int>(pass.planet)).toString();
        const double gain = pass.sunSpeedAfter - pass.sunSpeedBefore;
        const double radius = orbit::planetRadius(pass.planet);
        flybyPositions.append(QVariant::fromValue(scene::fromHeliocentric(
            orbit::planetPosition(pass.planet, pass.epoch) + pass.relativePosition)));
        flybyLabels.append(tr("%1 flyby, %2").arg(name, signedSpeed(gain)));
        QString rows =
            infoRow(tr("When"), display::date(pass.epoch)) +
            infoRow(tr("Closest"), tr("%1 from its centre (%2 radii)")
                                       .arg(display::longDistance(pass.distance))
                                       .arg(QString::number(pass.distance / radius, 'f', 1))) +
            infoRow(tr("Speed vs %1").arg(name), display::speed(pass.speed));
        if (pass.vInfinity > 0.0) {
            rows += infoRow(tr("Far from it (v∞)"), display::speed(pass.vInfinity)) +
                    infoRow(tr("Path turned by"), display::angle(pass.turnAngle));
        }
        rows += infoRow(tr("Speed (Sun) before"), display::speed(pass.sunSpeedBefore)) +
                infoRow(tr("Speed (Sun) after"), display::speed(pass.sunSpeedAfter)) +
                infoRow(tr("Gained for free"), signedSpeed(gain));
        flybyTexts.append(
            QStringLiteral("<b>%1</b><table>%2</table>").arg(tr("%1 flyby").arg(name), rows));
    }
    m_scene->setProperty("flybyPositions", flybyPositions);
    m_scene->setProperty("flybyLabels", flybyLabels);
    m_scene->setProperty("flybyInfoTexts", flybyTexts);

    const bool show = c.valid && c.epoch > m_session->now();
    m_scene->setProperty("arrivalVisible", show);
    if (show) {
        const orbit::Vec3 target = orbit::planetPosition(to, c.epoch);
        const QVector3D ship = scene::fromHeliocentric(target + c.relativePosition);
        m_scene->setProperty("arrivalShip", ship);
        m_scene->setProperty("arrivalEarth", scene::fromHeliocentric(target));
        // Close in, how high above the ground matters; far out, how far.
        const bool near = c.distance < orbit::sphereOfInfluence(to, c.epoch);
        const QString name = planetText(to);
        m_scene->setProperty(
            "arrivalLabel",
            near ? tr("closest to %1: %2 up, %3")
                       .arg(name, display::longDistance(c.distance - orbit::planetRadius(to)),
                            display::date(c.epoch))
                 : tr("closest to %1: %2 away, %3")
                       .arg(name, display::longDistance(c.distance), display::date(c.epoch)));
        if (m_arrivalLine) {
            const QColor color(0x93, 0xc5, 0xfd);
            m_arrivalLine->setPoints({ship, scene::fromHeliocentric(target)}, {color, color});
        }
    }
}

/// What the predicted path comes to, in the top-right corner.
void GameWindow::updateStatus() {
    const auto* p = m_session->prediction();
    if (!m_scene || !p) return;
    using orbit::VoyageEnd;
    const orbit::Goal goal = m_session->puzzle().voyage.goal;
    const orbit::Planet to = m_session->puzzle().voyage.destination;
    const QString name = planetText(to);
    const orbit::PlanetPass& pass = p->closestPasses.at(static_cast<std::size_t>(to));
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
        case orbit::Goal::ReachPlanet:
            text = tr("<b>On course!</b> You reach %1 on %2.").arg(name, when);
            break;
        case orbit::Goal::ClosePass:
            text = tr("<b>On course!</b> You pass %1 above %2 on %3.")
                       .arg(display::longDistance(pass.distance - orbit::planetRadius(to)), name,
                            when);
            break;
        case orbit::Goal::PlanetOrbit:
            text = tr("<b>On course!</b> You settle into orbit around %1 on %2.").arg(name, when);
            break;
        }
    } else if (p->end == VoyageEnd::TooSteep || p->end == VoyageEnd::TooFast ||
               p->end == VoyageEnd::Skipped || p->end == VoyageEnd::HitSun ||
               p->end == VoyageEnd::HitPlanet || p->end == VoyageEnd::Reentered) {
        color = QColor(0xfc, 0xa5, 0xa5);
        text = tr("<b>Trouble ahead:</b> %1 on %2.").arg(endText(*p, to).toLower(), when);
        if (p->entrySpeed > 0.0) {
            text += QLatin1Char(' ') +
                    tr("You'd hit the air at %1, %2 below level.")
                        .arg(display::speed(p->entrySpeed), display::angle(-p->entryAngle));
        }
    } else if (p->end == VoyageEnd::Captured && to == orbit::Planet::Earth) {
        color = QColor(0xfd, 0xe6, 0x8a);
        text = tr("You'd end up in orbit around the Earth. To land, add a burn that lowers "
                  "your lowest point into the air.");
    } else if (const auto parked = p->samples.empty() ? std::nullopt : p->samples.back().parked) {
        text = tr("Still in orbit around %1. Add a burn to leave.").arg(planetText(*parked));
    } else if (pass.valid) {
        const double altitude = pass.distance - orbit::planetRadius(to);
        const QString day = display::date(pass.epoch);
        if (pass.distance < orbit::sphereOfInfluence(to, p->endEpoch)) {
            text = tr("You pass <b>%1 above %2</b> on %3: close!")
                       .arg(display::longDistance(altitude), name, day);
            if (goal == orbit::Goal::EarthOrbit || goal == orbit::Goal::PlanetOrbit) {
                text += QLatin1Char(' ') + tr("Brake there to stay.");
            } else if (goal == orbit::Goal::ClosePass) {
                text += QLatin1Char(' ') + tr("Closer than %1 of its radii counts.")
                                               .arg(orbit::VoyageOptions{}.closePassRadii);
            }
        } else {
            text = tr("Closest to %1: <b>%2</b> away, on %3.")
                       .arg(name, display::longDistance(pass.distance), day);
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
    const orbit::VoyageSample ship = orbit::sampleAt(*prediction, now);
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
        // Against the direct way home worked out at the start: a good
        // gravity assist beats it.
        const double used = budget - p->deltaVLeft;
        const double direct = m_session->puzzle().plan.total();
        QString compare = tr("The direct way, worked out at the start, needed about %1; "
                             "you used %2.")
                              .arg(display::speed(direct), display::speed(used));
        if (used < 0.95 * direct) compare += QLatin1Char(' ') + tr("<b>You beat it!</b>");
        QStringList assists;
        const QVariantList names = m_scene->property("planetNames").toList();
        for (const orbit::PlanetPass& pass : p->flybys) {
            if (pass.planet == m_session->puzzle().voyage.destination || pass.epoch > p->endEpoch) {
                continue;
            }
            assists.append(
                tr("%1 (%2)").arg(names.value(static_cast<int>(pass.planet)).toString(),
                                  signedSpeed(pass.sunSpeedAfter - pass.sunSpeedBefore)));
        }
        if (!assists.isEmpty()) {
            compare += QStringLiteral("<br><br>") +
                       tr("Gravity assists: %1.").arg(assists.join(QStringLiteral(", ")));
        }
        showCard(m_session->puzzle().voyage.destination == orbit::Planet::Earth ? tr("Home!")
                                                                                : tr("Made it!"),
                 tr("You made it in %1 days, with %2 of Δv left (%3 % of what you had). "
                    "Hints used: %4.")
                         .arg(std::lround(days))
                         .arg(display::speed(p->deltaVLeft))
                         .arg(std::lround(100.0 * p->deltaVLeft / budget))
                         .arg(m_session->hintsUsed()) +
                     QStringLiteral("<br><br>") + compare,
                 tr("OK"));
    } else {
        showCard(endingTitle(p->end),
                 tr("%1 after %2 days. Press <b>Restart</b> to go back to the beginning and "
                    "change your burns, or start a <b>New game</b>.")
                     .arg(endText(*p, m_session->puzzle().voyage.destination))
                     .arg(std::lround(days)),
                 tr("OK"));
    }
}
