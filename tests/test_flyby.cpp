// SPDX-License-Identifier: GPL-3.0-or-later
// Gravity assists (flyby.hpp): what a pass by a planet does, and aiming a
// burn so the path passes where you want.

#include "orbit/core/astro_time.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/flyby.hpp"
#include "orbit/core/solar_system.hpp"
#include "orbit/core/two_body.hpp"
#include "orbit/core/voyage.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <utility>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kDay = 86400.0;

double epoch2027() { return epochFromCalendar({2027, 1, 1, 0, 0, 0.0}); }

/// Energy per kg around the Sun, v²/2 − μ/r: higher is a bigger orbit.
double sunEnergy(const VoyageSample& s) { return normSquared(s.v) / 2.0 - kMuSun / norm(s.r); }

/// Heliocentric longitude (radians, 0 to 2π).
double longitude(const Vec3& r) { return std::atan2(r.y, r.x); }

/// A ship on the Earth's orbit but well away from the Earth (60 days
/// ahead of it), with plenty of fuel and one empty burn, timed for a
/// launch window to Jupiter: Jupiter about 97° ahead, so that it arrives
/// where a Hohmann transfer (2.7 years, while Jupiter moves 83°) ends.
Voyage shipNearEarthsOrbit() {
    Voyage v;
    v.startEpoch = epoch2027();
    const TwoBodyState earth{planetPosition(Planet::Earth, v.startEpoch),
                             planetVelocity(Planet::Earth, v.startEpoch)};
    const TwoBodyState ahead = propagateKepler(earth, 60.0 * kDay, kMuSun);
    v.position = ahead.r;
    v.velocity = ahead.v;
    v.ship = {2000.0, 60000.0, 320.0};
    v.lookAhead = 6.0 * 365.25 * kDay;
    double window = v.startEpoch;
    double bestOff = 10.0;
    for (int day = 10; day < 420; ++day) {
        const double t = v.startEpoch + day * kDay;
        const Vec3 ship = propagateKepler({v.position, v.velocity}, t - v.startEpoch, kMuSun).r;
        double lead = longitude(planetPosition(Planet::Jupiter, t)) - longitude(ship);
        lead = std::remainder(lead, 2.0 * std::numbers::pi);
        const double off = std::abs(lead - 97.0 * std::numbers::pi / 180.0);
        if (off < bestOff) {
            bestOff = off;
            window = t;
        }
    }
    v.burns = {{window, 0.0, 0.0, 0.0}};
    return v;
}

} // namespace

TEST_CASE("A hyperbolic pass: v∞, the turn, and the speed around the Sun", "[flyby]") {
    // Past Jupiter at 10 radii, 6 km/s far away. e = 1 + r_p v∞² / μ, and
    // the path turns by δ = 2 asin(1/e).
    const double t = epoch2027();
    const double mu = planetMu(Planet::Jupiter);
    const double rp = 10.0 * planetRadius(Planet::Jupiter);
    const double vInf = 6000.0;
    const double vp = std::sqrt(vInf * vInf + 2.0 * mu / rp);
    const Vec3 rel{0.0, rp, 0.0};
    const Vec3 relV{-vp, 0.0, 0.0};
    const PlanetPass pass = passAt(Planet::Jupiter, t, planetPosition(Planet::Jupiter, t) + rel,
                                   planetVelocity(Planet::Jupiter, t) + relV);
    CHECK_THAT(pass.distance, WithinRel(rp, 1e-12));
    CHECK_THAT(pass.vInfinity, WithinRel(vInf, 1e-9));
    const double e = 1.0 + rp * vInf * vInf / mu;
    CHECK_THAT(pass.turnAngle, WithinRel(2.0 * std::asin(1.0 / e), 1e-9));
    // The speed relative to Jupiter is unchanged; around the Sun it isn't.
    CHECK(std::abs(pass.sunSpeedAfter - pass.sunSpeedBefore) > 1000.0);

    // The B-plane: b = r_p √(1 + 2μ/(r_p v∞²)), square to the incoming path.
    const BPlane plane = bPlane(rel, relV, mu);
    CHECK_THAT(norm(plane.b), WithinRel(impactParameter(rp, vInf, mu), 1e-9));
    CHECK_THAT(dot(plane.b, plane.s), WithinAbs(0.0, 1.0));
    CHECK_THAT(plane.vInfinity, WithinRel(vInf, 1e-9));
}

