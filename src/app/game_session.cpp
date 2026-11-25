// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_session.hpp"

#include "display_format.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/solar_system.hpp"

#include <QCoreApplication>
#include <QThread>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace {

constexpr double kDay = 86400.0;

/// How far ahead the hint looks for a way home, and the longest trip.
constexpr double kHintWindow = 1.5 * 365.25 * kDay;
constexpr double kHintLongestFlight = 700.0 * kDay;
/// Course corrections go this long before arriving.
constexpr double kCorrectionLead = 20.0 * kDay;

QString goalPhrase(orbit::Goal goal) {
    switch (goal) {
    case orbit::Goal::ReachEarth:
        return QObject::tr("reaching the Earth");
    case orbit::Goal::Reentry:
        return QObject::tr("the re-entry corridor");
    case orbit::Goal::EarthOrbit:
        return QObject::tr("a pass just above the air");
    }
    return {};
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

void GameSession::newPuzzle(orbit::Difficulty difficulty, std::uint64_t seed, double epoch) {
    if (m_makingPuzzle) return;
    m_makingPuzzle = true;
    m_puzzleWatcher.setFuture(
        QtConcurrent::run([=] { return orbit::makePuzzle(difficulty, seed, epoch); }));
    emit busyChanged(true);
}

void GameSession::onPuzzleMade() {
    m_makingPuzzle = false;
    setPuzzle(m_puzzleWatcher.result());
    emit busyChanged(isBusy());
}

void GameSession::setPuzzle(const orbit::Puzzle& puzzle) {
    m_puzzle = puzzle;
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

void GameSession::setNow(double epoch) {
    if (!m_hasPuzzle) return;
    epoch = std::max(epoch, startEpoch());
    // Not past the end of a flight that ends (arrives, or hits something).
    if (m_prediction && m_prediction->end != orbit::VoyageEnd::Horizon) {
        epoch = std::min(epoch, m_prediction->endEpoch);
    }
    if (epoch == m_now) return;
    m_now = epoch;
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

orbit::Voyage GameSession::voyage() const {
    orbit::Voyage v = m_puzzle.voyage;
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
    if (!m_hasPuzzle || m_makingHint) return;
    m_makingHint = true;
    m_hintWatcher.setFuture(QtConcurrent::run([puzzle = m_puzzle, burns = m_burns, now = m_now] {
        return computeHint(puzzle, burns, now);
    }));
    emit busyChanged(true);
}

void GameSession::onHintMade() {
    const Hint hint = m_hintWatcher.result();
    m_makingHint = false;
    emit busyChanged(isBusy());
    if (!hint.burns.empty()) {
        ++m_hintsUsed;
        m_burns = hint.burns;
        m_selected = hint.select;
        emit burnsChanged();
        emit selectionChanged(m_selected);
        predict();
    }
    emit hintReady(hint.message);
}

/// The hint: plan the way home if the path doesn't reach the Earth; aim it
/// if it does but misses the goal; brake at perigee to stay, on Hard.
GameSession::Hint GameSession::computeHint(const orbit::Puzzle& puzzle,
                                           const std::vector<orbit::Burn>& burns, double now) {
    using namespace orbit;
    Voyage voyage = puzzle.voyage;
    voyage.burns = burns;
    const VoyageResult prediction = flyVoyage(voyage);
    if (prediction.goalMet) {
        return {{}, -1, tr("You're on your way home already: just fly.")};
    }
    const double soi = sphereOfInfluence(Planet::Earth, now);
    const bool reaches = prediction.earthArrival.has_value() ||
                         (prediction.closest.valid && prediction.closest.distance < soi);

    if (!reaches) {
        // Start again from here: the cheapest transfer from where the ship
        // is now, replacing the burns still to come.
        std::erase_if(voyage.burns, [&](const Burn& b) { return b.epoch >= now; });
        const bool replaced = voyage.burns.size() < burns.size();
        const VoyageSample here = voyageStateAt(voyage, now);
        const auto plan =
            cheapestWayHome(now, here.r, here.v, voyage.goal, kHintWindow, kHintLongestFlight);
        if (!plan) return {{}, -1, tr("No way home turns up from here in the next 18 months.")};
        const auto departure = burnTowardsEarth(voyage, plan->departure, plan->arrival);
        if (!departure) return {{}, -1, tr("No way home turns up from here.")};
        voyage.burns.push_back(*departure);
        const auto index = voyage.burns.size() - 1;
        if (const auto tuned = refineAim(voyage, index)) voyage.burns[index] = *tuned;
        QString message =
            tr("The cheapest way home leaves on %1 with a burn of %2, and arrives on %3. "
               "Fly there and it fires.")
                .arg(display::date(plan->departure), display::speed(burnSize(voyage.burns[index])),
                     display::date(plan->arrival));
        if (replaced) message += QLatin1Char(' ') + tr("(It replaces the burns you had planned.)");
        return {voyage.burns, static_cast<int>(index), message};
    }

    // On Hard, once the pass is aimed just above the air: brake at perigee.
    if (voyage.goal == Goal::EarthOrbit && prediction.entrySpeed == 0.0) {
        const double altitude = prediction.closest.distance - kRadiusEarth;
        if (altitude >= 150e3 && altitude < 2000e3) {
            if (const auto brake = captureBurn(voyage)) {
                voyage.burns.push_back(*brake);
                std::ranges::stable_sort(voyage.burns, {}, &Burn::epoch);
                const auto it = std::ranges::find_if(
                    voyage.burns, [&](const Burn& b) { return b.epoch == brake->epoch; });
                return {voyage.burns, static_cast<int>(it - voyage.burns.begin()),
                        tr("Brake at your closest to the Earth, on %1: %2 against your motion "
                           "slows you enough for its gravity to keep you.")
                            .arg(display::date(brake->epoch), display::speed(burnSize(*brake)))};
            }
        }
    }

    // A course correction a few weeks out (or half way, if that's sooner),
    // and before reaching the Earth's sphere of influence.
    const double arrival = prediction.earthArrival.value_or(prediction.closest.epoch);
    double when = arrival - kCorrectionLead;
    if (when <= now) when = now + 0.5 * (arrival - now);
    voyage.burns.push_back({when, 0.0, 0.0, 0.0});
    std::ranges::stable_sort(voyage.burns, {}, &Burn::epoch);
    const auto it = std::ranges::find_if(
        voyage.burns, [&](const Burn& b) { return b.epoch == when && burnSize(b) == 0.0; });
    const auto index = static_cast<std::size_t>(it - voyage.burns.begin());
    const auto tuned = refineAim(voyage, index);
    if (!tuned) return {{}, -1, tr("No small correction lines you up from here: try a new plan.")};
    voyage.burns[index] = *tuned;
    return {
        voyage.burns, static_cast<int>(index),
        tr("A small correction on %1 (%2) lines you up for %3.")
            .arg(display::date(when), display::speed(burnSize(*tuned)), goalPhrase(voyage.goal))};
}
