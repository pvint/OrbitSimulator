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
#include <utility>

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
    CHECK_FALSE(frame.planet);
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
    CHECK(burnFrameAt(t, nearEarth, planetVelocity(Planet::Earth, t) + Vec3{0, 500, 0}).planet ==
          Planet::Earth);
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

namespace {

/// Play a puzzle the way the hints would, and fly it.
VoyageResult solveLikeTheHints(const Puzzle& puzzle) {
    const TransferPlan& plan = puzzle.plan;
    // The departure burn from Lambert (out of a parked orbit, at its best
    // point), fine-tuned with the real gravity...
    Voyage voyage = puzzle.voyage;
    const auto departure = transferBurn(voyage, plan.departure, plan.arrival);
    REQUIRE(departure);
    voyage.burns = {*departure};
    const auto tuned = aimAtDestination(voyage, 0);
    REQUIRE(tuned);
    voyage.burns = {*tuned};
    // ...a course correction a few weeks before arriving (as the hint
    // does: before entering the sphere of influence, which for a giant
    // planet is months before the closest approach)...
    const VoyageResult outward = flyVoyage(voyage);
    const double arrival = outward.arrival.value_or(
        outward.closestPasses.at(static_cast<std::size_t>(voyage.destination)).epoch);
    voyage.burns.push_back({std::min(plan.arrival, arrival) - 20 * kDay, 0.0, 0.0, 0.0});
    const auto correction = aimAtDestination(voyage, 1);
    REQUIRE(correction);
    voyage.burns[1] = *correction;
    // ...coming home from far away, braking inside the Earth's sphere of
    // influence (and aiming again) so the heat shield survives...
    if (voyage.goal == Goal::Reentry) {
        if (const auto brake = approachBrake(voyage, voyage.startEpoch)) {
            voyage.burns.push_back(*brake);
            const auto aimed = refineAim(voyage, 2);
            REQUIRE(aimed);
            voyage.burns[2] = *aimed;
        }
    }
    // ...and, to stay, braking at the low point.
    if (voyage.goal == Goal::EarthOrbit || voyage.goal == Goal::PlanetOrbit) {
        const auto brake = captureBurn(voyage);
        REQUIRE(brake);
        voyage.burns.push_back(*brake);
    }

    const VoyageResult result = flyVoyage(voyage);
    CAPTURE(describe(result.end), result.deltaVLeft, result.entrySpeed,
            radToDeg(result.entryAngle));
    for (const Burn& b : voyage.burns) UNSCOPED_INFO("burn of " << burnSize(b) << " m/s");
    CHECK(result.goalMet);
    for (const BurnReport& b : result.burns) CHECK_FALSE(b.shortOfFuel);
    CHECK(result.deltaVLeft > 0.0);
    // The departure burn's size is close to the estimate (Sun only, or
    // climbing out of the parked orbit by the Oberth formula). Out of orbit
    // around a giant the escape takes months, the Sun pulling all the
    // while, which the estimate leaves out: more slack there.
    CHECK_THAT(burnSize(voyage.burns[0]),
               WithinRel(plan.departureCost, puzzle.inOrbit ? 0.25 : 0.1));
    if (voyage.goal == Goal::Reentry) {
        CHECK(result.entryAngle < degToRad(-5.0));
        CHECK(result.entryAngle > degToRad(-8.0));
        CHECK(result.entrySpeed < VoyageOptions{}.fastestEntry);
    }
    return result;
}

void checkPuzzle(const Puzzle& puzzle) {
    const TransferPlan& plan = puzzle.plan;
    CAPTURE(plan.departureCost, plan.arrivalCost, plan.vInfinity, puzzle.budget,
            (plan.arrival - plan.departure) / kDay);
    REQUIRE(plan.total() > 0.0);
    CHECK(puzzle.budget > plan.total());
    CHECK_THAT(deltaVCapacity(puzzle.voyage.ship), WithinRel(puzzle.budget, 1e-9));
    CHECK(plan.departure >= puzzle.voyage.startEpoch);
    solveLikeTheHints(puzzle);
}

} // namespace

TEST_CASE("Every puzzle can be solved on its fuel, with the full simulation", "[lost-in-space]") {
    const Difficulty difficulty = GENERATE(Difficulty::Easy, Difficulty::Normal, Difficulty::Hard);
    const std::uint64_t seed = GENERATE(1U, 2U);
    CAPTURE(difficultyName(difficulty), seed);
    checkPuzzle(makePuzzle(difficulty, seed, epoch2027()));
}

TEST_CASE("Regions: names and keys go both ways", "[lost-in-space]") {
    for (const Region region : kAllRegions) {
        CHECK(regionFromKey(regionKey(region)) == region);
        CHECK_FALSE(regionName(region).empty());
    }
    CHECK_FALSE(regionFromKey("pluto"));
}

