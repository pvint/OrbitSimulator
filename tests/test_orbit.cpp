// SPDX-License-Identifier: GPL-3.0-or-later
// Orbital elements and two-body propagation.

#include "orbit/core/angles.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/integrator.hpp"
#include "orbit/core/orbit.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

State gravityOnly(double /*t*/, const State& y) {
    const double r = norm(y.r);
    return {y.v, (-kMuEarth / (r * r * r)) * y.r, 0.0};
}

double specificEnergy(const State& y) { return normSquared(y.v) / 2.0 - kMuEarth / norm(y.r); }

/// Coast for `duration` seconds, landing exactly on the end time. Calls
/// `onStep` after every accepted step.
template <typename OnStep>
State coast(State y, double duration, OnStep onStep) {
    DormandPrince45 stepper(gravityOnly, Tolerances{});
    double t = 0.0;
    double h = 10.0;
    while (t < duration) {
        const double hStep = std::min(h, duration - t);
        const auto result = stepper.step(t, y, hStep);
        h = result.nextStep;
        if (!result.accepted) continue;
        y = result.y;
        t += hStep;
        onStep(y);
    }
    return y;
}

State coast(const State& y, double duration) {
    return coast(y, duration, [](const State&) {});
}

State fromElements(double a, double e, double i, double raan, double argp, double nu) {
    const auto pv = stateFromElements(a, e, i, raan, argp, nu);
    return {pv.r, pv.v, 1.0};
}

} // namespace

TEST_CASE("A circular 400 km orbit keeps its radius and energy over 10 orbits", "[orbit]") {
    const double r0 = kRadiusEarth + 400e3;
    const State start{{r0, 0.0, 0.0}, {0.0, circularSpeed(r0), 0.0}, 1.0};
    const double e0 = specificEnergy(start);

    double worstRadiusError = 0.0;
    const State end = coast(start, 10.0 * orbitalPeriod(r0), [&](const State& y) {
        worstRadiusError = std::max(worstRadiusError, std::abs(norm(y.r) - r0) / r0);
    });

    REQUIRE(worstRadiusError < 1e-3); // the prototype's requirement: 0.1 %
    REQUIRE(worstRadiusError < 1e-8); // what DP45 actually achieves
    REQUIRE_THAT(specificEnergy(end), WithinRel(e0, 1e-8));
    // After exactly 10 periods (about 430,000 km of travel) it is back where it
    // started, to within a few metres.
    REQUIRE(norm(end.r - start.r) < 10.0);
}

TEST_CASE("An ellipse returns to its starting point after one period", "[orbit]") {
    const double rp = kRadiusEarth + 300e3;
    const double ra = kRadiusEarth + 3000e3;
    const double a = (rp + ra) / 2.0;
    const State start =
        fromElements(a, (ra - rp) / (ra + rp), degToRad(40), degToRad(10), degToRad(70), 0.0);
    const double period = orbitalPeriod(a);

    REQUIRE_THAT(period, WithinRel(kTwoPi * std::sqrt(a * a * a / kMuEarth), 1e-15));
    REQUIRE_THAT(elementsFromState(start.r, start.v).period, WithinRel(period, 1e-9));

    const State end = coast(start, period);
    REQUIRE(norm(end.r - start.r) < 1.0);
    // Half a period later it is at apoapsis.
    REQUIRE_THAT(norm(coast(start, period / 2.0).r), WithinAbs(ra, 1.0));
}

TEST_CASE("Periapsis and apoapsis of a known ellipse", "[orbit]") {
    const double rp = kRadiusEarth + 250e3;
    const double ra = kRadiusEarth + 20000e3;
    const double a = (rp + ra) / 2.0;
    // At periapsis, speed from the vis-viva equation v² = μ(2/r − 1/a).
    const Vec3 r{rp, 0.0, 0.0};
    const Vec3 v{0.0, std::sqrt(kMuEarth * (2.0 / rp - 1.0 / a)), 0.0};
    const OrbitalElements el = elementsFromState(r, v);

    REQUIRE_THAT(el.periapsis, WithinAbs(rp, 1e-3));
    REQUIRE_THAT(el.apoapsis, WithinAbs(ra, 1e-3));
    REQUIRE_THAT(el.semiMajorAxis, WithinAbs(a, 1e-3));
    REQUIRE_THAT(el.eccentricity, WithinAbs((ra - rp) / (ra + rp), 1e-12));
    REQUIRE_THAT(el.periapsisAltitude, WithinAbs(250e3, 1e-3));
    REQUIRE(el.bound);
}

