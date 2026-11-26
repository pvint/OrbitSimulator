// SPDX-License-Identifier: GPL-3.0-or-later
// Trips to a moon (moon_transfer.hpp, lost_in_space.hpp): the speed a
// moon is met at, the goals there, puzzles that end at a moon, and the
// hints that get there.

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/lost_in_space.hpp"
#include "orbit/core/moon_transfer.hpp"
#include "orbit/core/solar_system.hpp"
#include "orbit/core/voyage.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <optional>
#include <vector>

using namespace orbit;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kHour = 3600.0;
constexpr double kDay = 86400.0;

double epoch2027() { return epochFromCalendar({2027, 1, 1, 0, 0, 0.0}); }

double angleBetween(const Vec3& a, const Vec3& b) {
    return std::acos(std::clamp(dot(a, b) / (norm(a) * norm(b)), -1.0, 1.0));
}

/// The index of the burn at `epoch` once sorted.
std::size_t indexOf(std::vector<Burn>& burns, double epoch) {
    std::ranges::stable_sort(burns, {}, &Burn::epoch);
    return static_cast<std::size_t>(
        std::ranges::find_if(burns, [&](const Burn& b) { return b.epoch == epoch; }) -
        burns.begin());
}

/// The game's hints, in brief (GameSession::computeHint), all planned at
/// the start: the last leg to the moon once the path gets into its
/// planet's system, else the way to the planet; aimed; then a braking burn
/// at the low point if the goal is to stay.
VoyageResult playHints(Voyage v, int steps) {
    VoyageResult flown = flyVoyage(v);
    for (int i = 0; i < steps && !flown.goalMet; ++i) {
        const Body to = v.destination;
        const PlanetPass& closest = flown.closestPasses.at(static_cast<std::size_t>(to));
        const bool reaches = flown.arrival.has_value() ||
                             (closest.valid && closest.distance < sphereOfInfluence(to, 0.0));
        std::optional<Burn> added;
        if (reaches) {
            if (v.goal != Goal::PlanetOrbit) break;
            const auto brake = captureBurn(v);
            if (!brake) break;
            v.burns.push_back(*brake);
            flown = flyVoyage(v);
            continue;
        }
        if (const auto leg = planToMoon(v, v.startEpoch, flown)) {
            std::erase_if(v.burns, [&](const Burn& b) { return b.epoch >= leg->from; });
            added = leg->burn;
        } else {
            // Leaving the planet it's parked around (or its own orbit).
            DepartureFrom from;
            Vec3 r = v.position;
            Vec3 vel = v.velocity;
            if (const auto parked = parkedAround(v.startEpoch, r, vel)) {
                from = {parked, parkingOrbit(*parked), {}};
                r = bodyPosition(*parked, v.startEpoch);
                vel = bodyVelocity(*parked, v.startEpoch);
            }
            const SearchSpan span = searchSpan(r, vel, to);
            const auto plan = cheapestTransfer(v.startEpoch, r, vel, from, to, v.goal, span.window,
                                               span.longestFlight);
            if (!plan) break;
            v.burns.clear();
            added = transferBurn(v, plan->departure, plan->arrival);
        }
        if (!added) break;
        v.burns.push_back(*added);
        const std::size_t at = indexOf(v.burns, added->epoch);
        if (const auto tuned = aimAtDestination(v, at)) v.burns[at] = *tuned;
        flown = flyVoyage(v);
    }
    return flown;
}

} // namespace

