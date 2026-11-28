// SPDX-License-Identifier: GPL-3.0-or-later
// Kepler's and Lambert's problems (two_body.hpp).

#include "orbit/core/constants.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/orbit.hpp"
#include "orbit/core/two_body.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

double distance(const Vec3& a, const Vec3& b) { return norm(a - b); }

} // namespace

TEST_CASE("Stumpff functions join their series smoothly at zero", "[two-body]") {
    // Just either side of the switch to the series, the closed forms and the
    // series must agree.
    for (const double z : {-1.001e-3, -0.999e-3, 0.999e-3, 1.001e-3}) {
        const double c =
            z > 0 ? (1 - std::cos(std::sqrt(z))) / z : (std::cosh(std::sqrt(-z)) - 1) / -z;
        CHECK_THAT(stumpffC(z), WithinRel(c, 1e-9));
    }
    CHECK_THAT(stumpffC(0.0), WithinAbs(0.5, 1e-15));
    CHECK_THAT(stumpffS(0.0), WithinAbs(1.0 / 6.0, 1e-15));
    // One full turn of a circle: z = 4π², where C = 0.
    CHECK_THAT(stumpffC(4 * std::numbers::pi * std::numbers::pi), WithinAbs(0.0, 1e-15));
}

TEST_CASE("Kepler propagation follows a circular orbit", "[two-body]") {
    const double r = 7000e3;
    const double v = circularSpeed(r);
    const double period = orbitalPeriod(r);
    const TwoBodyState start{{r, 0, 0}, {0, v, 0}};

    const TwoBodyState quarter = propagateKepler(start, period / 4, kMuEarth);
    CHECK(distance(quarter.r, {0, r, 0}) < 1e-3);
    CHECK(distance(quarter.v, {-v, 0, 0}) < 1e-6);

    // Whole periods and negative times.
    const TwoBodyState later = propagateKepler(start, 3.5 * period, kMuEarth);
    CHECK(distance(later.r, {-r, 0, 0}) < 1e-2);
    const TwoBodyState earlier = propagateKepler(start, -period / 4, kMuEarth);
    CHECK(distance(earlier.r, {0, -r, 0}) < 1e-3);
}

TEST_CASE("Kepler propagation keeps energy and angular momentum on any conic", "[two-body]") {
    // An ellipse, a near-parabola and a hyperbola, around the Sun.
    const double speedFactor = GENERATE(0.8, 1.0, 1.4142, 1.4143, 2.0);
    const double r = kAstronomicalUnit;
    const TwoBodyState start{{r, 0, 0}, {0, speedFactor * circularSpeed(r, kMuSun), 3000.0}};
    const auto energy = [](const TwoBodyState& s) {
        return normSquared(s.v) / 2 - kMuSun / norm(s.r);
    };
    for (const double days : {10.0, 100.0, 400.0, -250.0}) {
        const TwoBodyState end = propagateKepler(start, days * 86400.0, kMuSun);
        CHECK_THAT(energy(end), WithinRel(energy(start), 1e-9));
        CHECK(distance(cross(end.r, end.v), cross(start.r, start.v)) <
              1e-9 * norm(cross(start.r, start.v)));
        // And back again.
        const TwoBodyState back = propagateKepler(end, -days * 86400.0, kMuSun);
        CHECK(distance(back.r, start.r) < 1.0);
    }
}

TEST_CASE("Lambert's problem: Curtis example 5.2", "[two-body]") {
    // H. D. Curtis, Orbital Mechanics for Engineering Students, example 5.2:
    // from (5000, 10000, 2100) km to (−14600, 2500, 7000) km in one hour.
    const Vec3 r1{5000e3, 10000e3, 2100e3};
    const Vec3 r2{-14600e3, 2500e3, 7000e3};
    const auto solution = solveLambert(r1, r2, 3600.0, 398600e9);
    REQUIRE(solution);
    CHECK(distance(solution->v1, {-5992.5, 1925.4, 3245.6}) < 1.0);
    CHECK(distance(solution->v2, {-3312.5, -4196.6, -385.29}) < 1.0);
}

TEST_CASE("Lambert solutions really join the two points", "[two-body]") {
    // Short way and long way, slow and fast, around the Sun: propagate the
    // solution and see that it arrives.
    const double au = kAstronomicalUnit;
    const Vec3 r1{au, 0.0, 0.0};
    const double angleDeg = GENERATE(30.0, 120.0, 200.0, 330.0);
    const double days = GENERATE(20.0, 150.0, 300.0);
    const double angle = angleDeg * std::numbers::pi / 180.0;
    const Vec3 r2{1.5 * au * std::cos(angle), 1.5 * au * std::sin(angle), 0.05 * au};
    const double dt = days * 86400.0;
    CAPTURE(angleDeg, days);

    const auto solution = solveLambert(r1, r2, dt, kMuSun);
    REQUIRE(solution);
    const TwoBodyState end = propagateKepler({r1, solution->v1}, dt, kMuSun);
    CHECK(distance(end.r, r2) < 1e-6 * au);
    CHECK(distance(end.v, solution->v2) < 1e-3);
    // Prograde: the orbit turns anticlockwise seen from the north.
    CHECK(cross(r1, solution->v1).z > 0.0);
}

TEST_CASE("Lambert gives the Hohmann transfer's speeds", "[two-body]") {
    // Earth's orbit to Mars's, half way round (just short of 180°, where the
    // plane is undefined): about Hohmann's numbers, departing ~2.9 km/s
    // faster than the Earth and arriving ~2.6 km/s slower than Mars.
    const double r1 = kAstronomicalUnit;
    const double r2 = 1.524 * kAstronomicalUnit;
    const double a = (r1 + r2) / 2;
    const double hohmannTime = std::numbers::pi * std::sqrt(a * a * a / kMuSun);
    const double angle = std::numbers::pi * (1.0 - 1e-4);
    const auto solution = solveLambert({r1, 0, 0}, {r2 * std::cos(angle), r2 * std::sin(angle), 0},
                                       hohmannTime, kMuSun);
    REQUIRE(solution);
    const double vDepart = std::sqrt(kMuSun * (2 / r1 - 1 / a));
    CHECK_THAT(norm(solution->v1), WithinRel(vDepart, 1e-3));
    CHECK_THAT(norm(solution->v1) - circularSpeed(r1, kMuSun), WithinAbs(2945.0, 15.0));
}

TEST_CASE("Lambert refuses impossible questions", "[two-body]") {
    const Vec3 r1{kAstronomicalUnit, 0, 0};
    // Exactly opposite: no plane.
    CHECK_FALSE(solveLambert(r1, -1.2 * r1, 200 * 86400.0, kMuSun));
    // No time at all.
    CHECK_FALSE(solveLambert(r1, Vec3{0, kAstronomicalUnit, 0}, 0.0, kMuSun));
}