TEST_CASE("Elements survive a round trip through position and velocity", "[orbit]") {
    // a, e, i, Ω, ω, ν (degrees for readability)
    const auto [a, e, iDeg, raanDeg, argpDeg, nuDeg] =
        GENERATE(table<double, double, double, double, double, double>({
            {7.0e6, 0.1, 28.5, 40.0, 60.0, 30.0},    // ordinary prograde
            {8.0e6, 0.3, 90.0, 200.0, 300.0, 170.0}, // polar
            {9.0e6, 0.5, 150.0, 10.0, 45.0, 250.0},  // retrograde, past apoapsis
            {4.2e7, 0.01, 5.0, 350.0, 120.0, 359.0}, // nearly geostationary
        }));
    CAPTURE(a, e, iDeg, raanDeg, argpDeg, nuDeg);

    const State s =
        fromElements(a, e, degToRad(iDeg), degToRad(raanDeg), degToRad(argpDeg), degToRad(nuDeg));
    const OrbitalElements el = elementsFromState(s.r, s.v);

    REQUIRE_THAT(el.semiMajorAxis, WithinRel(a, 1e-10));
    REQUIRE_THAT(el.eccentricity, WithinAbs(e, 1e-10));
    REQUIRE_THAT(radToDeg(el.inclination), WithinAbs(iDeg, 1e-8));
    REQUIRE_THAT(radToDeg(el.raan), WithinAbs(raanDeg, 1e-8));
    REQUIRE_THAT(radToDeg(el.argumentOfPeriapsis), WithinAbs(argpDeg, 1e-6));
    REQUIRE_THAT(radToDeg(el.trueAnomaly), WithinAbs(nuDeg, 1e-6));
}

TEST_CASE("Circular and equatorial orbits report their undefined angles as 0", "[orbit]") {
    const double r = kRadiusEarth + 500e3;
    // Circular, equatorial, at 30° from the x axis.
    const Vec3 pos = rotateZ(Vec3{r, 0.0, 0.0}, degToRad(30.0));
    const Vec3 vel = rotateZ(Vec3{0.0, circularSpeed(r), 0.0}, degToRad(30.0));
    const OrbitalElements el = elementsFromState(pos, vel);

    REQUIRE_THAT(el.eccentricity, WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(el.inclination, WithinAbs(0.0, 1e-12));
    REQUIRE(el.raan == 0.0);
    REQUIRE(el.argumentOfPeriapsis == 0.0);
    // ν becomes the true longitude: the angle from the x axis.
    REQUIRE_THAT(radToDeg(el.trueAnomaly), WithinAbs(30.0, 1e-9));
}

TEST_CASE("A straight up-and-down path is bound with a finite apoapsis", "[orbit]") {
    const double v = 3000.0;
    const OrbitalElements el = elementsFromState({0.0, 0.0, kRadiusEarth}, {0.0, 0.0, v});
    // Energy conservation: v²/2 − μ/R = −μ/r_max
    const double rMax = 1.0 / (1.0 / kRadiusEarth - v * v / (2.0 * kMuEarth));

    REQUIRE(el.bound);
    REQUIRE_THAT(el.apoapsis, WithinRel(rMax, 1e-9));
    REQUIRE(el.periapsis == 0.0);
}

TEST_CASE("Escape speed gives zero energy", "[orbit]") {
    const double r = kRadiusEarth + 200e3;
    const OrbitalElements el = elementsFromState({r, 0.0, 0.0}, {0.0, escapeSpeed(r), 0.0});
    REQUIRE_THAT(el.energy, WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(el.eccentricity, WithinAbs(1.0, 1e-12));
    REQUIRE_FALSE(el.bound);
    REQUIRE(std::isinf(el.apoapsis));
}
