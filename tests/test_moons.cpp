// SPDX-License-Identifier: GPL-3.0-or-later
// Moons in the flight (voyage.hpp, gravity.hpp): they pull, they bend a
// passing path, they can be hit, burns near them are relative to them,
// and a ship parked low around one follows Kepler's equations around it.

#include "orbit/core/astro_time.hpp"
#include "orbit/core/gravity.hpp"
#include "orbit/core/solar_system.hpp"
#include "orbit/core/two_body.hpp"
#include "orbit/core/voyage.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kHour = 3600.0;
constexpr double kDay = 86400.0;

double epoch2027() { return epochFromCalendar({2027, 1, 1, 0, 0, 0.0}); }

/// A ship relative to a body: heliocentric state from a relative one.
Voyage shipAt(Body body, double epoch, const TwoBodyState& rel) {
    Voyage v;
    v.startEpoch = epoch;
    v.position = bodyPosition(body, epoch) + rel.r;
    v.velocity = bodyVelocity(body, epoch) + rel.v;
    v.ship = {2000.0, 1000.0, 320.0};
    return v;
}

/// A ship that will pass `body` at `periapsis` from its centre, arriving
/// at v∞, its closest approach `before` seconds after the start (placed by
/// Kepler's equations with only the body pulling, backwards from there).
/// The low point is along `towards` from the body's centre (a unit vector),
/// and the ship moves square to that, towards ecliptic north as far as it
/// can.
Voyage shipPassing(Body body, double epoch, double vInfinity, double periapsis, double before,
                   const Vec3& towards = {1.0, 0.0, 0.0}) {
    const double mu = bodyMu(body);
    const double vp = std::sqrt(vInfinity * vInfinity + 2.0 * mu / periapsis);
    const Vec3 north{0.0, 0.0, 1.0};
    const Vec3 along = normalized(north - dot(north, towards) * towards);
    const TwoBodyState low{periapsis * towards, vp * along};
    return shipAt(body, epoch, propagateKepler(low, -before, mu));
}

/// The angle between two vectors (radians).
double angleBetween(const Vec3& a, const Vec3& b) {
    return std::acos(std::clamp(dot(a, b) / (norm(a) * norm(b)), -1.0, 1.0));
}

} // namespace

TEST_CASE("A moon is where its planet is plus its own orbit", "[moons]") {
    const double t = epoch2027();
    for (const Body moon : kMoons) {
        CAPTURE(bodyName(moon));
        const MoonState rel = moonFromPlanet(moon, t);
        const Body planet = planetOf(moon);
        CHECK_THAT(norm(bodyPosition(moon, t) - bodyPosition(planet, t) - rel.r),
                   WithinAbs(0.0, 1.0));
        CHECK_THAT(norm(bodyVelocity(moon, t) - bodyVelocity(planet, t) - rel.v),
                   WithinAbs(0.0, 0.05));
        // Each moon is listed among its planet's.
        const auto moons = moonsOf(planet);
        CHECK(std::ranges::find(moons, moon) != moons.end());
    }
    CHECK(moonsOf(Body::Mars).empty());
    CHECK(moonsOf(Body::Titan).empty());
    CHECK(moonsOf(Body::Saturn).size() == 7);
    // Innermost first.
    CHECK(moonsOf(Body::Jupiter).front() == Body::Io);
    // Jupiter alone is a little lighter than Jupiter with its moons
    // (the four Galilean moons are about 2/10,000 of its mass).
    CHECK(planetMuAlone(Body::Jupiter) < bodyMu(Body::Jupiter));
    CHECK_THAT(planetMuAlone(Body::Jupiter), WithinRel(bodyMu(Body::Jupiter), 3e-4));
    CHECK(planetMuAlone(Body::Earth) == bodyMu(Body::Earth));
}

