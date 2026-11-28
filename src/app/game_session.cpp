// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_session.hpp"

#include "display_format.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/moon_transfer.hpp"
#include "orbit/core/solar_system.hpp"

#include <QCoreApplication>
#include <QThread>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr double kDay = 86400.0;

/// The hint looks for a way home at least this far ahead, with trips at
/// least this long (further out, longer: orbit::searchSpan).
constexpr double kHintWindow = 1.5 * 365.25 * kDay;
constexpr double kHintLongestFlight = 700.0 * kDay;
/// Course corrections go this long before arriving.
constexpr double kCorrectionLead = 20.0 * kDay;

QString planetText(orbit::Body planet) { return QString::fromUtf8(orbit::bodyName(planet).data()); }

QString goalPhrase(orbit::Goal goal, orbit::Body destination) {
    const QString name = display::bodyInText(destination);
    switch (goal) {
    case orbit::Goal::ReachEarth:
        return QObject::tr("reaching the Earth");
    case orbit::Goal::Reentry:
        return QObject::tr("the re-entry corridor");
    case orbit::Goal::EarthOrbit:
        return QObject::tr("a pass just above the air");
    case orbit::Goal::ReachPlanet:
        return QObject::tr("reaching %1").arg(name);
    case orbit::Goal::ClosePass:
        return QObject::tr("a close pass by %1").arg(name);
    case orbit::Goal::PlanetOrbit:
        return QObject::tr("a pass just above %1").arg(name);
    case orbit::Goal::Explore:
        break;
    }
    return {};
}

/// Where the burn at `epoch` (just added or changed) sits in the sorted list.
int indexOfEpoch(std::vector<orbit::Burn>& burns, double epoch) {
    std::ranges::stable_sort(burns, {}, &orbit::Burn::epoch);
    const auto it =
        std::ranges::find_if(burns, [&](const orbit::Burn& b) { return b.epoch == epoch; });
    return static_cast<int>(it - burns.begin());
}

} // namespace

GameSession::GameSession(QObject* parent) : QObject(parent) {
    connect(&m_predictWatcher, &QFutureWatcherBase::finished, this, &GameSession::onPredicted);
    connect(&m_puzzleWatcher, &QFutureWatcherBase::finished, this, &GameSession::onPuzzleMade);
    connect(&m_hintWatcher, &QFutureWatcherBase::finished, this, &GameSession::onHintMade);
}

GameSession::~GameSession() {
    // Don't leave worker threads running into a destroyed object.
    m_puzzleWatcher.waitForFinished();
    m_hintWatcher.waitForFinished();
    m_predictWatcher.waitForFinished();
}

void GameSession::waitForIdle() const {
    // Each finished signal may start more work (a pending prediction), so
    // keep delivering them until all is quiet.
    // (A worker can be done while its finished signal is still queued: the
    // flags below are cleared only once the result has been taken in.)
    while (m_makingPuzzle || m_makingHint || (m_hasPuzzle && !m_predictionCurrent)) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
}

bool GameSession::isBusy() const { return m_makingPuzzle || m_makingHint; }

void GameSession::newPuzzle(const orbit::PuzzleSpec& spec, std::uint64_t seed, double epoch) {
    if (m_makingPuzzle) return;
    m_makingPuzzle = true;
    m_puzzleWatcher.setFuture(
        QtConcurrent::run([=] { return orbit::makePuzzle(spec, seed, epoch); }));
    emit busyChanged(true);
}

void GameSession::onPuzzleMade() {
    m_makingPuzzle = false;
    setPuzzle(m_puzzleWatcher.result());
    emit busyChanged(isBusy());
}

void GameSession::setPuzzle(const orbit::Puzzle& puzzle) {
    m_puzzle = puzzle;
    m_reach = 0.0;
    m_hasPuzzle = true;
    m_burns.clear();
    m_selected = -1;
    m_hintsUsed = 0;
    m_now = m_puzzle.voyage.startEpoch;
    m_prediction.reset();
    emit puzzleChanged();
    emit burnsChanged();
    emit selectionChanged(m_selected);
    emit nowChanged(m_now);
    predict();
}

