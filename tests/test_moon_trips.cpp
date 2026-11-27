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
        // Parked around the moon it started at, with nothing planned: leave
        // it first.
        const auto parked = parkedAround(v.startEpoch, v.position, v.velocity);
        if (parked && isMoon(*parked) && v.burns.empty()) {
            const auto leg = planFromMoon(v, v.startEpoch);
            if (!leg) break;
            v.burns = {leg->burn};
            if (leg->correction) {
                v.burns.push_back(*leg->correction);
            } else if (const auto tuned = aimAtDestination(v, 0)) {
                v.burns[0] = *tuned;
            }
            flown = flyVoyage(v);
            continue;
        }
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
            if (const auto home = parkedAround(v.startEpoch, r, vel)) {
                from = {home, parkingOrbit(*home), {}};
                r = bodyPosition(*home, v.startEpoch);
                vel = bodyVelocity(*home, v.startEpoch);
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

// --- Starting at a moon ----------------------------------------------------

TEST_CASE("Leaving a moon: two escapes, and the moon's own speed for free", "[moontrips]") {
    // From a low orbit around the Moon to 3 km/s clear of the Earth,
    // leaving forwards along the Moon's motion: the hyperbola out of the
    // Earth's gravity needs √(v∞² + 2μ/a) at the Moon's distance a, the
    // Moon already moves at about √(μ/a), and only the difference has to be
    // found by climbing out of the Moon's gravity.
    const double t = epoch2027();
    const MoonState m = moonFromPlanet(Body::Moon, t);
    const double vInf = 3000.0;
    const Vec3 along = normalized(m.v - dot(m.v, normalized(m.r)) * normalized(m.r));
    const double nuInf = std::acos(-1.0 / (1.0 + norm(m.r) * vInf * vInf / bodyMu(Body::Earth)));
    // The direction the escape leaves along, for a low point at the Moon.
    const Vec3 rHat = normalized(m.r);
    const Vec3 out = std::cos(nuInf) * rHat + std::sin(nuInf) * along;
    const ParkingOrbit low = parkingOrbit(Body::Moon);
    const auto escape = escapeFromMoon(Body::Moon, low, {m.r, m.v}, vInf * out);
    REQUIRE(escape);
    const double needed = std::sqrt(vInf * vInf + 2.0 * bodyMu(Body::Earth) / norm(m.r));
    CHECK_THAT(norm(escape->deltaV), WithinRel(needed - norm(m.v), 0.02));
    CHECK_THAT(escape->cost, WithinRel(departureCost(Body::Moon, low, needed - norm(m.v)), 0.02));
    // Cheaper than from low Earth orbit for the same v∞ (about 3.6 km/s).
    CHECK(escape->cost < departureCost(Body::Earth, parkingOrbit(Body::Earth), vInf) - 1000.0);
}

TEST_CASE("Starting at a moon: the spec", "[moontrips]") {
    PuzzleSpec spec;
    spec.from = Region::BetweenPlanets;
    spec.startMoon = Body::Europa;
    spec.to = Body::Mars;
    const PuzzleSpec europa = settledSpec(spec);
    CHECK(europa.from == Region::Jupiter);
    CHECK(europa.inOrbit);
    CHECK(europa.to == Body::Mars);
    // Not to itself, nor to its planet: to another of the planet's moons.
    spec.to = Body::Europa;
    CHECK(settledSpec(spec).to == Body::Io);
    spec.to = Body::Jupiter;
    CHECK(settledSpec(spec).to == Body::Io);
    // But home from the Moon is fine, and the Moon to the Moon is home.
    spec.startMoon = Body::Moon;
    spec.to = Body::Earth;
    CHECK(settledSpec(spec).to == Body::Earth);
    spec.to = Body::Moon;
    CHECK(settledSpec(spec).to == Body::Earth);
    // Home from the Moon is a re-entry at any difficulty.
    for (const Difficulty d : {Difficulty::Easy, Difficulty::Normal, Difficulty::Hard}) {
        CHECK(goalFor(d, Body::Earth, Body::Moon) == Goal::Reentry);
    }
    CHECK(goalFor(Difficulty::Hard, Body::Earth, Body::Titan) == Goal::EarthOrbit);
}

TEST_CASE("Home from the Moon, as Apollo came back", "[moontrips]") {
    PuzzleSpec spec;
    spec.difficulty = Difficulty::Normal;
    spec.startMoon = Body::Moon;
    const Puzzle p = makePuzzle(spec, 5, epoch2027());
    const Voyage& v = p.voyage;
    REQUIRE(parkedAround(v.startEpoch, v.position, v.velocity) == Body::Moon);
    CHECK(v.goal == Goal::Reentry);
    // Trans-Earth injection: under 1 km/s from a low lunar orbit (Apollo's
    // was about 1 km/s, for a faster trip), and four or five days home.
    CHECK(p.plan.departureCost > 700.0);
    CHECK(p.plan.departureCost < 1000.0);
    CHECK(p.plan.arrival - p.plan.departure > 3.5 * kDay);
    CHECK(p.plan.arrival - p.plan.departure < 6.0 * kDay);
    CHECK(v.horizon > 0.0);

    const VoyageResult end = playHints(v, 3);
    CHECK(end.end == VoyageEnd::Reentered);
    CHECK(end.goalMet);
    CHECK(end.deltaVLeft > 0.0);
    // Falling from the Moon's distance, the capsule meets the air at about
    // 11 km/s, just under the Earth's escape speed there.
    CHECK(end.entrySpeed > 10800.0);
    CHECK(end.entrySpeed < 11100.0);
    // Leaving the Moon is no flyby of it, though the path out has a low
    // point just after the burn.
    CHECK(std::ranges::none_of(end.flybys,
                               [](const PlanetPass& pass) { return pass.planet == Body::Moon; }));
}

TEST_CASE("From one moon to another", "[moontrips]") {
    struct Case {
        Difficulty difficulty;
        Body from;
        Body to;
        VoyageEnd end;
    };
    const Case c = GENERATE(
        // Ganymede to Europa: inwards, deep in Jupiter's gravity.
        Case{Difficulty::Hard, Body::Ganymede, Body::Europa, VoyageEnd::OrbitingPlanet},
        // Titan to Enceladus: Enceladus is too small to orbit so near
        // Saturn, so a close pass.
        Case{Difficulty::Normal, Body::Titan, Body::Enceladus, VoyageEnd::PassedClose},
        // Outwards, round tipped-over Uranus.
        Case{Difficulty::Easy, Body::Ariel, Body::Oberon, VoyageEnd::ReachedPlanet});
    INFO(bodyName(c.from) << " to " << bodyName(c.to));
    PuzzleSpec spec;
    spec.difficulty = c.difficulty;
    spec.startMoon = c.from;
    spec.to = c.to;
    const Puzzle p = makePuzzle(spec, 9, epoch2027());
    const Voyage& v = p.voyage;
    REQUIRE(parkedAround(v.startEpoch, v.position, v.velocity) == c.from);
    CHECK(p.plan.departure - v.startEpoch < kDay);
    CHECK(p.plan.arrival - p.plan.departure < 20.0 * kDay);
    CHECK(v.horizon > 0.0);
    const VoyageResult end = playHints(v, 4);
    CHECK(end.end == c.end);
    CHECK(end.goalMet);
    CHECK(end.deltaVLeft > 0.0);
}

TEST_CASE("From a moon out of its planet's system", "[moontrips]") {
    struct Case {
        Difficulty difficulty;
        Body from;
        Body to;
        VoyageEnd end;
    };
    const Case c = GENERATE(
        // From the Moon to Mars: out of the Moon's gravity, then the
        // Earth's, then across to Mars.
        Case{Difficulty::Normal, Body::Moon, Body::Mars, VoyageEnd::PassedClose},
        // From Europa home: out of Jupiter's deep well.
        Case{Difficulty::Easy, Body::Europa, Body::Earth, VoyageEnd::ReachedEarth},
        // From the Moon to Titan: two moons, two planets.
        Case{Difficulty::Easy, Body::Moon, Body::Titan, VoyageEnd::ReachedPlanet});
    INFO(bodyName(c.from) << " to " << bodyName(c.to));
    PuzzleSpec spec;
    spec.difficulty = c.difficulty;
    spec.startMoon = c.from;
    spec.to = c.to;
    const Puzzle p = makePuzzle(spec, 4, epoch2027());
    const Voyage& v = p.voyage;
    REQUIRE(parkedAround(v.startEpoch, v.position, v.velocity) == c.from);
    CHECK(p.plan.departure - v.startEpoch < kDay);
    CHECK(p.plan.departureCost > 0.0);
    const VoyageResult end = playHints(v, 5);
    CHECK(end.end == c.end);
    CHECK(end.goalMet);
    CHECK(end.deltaVLeft > 0.0);
}