TEST_CASE("Among a giant's moons, each moon pulls from where it is", "[moons][gravity]") {
    const double t = epoch2027();
    // 10,000 km from Ganymede: its pull, μ/d², about 0.1 m/s², stands out
    // over what Jupiter's pull does there.
    const Vec3 ganymede = bodyPosition(Body::Ganymede, t);
    const Vec3 jupiter = bodyPosition(Body::Jupiter, t);
    const Vec3 out = normalized(ganymede - jupiter);
    const Vec3 near = ganymede + 1e7 * out; // on the far side from Jupiter
    const Vec3 a = heliocentricGravity(near, t, true);
    // The same point with Ganymede's pull taken away: Jupiter alone (and
    // the other moons and planets, which hardly differ over 10,000 km)...
    const Vec3 nearby = ganymede - 1e7 * out; // the near side, for comparison
    const Vec3 b = heliocentricGravity(nearby, t, true);
    // ...Ganymede pulls the far side towards Jupiter and the near side away:
    // the difference along the line is about 2 μ/d² (plus Jupiter's tide).
    const double ganymedePull = bodyMu(Body::Ganymede) / (1e7 * 1e7);
    CHECK(ganymedePull > 0.09);
    const double difference = dot(a - b, out);
    CHECK(difference < -1.5 * ganymedePull);
    // Far from Jupiter (beyond its sphere of influence), it and its moons
    // pull as one point: nothing changes there.
    const Vec3 far = jupiter + 1.2 * sphereOfInfluence(Body::Jupiter, t) * out;
    const Vec3 sunAndPlanets = heliocentricGravity(far, t, true);
    const Vec3 sunOnly = heliocentricGravity(far, t, false);
    const Vec3 jupiterPull =
        bodyMu(Body::Jupiter) * (jupiter - far) / std::pow(norm(jupiter - far), 3.0);
    CHECK_THAT(norm(sunAndPlanets - sunOnly - jupiterPull),
               WithinAbs(0.0, 0.05 * norm(jupiterPull)));
}

TEST_CASE("Inside a moon's sphere of influence, it dominates", "[moons]") {
    const double t = epoch2027();
    const auto near = [&](Body body, double distance) {
        return bodyPosition(body, t) + Vec3{distance, 0.0, 0.0};
    };
    CHECK(dominantBody(t, near(Body::Titan, 10e6)) == Body::Titan);
    CHECK(dominantBody(t, near(Body::Moon, 20e6)) == Body::Moon);
    CHECK(dominantBody(t, near(Body::Ganymede, 5e6)) == Body::Ganymede);
    // Close to the Earth, far from the Moon: the Earth.
    CHECK(dominantBody(t, near(Body::Earth, 7e6)) == Body::Earth);
    // Between Saturn's moons, Saturn.
    CHECK(dominantBody(t, near(Body::Titan, 300e6)) == Body::Saturn);
    // And the burn directions follow: relative to Titan, near Titan.
    const Voyage v = shipPassing(Body::Titan, t, 2000.0, 8e6, 2.0 * kHour);
    const BurnFrame frame = burnFrameAt(t, v.position, v.velocity);
    CHECK(frame.planet == Body::Titan);
    const Vec3 relV = v.velocity - bodyVelocity(Body::Titan, t);
    CHECK_THAT(dot(frame.prograde, normalized(relV)), WithinAbs(1.0, 1e-12));
}

TEST_CASE("A pass by Titan is bent by Titan's gravity", "[moons][flyby]") {
    // At v∞ = 2 km/s, 3 radii from its centre: e = 1 + r_p v∞²/μ ≈ 4.4, so
    // the path turns by δ = 2 asin(1/e) ≈ 26°. Saturn's tide, far weaker
    // here than Titan's pull, changes that by well under a degree.
    const double t = epoch2027();
    const double rp = 3.0 * bodyRadius(Body::Titan);
    const double vInf = 2000.0;
    // The low point along Titan's motion around Saturn: the pass turns v∞
    // along that motion or against it, which changes the speed around
    // Saturn most.
    const Vec3 titanMotion =
        normalized(bodyVelocity(Body::Titan, t) - bodyVelocity(Body::Saturn, t));
    const Voyage v = shipPassing(Body::Titan, t, vInf, rp, 3.0 * kHour, titanMotion);
    VoyageOptions options;
    options.stopAt = t + 6.0 * kHour;
    const VoyageResult result = flyVoyage(v, options);
    CHECK(result.end == VoyageEnd::StopTime);

    // The flyby is recorded, at the right distance.
    const auto pass = std::ranges::find_if(
        result.flybys, [](const PlanetPass& p) { return p.planet == Body::Titan; });
    REQUIRE(pass != result.flybys.end());
    CHECK_THAT(pass->distance, WithinRel(rp, 0.02));
    CHECK_THAT(pass->vInfinity, WithinRel(vInf, 0.02));
    const double mu = bodyMu(Body::Titan);
    const double e = 1.0 + rp * vInf * vInf / mu;
    const double turn = 2.0 * std::asin(1.0 / e);
    CHECK_THAT(pass->turnAngle, WithinAbs(turn, 0.01));
    CHECK(result.closestPasses.at(static_cast<std::size_t>(Body::Titan)).turning);

    // The flight really turned: the velocity relative to Titan at the end
    // against at the start. (Still 3 hours out, not yet along the
    // asymptotes: a few degrees short of δ.)
    const VoyageSample& last = result.samples.back();
    const Vec3 vIn = v.velocity - bodyVelocity(Body::Titan, t);
    const Vec3 vOut = last.v - bodyVelocity(Body::Titan, last.epoch);
    const double turned = angleBetween(vIn, vOut);
    CHECK(turned > turn - 0.1);
    CHECK(turned < turn + 0.02);

    // A gravity assist around Saturn: Titan moves at 5.6 km/s, so turning
    // the 2 km/s relative to it changes the speed around Saturn, by up to
    // the change in the relative velocity, 2 v∞ sin(δ/2) ≈ 0.9 km/s.
    CHECK(std::abs(pass->sunSpeedAfter - pass->sunSpeedBefore) > 500.0);
    CHECK(std::abs(pass->sunSpeedAfter - pass->sunSpeedBefore) <
          2.0 * vInf * std::sin(turn / 2.0) + 1.0);
    CHECK(pass->sunSpeedBefore < 12000.0); // around Saturn, not the Sun (~9.6 km/s for Saturn)
}