TEST_CASE("A puzzle near a planet starts near it, outside its sphere of influence",
          "[lost-in-space]") {
    const auto [region, planet] =
        GENERATE(table<Region, Planet>({{Region::Venus, Planet::Venus},
                                        {Region::Mars, Planet::Mars},
                                        {Region::Jupiter, Planet::Jupiter},
                                        {Region::Saturn, Planet::Saturn},
                                        {Region::Uranus, Planet::Uranus},
                                        {Region::Neptune, Planet::Neptune}}));
    CAPTURE(regionName(region));
    const double t = epoch2027();
    const Puzzle puzzle = makePuzzle(Difficulty::Easy, 7, t, region);
    CHECK(puzzle.region == region);
    const double fromPlanet = norm(puzzle.voyage.position - planetPosition(planet, t));
    const double fromSun = norm(planetPosition(planet, t));
    CHECK(fromPlanet > 3.0 * sphereOfInfluence(planet, t));
    CHECK(fromPlanet < 0.4 * fromSun); // within about 20° along its orbit
    // About the planet's speed: it shares the orbit, give or take a push.
    CHECK_THAT(norm(puzzle.voyage.velocity), WithinRel(norm(planetVelocity(planet, t)), 0.05));
}

TEST_CASE("The search for a way home grows with the distance from the Sun", "[lost-in-space]") {
    const auto circular = [](double au) {
        const Vec3 r{au * kAstronomicalUnit, 0.0, 0.0};
        return std::pair{r, Vec3{0.0, std::sqrt(kMuSun / norm(r)), 0.0}};
    };
    const auto [rMars, vMars] = circular(1.52);
    const auto [rJupiter, vJupiter] = circular(5.2);
    const SearchSpan mars = searchSpan(rMars, vMars);
    const SearchSpan jupiter = searchSpan(rJupiter, vJupiter);
    // Mars and the Earth line up every 780 days (capped at 2.2 years);
    // Jupiter every 399 days.
    CHECK_THAT(mars.window / kDay, WithinAbs(2.2 * 365.25, 1.0));
    CHECK_THAT(jupiter.window / kDay, WithinAbs(1.1 * 398.9, 5.0));
    // A Hohmann transfer from Jupiter takes about 997 days.
    CHECK_THAT(jupiter.longestFlight / kDay, WithinRel(1.3 * 997.0, 0.02));
    CHECK(mars.longestFlight == 540.0 * kDay);
}

TEST_CASE("Coming home from Jupiter: brake before the air, or burn up", "[lost-in-space]") {
    // v∞ = 8.8 km/s (a Hohmann return from Jupiter) hits the air at
    // √(v∞² + 2μ/r_I) ≈ 14 km/s: too fast. The brake costs about the
    // difference in v∞.
    CHECK(fastestReentryArrival() > 6000.0);
    CHECK(fastestReentryArrival() < 6800.0);
    CHECK_THAT(arrivalCost(Goal::Reentry, 8800.0),
               WithinAbs(8800.0 - fastestReentryArrival(), 1e-9));

    EarthApproach slowPass;
    slowPass.valid = true;
    slowPass.distance = kRadiusEarth + 500e3;
    slowPass.speed = std::sqrt(3000.0 * 3000.0 + 2.0 * planetMu(Planet::Earth) / slowPass.distance);
    CHECK_THAT(approachSpeed(slowPass), WithinRel(3000.0, 1e-9));
}

TEST_CASE("Every region's puzzles can be solved on their fuel", "[lost-in-space][regions]") {
    const Region region =
        GENERATE(Region::Venus, Region::Mars, Region::AsteroidBelt, Region::Jupiter, Region::Saturn,
                 Region::Uranus, Region::Neptune);
    const Difficulty difficulty = GENERATE(Difficulty::Easy, Difficulty::Normal, Difficulty::Hard);
    CAPTURE(regionName(region), difficultyName(difficulty));
    checkPuzzle(makePuzzle(difficulty, 3, epoch2027(), region));
}

TEST_CASE("Parked in orbit: Kepler's equations, and the ship stays put", "[lost-in-space][orbit]") {
    // A puzzle in low Earth orbit, no burns: the prediction stays in the
    // 200 km orbit for years without integrating thousands of orbits.
    const Puzzle puzzle =
        makePuzzle({Difficulty::Easy, Region::Earth, true, Planet::Mars}, 5, epoch2027());
    REQUIRE(puzzle.inOrbit);
    CHECK(puzzle.destination == Planet::Mars);
    CHECK(puzzle.voyage.goal == Goal::ReachPlanet);
    const VoyageResult coast = flyVoyage(puzzle.voyage);
    CHECK(coast.end == VoyageEnd::Horizon);
    CHECK(coast.samples.size() < 5000); // a sample a day
    const double lowOrbit = kRadiusEarth + 200e3;
    // Between samples, sampleAt follows the orbit (not a chord across it).
    for (const double hours : {0.3, 7.7, 1000.1}) {
        const double t = puzzle.voyage.startEpoch + hours * 3600.0;
        const VoyageSample s = sampleAt(coast, t);
        CHECK_THAT(norm(s.r - planetPosition(Planet::Earth, t)), WithinRel(lowOrbit, 1e-6));
        CHECK(s.parked == Planet::Earth);
    }
    CHECK(parkedAround(puzzle.voyage.startEpoch, puzzle.voyage.position, puzzle.voyage.velocity) ==
          Planet::Earth);
    // Burn directions in orbit are relative to the Earth.
    CHECK(burnFrameAt(puzzle.voyage.startEpoch, puzzle.voyage.position, puzzle.voyage.velocity)
              .planet == Planet::Earth);
}

