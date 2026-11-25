// SPDX-License-Identifier: GPL-3.0-or-later
// The "Lost in space" game: voyages with instant burns (voyage.hpp), and the
// puzzles and hints (lost_in_space.hpp). The key promise: every puzzle can
// be solved, with the full simulation, on the fuel it gives.

#include "orbit/core/astro_time.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/lost_in_space.hpp"
#include "orbit/core/solar_system.hpp"
#include "orbit/core/voyage.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kDay = 86400.0;

double epoch2027() { return epochFromCalendar({2027, 1, 1, 0, 0, 0.0}); }

/// A ship a little outside the Earth's orbit, nowhere near the Earth.
Voyage driftingShip() {
    Voyage v;
    v.startEpoch = epoch2027();
    const Vec3 earth = planetPosition(Planet::Earth, v.startEpoch);
    v.position = -1.1 * earth; // the far side of the Sun
    v.velocity =
        std::sqrt(kMuSun / norm(v.position)) * normalized(cross(Vec3{0, 0, 1}, v.position));
    v.ship = {2000.0, 3000.0, 320.0};
    return v;
}

} // namespace

TEST_CASE("Burns spend propellant by the rocket equation", "[voyage]") {
    Voyage voyage = driftingShip();
    const double capacity = deltaVCapacity(voyage.ship);
    // v_e ln(5000 / 2000)
    CHECK_THAT(capacity, WithinRel(320.0 * kG0 * std::log(2.5), 1e-12));

    voyage.burns = {{voyage.startEpoch + 10 * kDay, 1000.0, 0.0, 0.0}};
    VoyageOptions options;
    options.stopAt = voyage.startEpoch + 20 * kDay;
    const VoyageResult result = flyVoyage(voyage, options);
    REQUIRE(result.burns.size() == 1);
    CHECK_FALSE(result.burns[0].shortOfFuel);
    CHECK_THAT(result.burns[0].propellantUsed,
               WithinRel(propellantFor(1000.0, 5000.0, 320.0), 1e-12));
    CHECK_THAT(result.deltaVLeft, WithinAbs(capacity - 1000.0, 1e-6));
    CHECK(result.end == VoyageEnd::StopTime);

    // Asking for more than the tank holds gives what there is.
    voyage.burns = {{voyage.startEpoch + 10 * kDay, 0.0, 5000.0, 0.0}};
    const VoyageResult dry = flyVoyage(voyage, options);
    CHECK(dry.burns[0].shortOfFuel);
    CHECK_THAT(dry.burns[0].deltaV, WithinRel(capacity, 1e-12));
    CHECK_THAT(dry.propellantLeft, WithinAbs(0.0, 1e-6));
}

TEST_CASE("Burn directions: prograde speeds up, normal tilts, radial turns outwards", "[voyage]") {
    const Voyage voyage = driftingShip();
    const BurnFrame frame = burnFrameAt(voyage.startEpoch, voyage.position, voyage.velocity);
    CHECK(frame.body == ReferenceBody::Sun);
    CHECK_THAT(dot(frame.prograde, frame.radial), WithinAbs(0.0, 1e-12));
    CHECK_THAT(dot(frame.prograde, frame.normal), WithinAbs(0.0, 1e-12));
    CHECK_THAT(norm(frame.radial), WithinAbs(1.0, 1e-12));
    // A circular orbit: radial points straight away from the Sun, normal to
    // ecliptic north.
    CHECK(dot(frame.radial, normalized(voyage.position)) > 0.999);
    CHECK(frame.normal.z > 0.999);

    const Burn burn{voyage.startEpoch, 10.0, -20.0, 30.0};
    const Burn back = burnFromVector(burn.epoch, burnVector(burn, frame), frame);
    CHECK_THAT(back.prograde, WithinAbs(10.0, 1e-9));
    CHECK_THAT(back.radial, WithinAbs(-20.0, 1e-9));
    CHECK_THAT(back.normal, WithinAbs(30.0, 1e-9));
    CHECK_THAT(burnSize(burn), WithinRel(std::sqrt(1400.0), 1e-12));

    // Near the Earth, directions are taken relative to it.
    const double t = voyage.startEpoch;
    const Vec3 nearEarth = planetPosition(Planet::Earth, t) + Vec3{1e8, 0, 0};
    CHECK(burnFrameAt(t, nearEarth, planetVelocity(Planet::Earth, t) + Vec3{0, 500, 0}).body ==
          ReferenceBody::Earth);
}

