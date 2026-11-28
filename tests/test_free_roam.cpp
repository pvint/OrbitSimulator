// SPDX-License-Identifier: GPL-3.0-or-later
// Free roam (free_roam.hpp): the start in orbit around any planet or moon,
// unlimited fuel, and a flight with no goal that only a crash ends.

#include "orbit/core/astro_time.hpp"
#include "orbit/core/free_roam.hpp"
#include "orbit/core/orbit.hpp"
#include "orbit/core/solar_system.hpp"
#include "orbit/core/voyage.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kDay = 86400.0;

double epoch2027() { return epochFromCalendar({2027, 1, 1, 0, 0, 0.0}); }

/// The orbit around `body` of the voyage's start.
OrbitalElements startOrbit(const Voyage& v, Body body) {
    return elementsFromState(v.position - bodyPosition(body, v.startEpoch),
                             v.velocity - bodyVelocity(body, v.startEpoch), bodyMu(body),
                             bodyRadius(body));
}

} // namespace

TEST_CASE("Free roam starts in orbit around any planet or moon a ship can park at", "[free-roam]") {
    const auto index = GENERATE(range(std::size_t{0}, kAllBodies.size()));
    const Body body = kAllBodies.at(index);
    CAPTURE(bodyName(body));
    if (!canRoamFrom(body)) {
        // Too small and too close to its planet to orbit (Mimas, say).
        REQUIRE(isMoon(body));
        REQUIRE_FALSE(canPark(body));
        CHECK(freeRoam(body, epoch2027()).destination == Body::Earth);
        return;
    }
    const Puzzle game = freeRoam(body, epoch2027());
    const Voyage& v = game.voyage;
    CHECK(isFreeRoam(game));
    CHECK(v.goal == Goal::Explore);
    CHECK(v.destination == body);
    CHECK(v.ship.unlimitedFuel);
    CHECK(game.startMoon.has_value() == isMoon(body));
    // Parked: on a circle at the free-roam orbit's height, round the way
    // the body itself goes round (its orbit's pole and the ship's agree).
    REQUIRE(parkedAround(v.startEpoch, v.position, v.velocity) == body);
    const OrbitalElements el = startOrbit(v, body);
    CHECK_THAT(el.periapsis, WithinRel(freeRoamOrbit(body).periapsis, 1e-6));
    CHECK(el.eccentricity < 1e-6);
    const Vec3 centreR = isMoon(body) ? bodyPosition(planetOf(body), v.startEpoch) : Vec3{};
    const Vec3 centreV = isMoon(body) ? bodyVelocity(planetOf(body), v.startEpoch) : Vec3{};
    const Vec3 bodyPole = normalized(cross(bodyPosition(body, v.startEpoch) - centreR,
                                           bodyVelocity(body, v.startEpoch) - centreV));
    const Vec3 shipPole = normalized(cross(v.position - bodyPosition(body, v.startEpoch),
                                           v.velocity - bodyVelocity(body, v.startEpoch)));
    CHECK(dot(bodyPole, shipPole) > 0.999999);

    // With no burns, nothing happens: still in orbit when the look-ahead ends.
    const VoyageResult flown = flyVoyage(v);
    CHECK(flown.end == VoyageEnd::Horizon);
    CHECK_FALSE(flown.goalMet);
    CHECK_THAT(flown.endEpoch - v.startEpoch, WithinAbs(freeRoamHorizon(body), 1.0));
    const VoyageSample& last = flown.samples.back();
    CHECK(parkedAround(last.epoch, last.r, last.v) == body);
}

TEST_CASE("Free roam around a giant planet starts inside all its moons", "[free-roam]") {
    for (const Body planet : {Body::Jupiter, Body::Saturn, Body::Uranus, Body::Neptune}) {
        CAPTURE(bodyName(planet));
        const ParkingOrbit orbit = freeRoamOrbit(planet);
        CHECK_THAT(orbit.periapsis, WithinRel(2.0 * bodyRadius(planet), 1e-12));
        for (const Body moon : moonsOf(planet)) {
            CHECK(orbit.apoapsis < 0.7 * moonOrbit(moon).semiMajorAxis);
        }
        // Among its moons, predictions look weeks ahead, not a year.
        CHECK(freeRoamHorizon(planet) == 30.0 * kDay);
    }
    CHECK(freeRoamHorizon(Body::Titan) == 30.0 * kDay);
    CHECK(freeRoamHorizon(Body::Earth) == 365.25 * kDay);
    CHECK(freeRoamHorizon(Body::Moon) == 365.25 * kDay);
}