TEST_CASE("The Oberth effect: leaving low orbit costs less than v∞", "[lost-in-space][orbit]") {
    // From 200 km, v∞ = 3 km/s for Mars costs √(v∞² + 2μ/r) − √(μ/r)
    // ≈ 3.6 km/s: the escape itself (3.2 km/s more than orbit) is mostly
    // paid for by the depth of the Earth's gravity.
    const ParkingOrbit leo = parkingOrbit(Planet::Earth);
    const double cost = departureCost(Planet::Earth, leo, 3000.0);
    CHECK_THAT(cost, WithinAbs(3600.0, 50.0));
    // Twice the v∞ for much less than twice the cost.
    CHECK(departureCost(Planet::Earth, leo, 6000.0) < 1.7 * cost);
    // A giant's long ellipse: a small burn at its low point.
    const ParkingOrbit jupiter = parkingOrbit(Planet::Jupiter);
    CHECK(jupiter.apoapsis > 40.0 * jupiter.periapsis);
    CHECK(departureCost(Planet::Jupiter, jupiter, 6000.0) < 1500.0);
}

TEST_CASE("Flying into a planet ends the flight", "[lost-in-space][orbit]") {
    // 300,000 km from Mars, heading straight at its centre at 3 km/s.
    const double t = epoch2027();
    Voyage voyage;
    voyage.startEpoch = t;
    const Vec3 out = normalized(Vec3{1.0, 2.0, 0.5});
    voyage.position = planetPosition(Planet::Mars, t) + 3.0e8 * out;
    voyage.velocity = planetVelocity(Planet::Mars, t) - 3000.0 * out;
    voyage.goal = Goal::ReachPlanet;
    voyage.destination = Planet::Jupiter;
    const VoyageResult result = flyVoyage(voyage);
    CHECK(result.end == VoyageEnd::HitPlanet);
    CHECK(result.hitPlanet == Planet::Mars);
    CHECK_FALSE(result.goalMet);
}

TEST_CASE("Leaving the Earth: every destination's puzzles can be solved", "[lost-in-space][out]") {
    const Planet to = GENERATE(Planet::Venus, Planet::Mars, Planet::Jupiter, Planet::Saturn);
    const Difficulty difficulty = GENERATE(Difficulty::Easy, Difficulty::Normal, Difficulty::Hard);
    CAPTURE(planetName(to), difficultyName(difficulty));
    const Puzzle puzzle = makePuzzle({difficulty, Region::Earth, true, to}, 3, epoch2027());
    CHECK(puzzle.voyage.goal == goalFor(difficulty, to));
    CHECK(puzzle.plan.departure - puzzle.voyage.startEpoch < 31.0 * kDay);
    checkPuzzle(puzzle);
}

TEST_CASE("Home from orbit around another planet", "[lost-in-space][orbit]") {
    const Region from = GENERATE(Region::Mars, Region::Venus, Region::Jupiter);
    const Difficulty difficulty = GENERATE(Difficulty::Easy, Difficulty::Normal, Difficulty::Hard);
    CAPTURE(regionName(from), difficultyName(difficulty));
    const Puzzle puzzle = makePuzzle({difficulty, from, true, Planet::Earth}, 4, epoch2027());
    REQUIRE(puzzle.inOrbit);
    const Planet home = *regionPlanet(from);
    CHECK(parkedAround(puzzle.voyage.startEpoch, puzzle.voyage.position, puzzle.voyage.velocity) ==
          home);
    checkPuzzle(puzzle);
}

TEST_CASE("Specs are made sensible", "[lost-in-space]") {
    // The Earth always in orbit, a drifting place never, and never to the
    // planet you start at.
    CHECK(settledSpec({Difficulty::Easy, Region::Earth, false, Planet::Mars}).inOrbit);
    CHECK_FALSE(settledSpec({Difficulty::Easy, Region::AsteroidBelt, true, Planet::Earth}).inOrbit);
    CHECK(settledSpec({Difficulty::Easy, Region::Mars, true, Planet::Mars}).to == Planet::Earth);
    CHECK(settledSpec({Difficulty::Easy, Region::Earth, true, Planet::Earth}).to == Planet::Mars);
}