void GameSession::newFreeRoam(orbit::Body body, double epoch) {
    if (m_makingPuzzle) return;
    // Quick (no transfer to search for): straight away.
    setPuzzle(orbit::freeRoam(body, epoch));
}

void GameSession::setNow(double epoch) {
    if (!m_hasPuzzle) return;
    epoch = std::max(epoch, startEpoch());
    // Not past the end of a flight that ends (arrives, or hits something).
    if (m_prediction && m_prediction->end != orbit::VoyageEnd::Horizon) {
        epoch = std::min(epoch, m_prediction->endEpoch);
    }
    if (epoch == m_now) return;
    m_now = epoch;
    // Free roam goes on for as long as the player likes: past half of the
    // look-ahead left, look a whole one ahead of the clock (once per step).
    if (isFreeRoam() && m_prediction && m_prediction->end == orbit::VoyageEnd::Horizon) {
        const double horizon = m_puzzle.voyage.horizon;
        if (m_prediction->endEpoch < m_now + 0.5 * horizon && m_reach < m_now + 0.75 * horizon) {
            m_reach = m_now + horizon;
            predict();
        }
    }
    emit nowChanged(m_now);
}

void GameSession::restart() {
    if (!m_hasPuzzle) return;
    m_now = startEpoch();
    emit nowChanged(m_now);
}

bool GameSession::isLocked(int index) const {
    return index >= 0 && index < burnCount() &&
           m_burns[static_cast<std::size_t>(index)].epoch < m_now;
}

void GameSession::select(int index) {
    if (index < -1 || index >= burnCount()) index = -1;
    if (index == m_selected) return;
    m_selected = index;
    emit selectionChanged(m_selected);
}

int GameSession::addBurn(double epoch) {
    if (!m_hasPuzzle) return -1;
    m_burns.push_back({std::max(epoch, m_now), 0.0, 0.0, 0.0});
    sortBurns(burnCount() - 1);
    emit burnsChanged();
    emit selectionChanged(m_selected);
    predict();
    return m_selected;
}

void GameSession::setBurn(int index, const orbit::Burn& burn) {
    if (index < 0 || index >= burnCount() || isLocked(index)) return;
    orbit::Burn changed = burn;
    changed.epoch = std::max(changed.epoch, m_now);
    m_burns[static_cast<std::size_t>(index)] = changed;
    const int before = m_selected;
    sortBurns(index);
    emit burnsChanged();
    if (m_selected != before) emit selectionChanged(m_selected);
    predict();
}

void GameSession::removeBurn(int index) {
    if (index < 0 || index >= burnCount() || isLocked(index)) return;
    m_burns.erase(m_burns.begin() + index);
    m_selected = std::min(index, burnCount() - 1);
    if (isLocked(m_selected)) m_selected = -1;
    emit burnsChanged();
    emit selectionChanged(m_selected);
    predict();
}

/// Keep the burns in time order, with the selection on burn `follow`.
void GameSession::sortBurns(int follow) {
    std::vector<std::pair<orbit::Burn, bool>> tagged;
    tagged.reserve(m_burns.size());
    for (std::size_t i = 0; i < m_burns.size(); ++i) {
        tagged.emplace_back(m_burns[i], std::cmp_equal(i, follow));
    }
    std::ranges::stable_sort(tagged, {}, [](const auto& t) { return t.first.epoch; });
    m_selected = -1;
    for (std::size_t i = 0; i < tagged.size(); ++i) {
        m_burns[i] = tagged[i].first;
        if (tagged[i].second) m_selected = static_cast<int>(i);
    }
}

orbit::Puzzle GameSession::workingPuzzle() const {
    orbit::Puzzle puzzle = m_puzzle;
    // Look-ahead counts from the last burn.
    double last = puzzle.voyage.startEpoch;
    for (const orbit::Burn& b : m_burns) last = std::max(last, b.epoch);
    puzzle.voyage.lookAhead = std::max(puzzle.voyage.lookAhead, m_reach - last);
    return puzzle;
}