TEST_CASE("The flight records passes inside a planet's sphere of influence", "[flyby]") {
    const Voyage voyage = shipNearEarthsOrbit();
    const VoyageResult coast = flyVoyage(voyage);
    // Coasting on the Earth's orbit, 60 days ahead: no flybys, but a closest
    // approach to every planet.
    CHECK(coast.flybys.empty());
    for (const PlanetPass& p : coast.closestPasses) CHECK(p.valid);
    CHECK(coast.closestPasses.at(static_cast<std::size_t>(Planet::Jupiter)).distance >
          3.0 * kAstronomicalUnit);
}

TEST_CASE("Aiming at Jupiter, and what the pass gives", "[flyby][slow]") {
    Voyage voyage = shipNearEarthsOrbit();
    const double rj = planetRadius(Planet::Jupiter);
    const auto passFor = [&](const FlybyAim& aim) {
        Voyage v = voyage;
        const auto burn = aimFlyby(v, 0, aim).burn;
        REQUIRE(burn);
        v.burns = {*burn};
        const auto pass = passAfter(flyVoyage(v), Planet::Jupiter, burn->epoch);
        REQUIRE(pass);
        CAPTURE(flybySideName(aim.side), burnSize(*burn), pass->distance / rj, pass->vInfinity,
                pass->sunSpeedBefore, pass->sunSpeedAfter);
        // Where it was aimed, to within a fraction of a percent.
        CHECK_THAT(pass->distance, WithinRel(aim.periapsis, 0.01));
        return std::pair{v, *pass};
    };

    const auto [faster, fast] = passFor({Planet::Jupiter, 8.0 * rj, FlybySide::Faster});
    const auto [slower, slow] = passFor({Planet::Jupiter, 8.0 * rj, FlybySide::Slower});
    // Arriving from a Hohmann transfer, Jupiter catches the ship up: its
    // motion relative to Jupiter points backwards, so turning it either way
    // speeds the ship up, but on the "faster" side by much more.
    CHECK(fast.sunSpeedAfter > fast.sunSpeedBefore + 5000.0);
    CHECK(slow.sunSpeedAfter < fast.sunSpeedAfter - 1000.0);
    CHECK(fast.turnAngle > 0.5);

    // The patched-conic estimate against the real thing: the energy around
    // the Sun well outside Jupiter's sphere of influence (48 million km,
    // about 100 days each way at 6 km/s), ΔE = (v_after² − v_before²) / 2.
    const VoyageSample before = voyageStateAt(faster, fast.epoch - 200.0 * kDay);
    const VoyageSample after = voyageStateAt(faster, fast.epoch + 200.0 * kDay);
    const double estimate =
        (fast.sunSpeedAfter * fast.sunSpeedAfter - fast.sunSpeedBefore * fast.sunSpeedBefore) / 2.0;
    CHECK_THAT(sunEnergy(after) - sunEnergy(before), WithinRel(estimate, 0.05));

    // Aiming again with "keep" and a wider pass: further out, less turn.
    Voyage again = faster;
    const auto wider = aimFlyby(again, 0, {Planet::Jupiter, 20.0 * rj, FlybySide::Keep}).burn;
    REQUIRE(wider);
    again.burns = {*wider};
    const auto widerPass = passAfter(flyVoyage(again), Planet::Jupiter, wider->epoch);
    REQUIRE(widerPass);
    CHECK_THAT(widerPass->distance, WithinRel(20.0 * rj, 0.01));
    CHECK(widerPass->turnAngle < fast.turnAngle);
    CHECK(widerPass->sunSpeedAfter - widerPass->sunSpeedBefore <
          fast.sunSpeedAfter - fast.sunSpeedBefore);
}

TEST_CASE("Aiming at a planet out of reach says so", "[flyby]") {
    // From the Earth's orbit, at a poor date, with a small tank.
    Voyage voyage = shipNearEarthsOrbit();
    voyage.burns.front().epoch = voyage.startEpoch + 1.0 * kDay;
    voyage.ship.propellant = 500.0;
    const FlybyAimResult result =
        aimFlyby(voyage, 0, {Planet::Neptune, 5.0 * planetRadius(Planet::Neptune)});
    CHECK_FALSE(result.burn);
    CHECK(result.outOfReach);
    CHECK(result.cheapest > deltaVCapacity(voyage.ship));
}