TEST_CASE("A moon is met fast: its planet's gravity speeds up every arrival", "[moontrips]") {
    // Falling from far away at v∞, the ship reaches Titan's distance a at
    // √(v∞² + 2μ/a) (energy around Saturn) while Titan moves at √(μ/a):
    // from the Earth's direct transfer (v∞ about 5.4 km/s), nearly 4 km/s.
    CHECK_THAT(moonApproachSpeed(5400.0, Body::Titan), WithinRel(3980.0, 0.01));
    // Even arriving with no speed to spare, the ship falls in at escape
    // speed, √2 times the moon's: (√2 − 1) of the moon's speed is left.
    const double a = moonOrbit(Body::Titan).semiMajorAxis;
    const double vMoon = std::sqrt(bodyMu(Body::Saturn) / a);
    CHECK_THAT(moonApproachSpeed(0.0, Body::Titan),
               WithinRel((std::sqrt(2.0) - 1.0) * vMoon, 1e-9));
    // The Earth's gravity is far weaker: coming in slowly, the Moon is met
    // at under 1 km/s.
    CHECK(moonApproachSpeed(500.0, Body::Moon) < 1000.0);
}

TEST_CASE("Goals at a moon", "[moontrips]") {
    // Mimas is too small to orbit so close to Saturn: Hard is a close pass.
    CHECK(goalFor(Difficulty::Hard, Body::Mimas) == Goal::ClosePass);
    CHECK(goalFor(Difficulty::Hard, Body::Titan) == Goal::PlanetOrbit);
    CHECK(goalFor(Difficulty::Easy, Body::Moon) == Goal::ReachPlanet);
    // Braking into orbit aims above Titan's haze, and the orbit braked into
    // stays well inside each moon's sphere of influence.
    CHECK(aimPeriapsis(Goal::PlanetOrbit, Body::Titan) > lowestOrbit(Body::Titan));
    for (const Body moon : kMoons) {
        if (!canPark(moon)) continue;
        INFO(bodyName(moon));
        const double low = aimPeriapsis(Goal::PlanetOrbit, moon);
        const double high = captureApoapsis(moon, 0.0);
        CHECK(low > lowestOrbit(moon));
        CHECK(high >= low);
        CHECK(high < 0.5 * sphereOfInfluence(moon, 0.0));
    }
}

TEST_CASE("Going to a planet's own moon starts in orbit around it", "[moontrips]") {
    // Drifting near Jupiter, Ganymede would be years away round the Sun.
    const PuzzleSpec jupiter =
        settledSpec({Difficulty::Normal, Region::Jupiter, false, Body::Ganymede});
    CHECK(jupiter.inOrbit);
    CHECK(jupiter.to == Body::Ganymede);
    const PuzzleSpec earth = settledSpec({Difficulty::Normal, Region::Earth, false, Body::Moon});
    CHECK(earth.inOrbit);
    CHECK(earth.to == Body::Moon);
    // Another planet's moon: drifting is fine.
    CHECK_FALSE(settledSpec({Difficulty::Normal, Region::Mars, false, Body::Titan}).inOrbit);
}

TEST_CASE("From low Earth orbit to the Moon", "[moontrips]") {
    const double t = epoch2027();
    for (const Difficulty d : {Difficulty::Easy, Difficulty::Normal, Difficulty::Hard}) {
        INFO(difficultyName(d));
        const Puzzle p = makePuzzle({d, Region::Earth, true, Body::Moon}, 7, t);
        const Voyage& v = p.voyage;
        REQUIRE(parkedAround(v.startEpoch, v.position, v.velocity) == Body::Earth);
        // In the Moon's plane, leaving within a few hours.
        const Vec3 rel = v.position - bodyPosition(Body::Earth, t);
        const Vec3 relV = v.velocity - bodyVelocity(Body::Earth, t);
        CHECK(angleBetween(cross(rel, relV), moonOrbitNormal(Body::Moon, t)) < degToRad(1.0));
        CHECK(p.plan.departure - v.startEpoch < 6.0 * kHour);
        // Trans-lunar injection from 200 km: about 3.1 km/s (Apollo's was
        // 3.1 to 3.2), and a trip of three to six days.
        CHECK(p.plan.departureCost > 3000.0);
        CHECK(p.plan.departureCost < 3250.0);
        CHECK(p.plan.arrival - p.plan.departure > 3.0 * kDay);
        CHECK(p.plan.arrival - p.plan.departure < 6.0 * kDay);
        // Predictions look weeks ahead here, not years.
        CHECK(v.horizon > 0.0);
        CHECK(v.horizon < 120.0 * kDay);

        // The hints get there, on the fuel in the tanks.
        const VoyageResult end = playHints(v, 4);
        CHECK(end.goalMet);
        CHECK(end.deltaVLeft > 0.0);
        if (d == Difficulty::Hard) CHECK(end.end == VoyageEnd::OrbitingPlanet);
    }
}