orbit::Voyage GameSession::voyage() const {
    orbit::Voyage v = workingPuzzle().voyage;
    v.burns = m_burns;
    return v;
}

void GameSession::predict() {
    if (!m_hasPuzzle) return;
    m_predictionCurrent = false;
    if (m_predictWatcher.isRunning()) {
        m_predictPending = true;
        return;
    }
    m_predictPending = false;
    m_predictWatcher.setFuture(QtConcurrent::run([v = voyage()] {
        return std::make_shared<const orbit::VoyageResult>(orbit::flyVoyage(v));
    }));
}

void GameSession::onPredicted() {
    m_prediction = m_predictWatcher.result();
    if (m_predictPending) {
        predict();
    } else {
        m_predictionCurrent = true;
    }
    // A shorter flight may now end before the clock.
    if (m_prediction->end != orbit::VoyageEnd::Horizon && m_now > m_prediction->endEpoch) {
        m_now = m_prediction->endEpoch;
        emit nowChanged(m_now);
    }
    emit predictionChanged();
}

void GameSession::requestHint() {
    // Free roam has no goal to hint at.
    if (!m_hasPuzzle || m_makingHint || isFreeRoam()) return;
    m_makingHint = true;
    m_hintWatcher.setFuture(
        QtConcurrent::run([puzzle = workingPuzzle(), burns = m_burns, now = m_now] {
            return computeHint(puzzle, burns, now);
        }));
    emit busyChanged(true);
}

void GameSession::aimBurn(int index, const orbit::FlybyAim& aim) {
    if (!m_hasPuzzle || m_makingHint || index < 0 || index >= burnCount() || isLocked(index)) {
        return;
    }
    m_makingHint = true;
    m_hintWatcher.setFuture(QtConcurrent::run([puzzle = m_puzzle, burns = m_burns, index, aim] {
        return computeAim(puzzle, burns, index, aim);
    }));
    emit busyChanged(true);
}

GameSession::Hint GameSession::computeAim(const orbit::Puzzle& puzzle,
                                          const std::vector<orbit::Burn>& burns, int index,
                                          const orbit::FlybyAim& aim) {
    using namespace orbit;
    Voyage voyage = puzzle.voyage;
    voyage.burns = burns;
    const auto i = static_cast<std::size_t>(index);
    const QString name = planetText(aim.planet);
    const FlybyAimResult result = aimFlyby(voyage, i, aim);
    if (!result.burn) {
        if (result.outOfReach) {
            return {{},
                    -1,
                    tr("Reaching %1 from where burn %2 is needs at least %3, more than your "
                       "tanks hold by then. Try another date: the planets line up better at "
                       "some times than others.")
                        .arg(name)
                        .arg(index + 1)
                        .arg(display::speed(result.cheapest)),
                    false};
        }
        return {{},
                -1,
                tr("That pass by %1 couldn't be lined up from burn %2. Try a wider pass, "
                   "another side, or moving the burn.")
                    .arg(name)
                    .arg(index + 1),
                false};
    }
    // (Out of a parked orbit, the burn may move to the best point in it.)
    voyage.burns[i] = *result.burn;
    const int at = indexOfEpoch(voyage.burns, result.burn->epoch);
    // Predictions from now on reach past the pass, whatever the look-ahead.
    const double reach = result.passEpoch + 365.25 * kDay;
    double last = voyage.startEpoch;
    for (const Burn& b : voyage.burns) last = std::max(last, b.epoch);
    voyage.lookAhead = std::max(voyage.lookAhead, reach - last);
    const VoyageResult flown = flyVoyage(voyage);
    QString message =
        tr("Burn %1 is now %2, on %3.")
            .arg(at + 1)
            .arg(display::speed(burnSize(*result.burn)), display::date(result.burn->epoch));
    if (const auto pass = passAfter(flown, aim.planet, result.burn->epoch)) {
        message +=
            QLatin1Char(' ') +
            tr("You pass %1 on %2, %3 from its centre (%4 times its radius). Around "
               "the Sun you go from %5 to %6.")
                .arg(name, display::date(pass->epoch), display::longDistance(pass->distance))
                .arg(QString::number(pass->distance / bodyRadius(aim.planet), 'f', 1))
                .arg(display::speed(pass->sunSpeedBefore), display::speed(pass->sunSpeedAfter));
    }
    return {voyage.burns, at, message, false, reach};
}