TEST_CASE("Unlimited fuel: any burn is given in full and the mass stays the same", "[free-roam]") {
    Puzzle game = freeRoam(Body::Earth, epoch2027());
    Voyage& v = game.voyage;
    const double mass = v.ship.dryMass + v.ship.propellant;
    // Far more than the tanks would hold (Δv = v_e ln(m / m_dry)), twice.
    REQUIRE(deltaVCapacity(v.ship) < 5000.0);
    v.burns = {{v.startEpoch + 600.0, 20000.0, 0.0, 0.0},
               {v.startEpoch + 10.0 * kDay, -3000.0, 4000.0, 0.0}};
    const VoyageResult flown = flyVoyage(v);
    REQUIRE(flown.burns.size() == 2);
    CHECK_THAT(flown.burns[0].deltaV, WithinRel(20000.0, 1e-12));
    CHECK_THAT(flown.burns[1].deltaV, WithinRel(5000.0, 1e-12));
    for (const BurnReport& b : flown.burns) {
        CHECK_FALSE(b.shortOfFuel);
        CHECK(b.propellantUsed == 0.0);
    }
    CHECK_THAT(flown.deltaVUsed, WithinRel(25000.0, 1e-12));
    CHECK(std::isinf(flown.deltaVLeft));
    CHECK(flown.samples.back().mass == mass);
    CHECK(std::isinf(deltaVAvailable(v.ship, mass)));

    // An ordinary ship would have run dry on the first.
    v.ship.unlimitedFuel = false;
    const VoyageResult limited = flyVoyage(v);
    CHECK(limited.burns[0].shortOfFuel);
    CHECK_THAT(limited.deltaVUsed, WithinRel(deltaVCapacity(v.ship), 1e-9));
    CHECK_THAT(deltaVAvailable(v.ship, mass), WithinRel(deltaVCapacity(v.ship), 1e-12));
}

TEST_CASE("Free roam doesn't end on settling into an orbit", "[free-roam]") {
    // Raising the far side of a low Earth orbit a little leaves the ship in
    // orbit around the Earth: that ends a puzzle going home on Hard, but not
    // free roam.
    Puzzle game = freeRoam(Body::Earth, epoch2027());
    Voyage& v = game.voyage;
    v.burns = {{v.startEpoch + 600.0, 100.0, 0.0, 0.0}};
    const VoyageResult roaming = flyVoyage(v);
    CHECK(roaming.end == VoyageEnd::Horizon);
    const VoyageSample& last = roaming.samples.back();
    CHECK(parkedAround(last.epoch, last.r, last.v) == Body::Earth);

    v.goal = Goal::EarthOrbit;
    v.destination = Body::Earth;
    CHECK(flyVoyage(v).end == VoyageEnd::Captured);
}

TEST_CASE("Free roam still ends in a crash or a landing", "[free-roam]") {
    SECTION("into the Moon") {
        // Stopping dead in a low orbit around the Moon: straight down.
        Puzzle game = freeRoam(Body::Moon, epoch2027());
        Voyage& v = game.voyage;
        const double speed = norm(v.velocity - bodyVelocity(Body::Moon, v.startEpoch));
        v.burns = {{v.startEpoch + 60.0, -speed, 0.0, 0.0}};
        const VoyageResult flown = flyVoyage(v);
        CHECK(flown.end == VoyageEnd::HitPlanet);
        CHECK(flown.hitPlanet == Body::Moon);
        CHECK(flown.endEpoch - v.startEpoch < kDay);
    }
    SECTION("into the Earth's air") {
        // Braking hard in low Earth orbit: down through the air, too
        // steeply for a capsule, but it ends the flight all the same.
        Puzzle game = freeRoam(Body::Earth, epoch2027());
        Voyage& v = game.voyage;
        v.burns = {{v.startEpoch + 60.0, -1500.0, 0.0, 0.0}};
        const VoyageResult flown = flyVoyage(v);
        CHECK(flown.entrySpeed > 0.0);
        CHECK(flown.end != VoyageEnd::Horizon);
        CHECK(flown.endEpoch - v.startEpoch < kDay);
    }
}