TEST_CASE("From the Earth to Titan: to Saturn first, then Titan", "[moontrips]") {
    const Puzzle p =
        makePuzzle({Difficulty::Normal, Region::Earth, true, Body::Titan}, 3, epoch2027());
    CHECK(p.voyage.goal == Goal::ClosePass);
    // To Saturn the direct way: over 7 km/s from low Earth orbit, six years
    // or more.
    CHECK(p.plan.departureCost > 7000.0);
    CHECK(p.plan.arrival - p.plan.departure > 5.5 * 365.25 * kDay);
    // The plan meets Titan as fast as moonApproachSpeed says.
    CHECK(p.plan.vInfinity > 3000.0);

    Voyage v = p.voyage;
    const auto departure = transferBurn(v, p.plan.departure, p.plan.arrival);
    REQUIRE(departure);
    v.burns = {*departure};
    const auto aimed = aimAtDestination(v, 0);
    REQUIRE(aimed);
    v.burns = {*aimed};
    // Aimed to pass Saturn at Titan's distance...
    const VoyageResult toSaturn = flyVoyage(v);
    const PlanetPass& saturn = toSaturn.closestPasses.at(static_cast<std::size_t>(Body::Saturn));
    REQUIRE(saturn.valid);
    CHECK_THAT(saturn.distance, WithinRel(moonOrbit(Body::Titan).semiMajorAxis, 0.05));
    CHECK_FALSE(toSaturn.goalMet);
    // ...then a correction a month out meets Titan itself.
    const auto leg = planToMoon(v, v.startEpoch, toSaturn);
    REQUIRE(leg);
    CHECK(leg->burn.epoch < saturn.epoch);
    CHECK(burnSize(leg->burn) < 1000.0);
    v.burns.push_back(leg->burn);
    const std::size_t at = indexOf(v.burns, leg->burn.epoch);
    const auto tuned = aimAtDestination(v, at);
    REQUIRE(tuned);
    v.burns[at] = *tuned;
    const VoyageResult there = flyVoyage(v);
    CHECK(there.end == VoyageEnd::PassedClose);
    CHECK(there.goalMet);
    CHECK(there.deltaVLeft > 0.0);
}

TEST_CASE("Hints reach moons all over the solar system", "[moontrips]") {
    struct Case {
        Difficulty difficulty;
        Region from;
        bool inOrbit;
        Body to;
        VoyageEnd end;
    };
    const Case c = GENERATE(
        // In orbit around Jupiter, into orbit around Europa, deep in
        // Jupiter's gravity.
        Case{Difficulty::Hard, Region::Jupiter, true, Body::Europa, VoyageEnd::OrbitingPlanet},
        // Coming in from between the planets, the Moon must be where the
        // path crosses its orbit: a correction weeks out sees to that.
        Case{Difficulty::Normal, Region::BetweenPlanets, false, Body::Moon, VoyageEnd::PassedClose},
        // Uranus is tipped over: its moons go round nearly square to the
        // ecliptic.
        Case{Difficulty::Normal, Region::Earth, true, Body::Ariel, VoyageEnd::PassedClose},
        // Mimas: too small to orbit, Hard is a close pass.
        Case{Difficulty::Hard, Region::Earth, true, Body::Mimas, VoyageEnd::PassedClose});
    INFO(bodyName(c.to) << " from " << regionKey(c.from));
    const Puzzle p = makePuzzle({c.difficulty, c.from, c.inOrbit, c.to}, 11, epoch2027());
    const VoyageResult end = playHints(p.voyage, 5);
    CHECK(end.end == c.end);
    CHECK(end.goalMet);
    CHECK(end.deltaVLeft > 0.0);
}