void GameSession::onHintMade() {
    const Hint hint = m_hintWatcher.result();
    m_makingHint = false;
    emit busyChanged(isBusy());
    if (!hint.burns.empty()) {
        if (hint.counts) ++m_hintsUsed;
        m_reach = std::max(m_reach, hint.reach);
        m_burns = hint.burns;
        m_selected = hint.select;
        emit burnsChanged();
        emit selectionChanged(m_selected);
        predict();
    }
    if (hint.counts) {
        emit hintReady(hint.message);
    } else {
        emit aimReady(!hint.burns.empty(), hint.message);
    }
}

std::optional<GameSession::Hint>
GameSession::hintFromMoon(orbit::Voyage voyage, std::size_t planned, orbit::Body moon, double now) {
    using namespace orbit;
    const auto leg = planFromMoon(voyage, now);
    if (!leg) return std::nullopt;
    std::erase_if(voyage.burns, [&](const Burn& b) { return b.epoch >= leg->from; });
    const bool replaced = voyage.burns.size() < planned;
    voyage.burns.push_back(leg->burn);
    auto index = static_cast<std::size_t>(indexOfEpoch(voyage.burns, leg->burn.epoch));
    if (leg->correction) {
        voyage.burns.push_back(*leg->correction);
        index = static_cast<std::size_t>(indexOfEpoch(voyage.burns, leg->burn.epoch));
    } else if (const auto tuned = aimAtDestination(voyage, index)) {
        voyage.burns[index] = *tuned;
        index = static_cast<std::size_t>(indexOfEpoch(voyage.burns, tuned->epoch));
    }
    const Burn& first = voyage.burns[index];
    const Body to = voyage.destination;
    const Body planet = planetOf(moon);
    const QString from = display::bodyInText(moon);
    const QString when = display::date(first.epoch);
    const QString size = display::speed(burnSize(first));
    QString message;
    if (to == planet) {
        message = tr("Slow down to fall home: leave %1 on %2 with a burn of %3 at the right point "
                     "in your orbit. You leave %1 backwards, slower than it goes round %4, and "
                     "fall towards %4 for %5, meeting the air at just the right angle.")
                      .arg(from, when, size, display::bodyInText(planet),
                           display::duration(leg->plan.arrival - first.epoch));
    } else if (isMoon(to) && planetOf(to) == planet) {
        message =
            tr("%1 goes round %2 too. The cheapest way to meet it leaves %3 on %4 with a "
               "burn of %5 and gets there on %6, arriving at %7 relative to it.")
                .arg(display::bodyInText(to, true), display::bodyInText(planet), from, when, size,
                     display::date(leg->plan.arrival), display::speed(leg->plan.vInfinity));
    } else {
        message = tr("Leaving %1 takes two climbs: out of %1's gravity, then out of %2's. Burn "
                     "%3 on %4, at the right point in your orbit and with %1 in the right place "
                     "round %2, and you arrive on %5.")
                      .arg(from, display::bodyInText(planet), size, when,
                           display::date(leg->plan.arrival));
        if (leg->correction) {
            message += QLatin1Char(' ') + tr("A small correction of %1 on %2, once clear of %3, "
                                             "lines you up.")
                                              .arg(display::speed(burnSize(*leg->correction)),
                                                   display::date(leg->correction->epoch),
                                                   display::bodyInText(planet));
        }
        if (isMoon(to)) {
            message += QLatin1Char(' ') +
                       tr("That takes you to %1, crossing %2's orbit: once "
                          "you're near, press Hint again to meet %2 itself.")
                           .arg(display::bodyInText(planetOf(to)), display::bodyInText(to));
        }
    }
    if (replaced) message += QLatin1Char(' ') + tr("(It replaces the burns you had planned.)");
    return Hint{voyage.burns, static_cast<int>(index), message};
}