TEST_CASE("Flying into a moon ends the flight", "[moons]") {
    const auto moon = GENERATE(Body::Moon, Body::Europa, Body::Titan, Body::Triton);
    CAPTURE(bodyName(moon));
    const double t = epoch2027();
    // Straight at it from 20,000 km at 3 km/s (plus what its pull adds):
    // under two hours, too short for the planet's tide to make it miss.
    const Vec3 dir = normalized(Vec3{1.0, 2.0, 0.5});
    const Voyage v = shipAt(moon, t, {20e6 * dir, -3000.0 * dir});
    const VoyageResult result = flyVoyage(v);
    CHECK(result.end == VoyageEnd::HitPlanet);
    REQUIRE(result.hitPlanet);
    CHECK(*result.hitPlanet == moon);
    CHECK(result.endEpoch - t < 4.0 * kHour);
    const VoyageSample& last = result.samples.back();
    CHECK_THAT(norm(last.r - bodyPosition(moon, last.epoch)), WithinRel(bodyRadius(moon), 1e-3));
}

TEST_CASE("Parked low around a moon: Kepler's equations around it", "[moons][orbit]") {
    const double t = epoch2027();
    const Body moon = GENERATE(Body::Titan, Body::Moon, Body::Ganymede);
    CAPTURE(bodyName(moon));
    const double mu = bodyMu(moon);
    const double r = bodyRadius(moon) + (moon == Body::Titan ? 1500e3 : 100e3);
    const Voyage v = shipAt(moon, t, {{r, 0.0, 0.0}, {0.0, std::sqrt(mu / r), 0.0}});
    CHECK(parkedAround(t, v.position, v.velocity) == moon);
    VoyageOptions options;
    options.stopAt = t + 3.0 * kDay;
    const VoyageResult result = flyVoyage(v, options);
    CHECK(result.end == VoyageEnd::StopTime);
    REQUIRE(result.samples.size() > 1);
    CHECK(result.samples.front().parked == moon);
    // Still on the same circle around the moon after three days.
    for (const double after : {0.5 * kDay, 1.7 * kDay, 3.0 * kDay}) {
        const VoyageSample s = sampleAt(result, t + after);
        CHECK_THAT(norm(s.r - bodyPosition(moon, s.epoch)), WithinRel(r, 1e-6));
    }
}