TEST_CASE("A ship with no plan just coasts: energy around the Sun barely changes", "[voyage]") {
    const Voyage voyage = driftingShip();
    VoyageOptions options;
    options.horizon = 365.25 * kDay;
    const VoyageResult result = flyVoyage(voyage, options);
    CHECK(result.end == VoyageEnd::Horizon);
    CHECK_FALSE(result.goalMet);
    REQUIRE(result.samples.size() > 100);
    const auto energy = [](const VoyageSample& s) {
        return normSquared(s.v) / 2 - kMuSun / norm(s.r);
    };
    // The planets tug a little (Jupiter most), but this is a 1.1 au orbit.
    CHECK_THAT(energy(result.samples.back()), WithinRel(energy(result.samples.front()), 1e-3));
    CHECK(result.closest.valid);
    CHECK(result.closest.distance > 0.5 * kAstronomicalUnit);
}

TEST_CASE("The arrival cost of each goal", "[lost-in-space]") {
    CHECK(arrivalCost(Goal::ReachEarth, 5000.0) == 0.0);
    // Below about 7.2 km/s of v∞, the entry stays under 13 km/s: free.
    CHECK(arrivalCost(Goal::Reentry, 5000.0) == 0.0);
    CHECK(arrivalCost(Goal::Reentry, 9000.0) > 1500.0);
    // Capture from a slow arrival costs a few hundred m/s, more when faster.
    const double slow = arrivalCost(Goal::EarthOrbit, 1000.0);
    CHECK(slow > 100.0);
    CHECK(slow < 600.0);
    CHECK(arrivalCost(Goal::EarthOrbit, 4000.0) > slow);
}

TEST_CASE("The same seed always gives the same puzzle", "[lost-in-space]") {
    const Puzzle a = makePuzzle(Difficulty::Easy, 42, epoch2027());
    const Puzzle b = makePuzzle(Difficulty::Easy, 42, epoch2027());
    const Puzzle c = makePuzzle(Difficulty::Easy, 43, epoch2027());
    CHECK(a.voyage.position == b.voyage.position);
    CHECK(a.budget == b.budget);
    CHECK(a.voyage.position != c.voyage.position);
}

TEST_CASE("Every puzzle can be solved on its fuel, with the full simulation", "[lost-in-space]") {
    const Difficulty difficulty = GENERATE(Difficulty::Easy, Difficulty::Normal, Difficulty::Hard);
    const std::uint64_t seed = GENERATE(1U, 2U);
    CAPTURE(difficultyName(difficulty), seed);

    const Puzzle puzzle = makePuzzle(difficulty, seed, epoch2027());
    const TransferPlan& plan = puzzle.plan;
    CAPTURE(plan.departureCost, plan.arrivalCost, puzzle.budget);
    REQUIRE(plan.total() > 0.0);
    CHECK(puzzle.budget > plan.total());
    CHECK_THAT(deltaVCapacity(puzzle.voyage.ship), WithinRel(puzzle.budget, 1e-9));
    CHECK(plan.departure >= puzzle.voyage.startEpoch);

    // Play it the way the hints would: the departure burn from Lambert,
    // fine-tuned with the real gravity...
    Voyage voyage = puzzle.voyage;
    const auto departure = burnTowardsEarth(voyage, plan.departure, plan.arrival);
    REQUIRE(departure);
    voyage.burns = {*departure};
    const auto tuned = refineAim(voyage, 0);
    REQUIRE(tuned);
    voyage.burns = {*tuned};
    // ...a course correction a few weeks out...
    voyage.burns.push_back({plan.arrival - 20 * kDay, 0.0, 0.0, 0.0});
    const auto correction = refineAim(voyage, 1);
    REQUIRE(correction);
    voyage.burns[1] = *correction;
    // ...and, to stay, braking at perigee.
    if (voyage.goal == Goal::EarthOrbit) {
        const auto brake = captureBurn(voyage);
        REQUIRE(brake);
        voyage.burns.push_back(*brake);
    }

    const VoyageResult result = flyVoyage(voyage);
    CAPTURE(describe(result.end), result.deltaVLeft, burnSize(voyage.burns[0]),
            burnSize(voyage.burns[1]));
    CHECK(result.goalMet);
    for (const BurnReport& b : result.burns) CHECK_FALSE(b.shortOfFuel);
    CHECK(result.deltaVLeft > 0.0);
    // The departure burn's size is close to the Sun-only estimate.
    CHECK_THAT(burnSize(voyage.burns[0]), WithinRel(plan.departureCost, 0.1));
    if (voyage.goal == Goal::Reentry) {
        CHECK(result.entryAngle < degToRad(-5.0));
        CHECK(result.entryAngle > degToRad(-8.0));
    }
}