/// The hint: plan the way there if the path doesn't reach the destination;
/// aim it if it does but misses the goal; brake if it would come in too
/// fast to land; brake at the low point to stay, on Hard.
GameSession::Hint GameSession::computeHint(const orbit::Puzzle& puzzle,
                                           const std::vector<orbit::Burn>& burns, double now) {
    using namespace orbit;
    Voyage voyage = puzzle.voyage;
    voyage.burns = burns;
    const Body to = voyage.destination;
    const QString name = planetText(to);
    const VoyageResult prediction = flyVoyage(voyage);
    if (prediction.goalMet) {
        return {{},
                -1,
                to == Body::Earth ? tr("You're on your way home already: just fly.")
                                  : tr("You're on your way already: just fly.")};
    }
    const PlanetPass& closest = prediction.closestPasses.at(static_cast<std::size_t>(to));
    const double soi = sphereOfInfluence(to, now);
    const bool reaches =
        prediction.arrival.has_value() || (closest.valid && closest.distance < soi);

    // Still in orbit around a moon: the way out of it (home to its planet,
    // that's the first thing to plan, wherever the path goes now).
    Voyage before = voyage;
    std::erase_if(before.burns, [&](const Burn& b) { return b.epoch >= now; });
    const VoyageSample start = voyageStateAt(before, now);
    const auto startMoon = parkedAround(now, start.r, start.v);
    if (startMoon && isMoon(*startMoon) && (!reaches || to == planetOf(*startMoon))) {
        if (auto hint = hintFromMoon(voyage, burns.size(), *startMoon, now)) return *hint;
    }

    if (!reaches && isMoon(to)) {
        // To a moon, once in (or on the way into) its planet's sphere of
        // influence: the last leg, inside the planet's system.
        if (const auto leg = planToMoon(voyage, now, prediction)) {
            std::erase_if(voyage.burns, [&](const Burn& b) { return b.epoch >= leg->from; });
            const bool replaced = voyage.burns.size() < burns.size();
            voyage.burns.push_back(leg->burn);
            auto index = static_cast<std::size_t>(indexOfEpoch(voyage.burns, leg->burn.epoch));
            if (const auto tuned = aimAtDestination(voyage, index)) {
                voyage.burns[index] = *tuned;
                index = static_cast<std::size_t>(indexOfEpoch(voyage.burns, tuned->epoch));
            }
            const Burn& first = voyage.burns[index];
            const QString planet = display::bodyInText(planetOf(to));
            QString message =
                tr("%1 goes round %2 every %3. The cheapest way to meet it leaves on %4 with a "
                   "burn of %5 and gets there on %6, arriving at %7 relative to it.")
                    .arg(display::bodyInText(to, true), planet,
                         display::duration(kTwoPi *
                                           std::sqrt(std::pow(moonOrbit(to).semiMajorAxis, 3) /
                                                     bodyMu(planetOf(to)))),
                         display::date(first.epoch), display::speed(burnSize(first)),
                         display::date(leg->plan.arrival), display::speed(leg->plan.vInfinity));
            if (replaced) {
                message += QLatin1Char(' ') + tr("(It replaces the burns you had planned.)");
            }
            return {voyage.burns, static_cast<int>(index), message};
        }
    }

    if (!reaches) {
        // Start again from here: the cheapest transfer from where the ship
        // is now (or from the planet it's parked around), replacing the
        // burns still to come.
        std::erase_if(voyage.burns, [&](const Burn& b) { return b.epoch >= now; });
        const bool replaced = voyage.burns.size() < burns.size();
        const VoyageSample here = voyageStateAt(voyage, now);
        DepartureFrom from;
        Vec3 r = here.r;
        Vec3 v = here.v;
        if (const auto parked = parkedAround(now, here.r, here.v)) {
            const Vec3 pr = bodyPosition(*parked, now);
            const Vec3 pv = bodyVelocity(*parked, now);
            const OrbitalElements el =
                elementsFromState(here.r - pr, here.v - pv, bodyMu(*parked), bodyRadius(*parked));
            // The orbit is set: price each departure from its best point.
            from = {parked,
                    {el.periapsis, el.apoapsis},
                    orbitPoints({here.r - pr, here.v - pv}, bodyMu(*parked), 72)};
            r = pr;
            v = pv;
        }
        const SearchSpan span = searchSpan(r, v, to);
        const double window = std::max(kHintWindow, span.window);
        const auto plan = cheapestTransfer(now, r, v, from, to, voyage.goal, window,
                                           std::max(kHintLongestFlight, span.longestFlight));
        if (!plan) {
            return {{},
                    -1,
                    tr("No way to %1 turns up from here in the next %2 months.")
                        .arg(name)
                        .arg(std::lround(window / (365.25 * kDay) * 12.0))};
        }
        const auto departure = transferBurn(voyage, plan->departure, plan->arrival);
        if (!departure) return {{}, -1, tr("No way to %1 turns up from here.").arg(name)};
        voyage.burns.push_back(*departure);
        auto index = static_cast<std::size_t>(indexOfEpoch(voyage.burns, departure->epoch));
        if (const auto tuned = aimAtDestination(voyage, index)) {
            voyage.burns[index] = *tuned;
            index = static_cast<std::size_t>(indexOfEpoch(voyage.burns, tuned->epoch));
        }
        const Burn& first = voyage.burns[index];
        QString message =
            from.planet
                ? tr("The cheapest way to %1 leaves your orbit on %2 with a burn of %3, at just "
                     "the right point in it, and arrives on %4. Fly there and it fires.")
                      .arg(name, display::date(first.epoch), display::speed(burnSize(first)),
                           display::date(plan->arrival))
                : tr("The cheapest way to %1 leaves on %2 with a burn of %3, and arrives on "
                     "%4. Fly there and it fires.")
                      .arg(name, display::date(first.epoch), display::speed(burnSize(first)),
                           display::date(plan->arrival));
        if (isMoon(to)) {
            message = tr("To get to %1, get to %2 first: the cheapest way leaves on %3 with a "
                         "burn of %4 and arrives on %5, crossing %1's orbit. Once you're near "
                         "%2, press Hint again to meet %1 itself.")
                          .arg(display::bodyInText(to), display::bodyInText(planetOf(to)),
                               display::date(first.epoch), display::speed(burnSize(first)),
                               display::date(plan->arrival));
        }
        if (replaced) message += QLatin1Char(' ') + tr("(It replaces the burns you had planned.)");
        return {voyage.burns, static_cast<int>(index), message};
    }

    // On Hard, once the pass is aimed just above the air (or the planet):
    // brake at the low point.
    if (voyage.goal == Goal::EarthOrbit && prediction.entrySpeed == 0.0) {
        const double altitude = prediction.closest.distance - kRadiusEarth;
        if (altitude >= 150e3 && altitude < 2000e3) {
            if (const auto brake = captureBurn(voyage)) {
                voyage.burns.push_back(*brake);
                const int index = indexOfEpoch(voyage.burns, brake->epoch);
                return {voyage.burns, index,
                        tr("Brake at your closest to the Earth, on %1: %2 against your motion "
                           "slows you enough for its gravity to keep you.")
                            .arg(display::date(brake->epoch), display::speed(burnSize(*brake)))};
            }
        }
    }
    if (voyage.goal == Goal::PlanetOrbit && closest.valid && !prediction.hitPlanet) {
        const double radii = closest.distance / bodyRadius(to);
        if (radii >= VoyageOptions{}.orbitMinPeriapsisRadii && radii < 3.0) {
            if (const auto brake = captureBurn(voyage)) {
                voyage.burns.push_back(*brake);
                const int index = indexOfEpoch(voyage.burns, brake->epoch);
                return {
                    voyage.burns, index,
                    tr("Brake at your closest to %1, on %2: %3 against your motion slows "
                       "you enough for its gravity to keep you.")
                        .arg(name, display::date(brake->epoch), display::speed(burnSize(*brake)))};
            }
        }
    }

    // To land, coming home from far away: once the pass is aimed at the air,
    // shed speed inside the Earth's sphere of influence, aimed again.
    if (voyage.goal == Goal::Reentry) {
        const double altitude = prediction.closest.distance - kRadiusEarth;
        const bool aimed = prediction.entrySpeed > 0.0 || altitude < 5000e3;
        if (aimed && approachSpeed(prediction.closest) > fastestReentryArrival() + 10.0) {
            if (const auto brake = approachBrake(voyage, now)) {
                voyage.burns.push_back(*brake);
                auto index = static_cast<std::size_t>(indexOfEpoch(voyage.burns, brake->epoch));
                if (const auto tuned = refineAim(voyage, index)) {
                    voyage.burns[index] = *tuned;
                    index = static_cast<std::size_t>(indexOfEpoch(voyage.burns, tuned->epoch));
                }
                // Energy around the Earth: v_entry² = v∞² + 2μ/r_I.
                const double rI = kRadiusEarth + VoyageOptions{}.entryInterfaceAltitude;
                const double entry = prediction.entrySpeed > 0.0
                                         ? prediction.entrySpeed
                                         : std::hypot(approachSpeed(prediction.closest),
                                                      std::sqrt(2.0 * bodyMu(Body::Earth) / rI));
                return {voyage.burns, static_cast<int>(index),
                        tr("Coming from so far out, you'd hit the air at about %1: faster than "
                           "any heat shield can take. Brake as you near the Earth, on %2: %3 "
                           "against your motion, aimed so you still meet the air at the right "
                           "angle.")
                            .arg(display::speed(entry), display::date(brake->epoch),
                                 display::speed(burnSize(voyage.burns[index])))};
            }
        }
    }

    // Already braking near the Earth for a landing: aim with that burn (a
    // correction before it would be flown without it, and miss).
    const double arrival = prediction.arrival.value_or(closest.epoch);
    if (voyage.goal == Goal::Reentry) {
        const auto late = std::ranges::find_if(voyage.burns, [&](const Burn& b) {
            return b.epoch >= now && b.epoch >= arrival && b.epoch < prediction.closest.epoch;
        });
        if (late != voyage.burns.end()) {
            const auto index = static_cast<std::size_t>(late - voyage.burns.begin());
            if (const auto tuned = refineAim(voyage, index)) {
                voyage.burns[index] = *tuned;
                return {voyage.burns, static_cast<int>(index),
                        tr("Changing your burn on %1 to %2 lines you up for %3.")
                            .arg(display::date(tuned->epoch), display::speed(burnSize(*tuned)),
                                 goalPhrase(voyage.goal, to))};
            }
        }
    }

    // A course correction a few weeks out (or half way, if that's sooner),
    // and before reaching the destination's sphere of influence (or before
    // the closest approach, once inside it).
    const double target = arrival > now ? arrival : closest.epoch;
    double when = target - kCorrectionLead;
    if (when <= now) when = now + 0.5 * (target - now);
    voyage.burns.push_back({when, 0.0, 0.0, 0.0});
    std::ranges::stable_sort(voyage.burns, {}, &Burn::epoch);
    const auto it = std::ranges::find_if(
        voyage.burns, [&](const Burn& b) { return b.epoch == when && burnSize(b) == 0.0; });
    const auto index = static_cast<std::size_t>(it - voyage.burns.begin());
    const auto tuned = aimAtDestination(voyage, index);
    if (!tuned) return {{}, -1, tr("No small correction lines you up from here: try a new plan.")};
    voyage.burns[index] = *tuned;
    return {voyage.burns, static_cast<int>(index),
            tr("A small correction on %1 (%2) lines you up for %3.")
                .arg(display::date(when), display::speed(burnSize(*tuned)),
                     goalPhrase(voyage.goal, to))};
}