TEST_CASE("A long orbit around a giant reaches out among its moons: it is flown, not parked",
          "[moons][orbit]") {
    // From 1.1 to 50 radii around Jupiter (like Juno's), crossing the
    // Galilean moons' orbits: their pulls matter, so the flight integrates.
    // Below Io's orbit, Kepler's equations still do.
    const double t = epoch2027();
    const double mu = bodyMu(Body::Jupiter);
    const double radius = bodyRadius(Body::Jupiter);
    const auto ellipse = [&](double rp, double ra) {
        const double vp = std::sqrt(2.0 * mu / rp - 2.0 * mu / (rp + ra));
        return shipAt(Body::Jupiter, t, {{rp, 0.0, 0.0}, {0.0, vp, 0.0}});
    };
    VoyageOptions options;
    options.stopAt = t + 2.0 * kDay;

    const VoyageResult wide = flyVoyage(ellipse(1.1 * radius, 50.0 * radius), options);
    CHECK(parkedAround(t, wide.samples.front().r, wide.samples.front().v) == Body::Jupiter);
    CHECK(std::ranges::none_of(wide.samples,
                               [](const VoyageSample& s) { return s.parked.has_value(); }));
    CHECK(wide.samples.size() > 50);

    const VoyageResult low = flyVoyage(ellipse(1.1 * radius, 3.0 * radius), options);
    CHECK(low.samples.front().parked == Body::Jupiter);

    // The same for the Earth: low orbits are parked, the Moon is far.
    const double rLeo = kRadiusEarth + 200e3;
    const Voyage leo = shipAt(
        Body::Earth, t, {{rLeo, 0.0, 0.0}, {0.0, std::sqrt(bodyMu(Body::Earth) / rLeo), 0.0}});
    CHECK(flyVoyage(leo, options).samples.front().parked == Body::Earth);
}

TEST_CASE("Reaching a moon, and how low an orbit may go", "[moons]") {
    const double t = epoch2027();
    // Titan's sphere of influence is wider than ten radii; Mimas's barely
    // clears its surface, so ten radii count instead.
    CHECK(reachRadius(Body::Titan, t) == sphereOfInfluence(Body::Titan, t));
    CHECK(sphereOfInfluence(Body::Mimas, t) < 2.0 * bodyRadius(Body::Mimas));
    CHECK(reachRadius(Body::Mimas, t) == 10.0 * bodyRadius(Body::Mimas));
    // Planets: their spheres of influence.
    CHECK(reachRadius(Body::Jupiter, t) == sphereOfInfluence(Body::Jupiter, t));
    // Titan's orbit must clear its haze; the Earth's, its air.
    CHECK(lowestOrbit(Body::Titan) > bodyRadius(Body::Titan) + 1000e3);
    CHECK_THAT(lowestOrbit(Body::Earth), WithinAbs(kRadiusEarth + 150e3, 1.0));
    CHECK_THAT(lowestOrbit(Body::Ganymede), WithinRel(1.02 * bodyRadius(Body::Ganymede), 1e-12));
}

TEST_CASE("Arriving at a moon, and settling into orbit around it", "[moons]") {
    // A ship on its way to Titan, 12 hours from a pass 3,000 km up (and
    // outside its 43,000 km sphere of influence), with its destination
    // Titan; braking at the low point into an orbit that clears the haze
    // meets the goal.
    const double t = epoch2027();
    const double rp = bodyRadius(Body::Titan) + 3000e3;
    Voyage v = shipPassing(Body::Titan, t, 1500.0, rp, 12.0 * kHour);
    REQUIRE(norm(v.position - bodyPosition(Body::Titan, t)) > reachRadius(Body::Titan, t));
    v.destination = Body::Titan;
    v.goal = Goal::ReachPlanet;
    const VoyageResult reached = flyVoyage(v);
    CHECK(reached.end == VoyageEnd::ReachedPlanet);
    CHECK(reached.goalMet);

    v.goal = Goal::PlanetOrbit;
    VoyageOptions twoDays;
    twoDays.stopAt = t + 2.0 * kDay;
    const VoyageResult passing = flyVoyage(v, twoDays);
    CHECK_FALSE(passing.goalMet);
    const PlanetPass& low = passing.closestPasses.at(static_cast<std::size_t>(Body::Titan));
    REQUIRE(low.valid);
    CHECK(low.turning);
    CHECK(low.distance > lowestOrbit(Body::Titan));
    // Brake at the low point to an ellipse out to 10,000 km (vis-viva).
    const double mu = bodyMu(Body::Titan);
    const double ra = 10000e3;
    const double wanted = std::sqrt(2.0 * mu / low.distance - 2.0 * mu / (low.distance + ra));
    v.burns = {{low.epoch, -(low.speed - wanted), 0.0, 0.0}};
    const VoyageResult staying = flyVoyage(v);
    CHECK(staying.end == VoyageEnd::OrbitingPlanet);
    CHECK(staying.goalMet);
    REQUIRE(staying.burns.size() == 1);
    CHECK(staying.burns.front().planet == Body::Titan);
}
