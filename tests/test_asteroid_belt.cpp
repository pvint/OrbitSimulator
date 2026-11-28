// SPDX-License-Identifier: GPL-3.0-or-later
// The asteroid belt drawn in the game (asteroid_belt.hpp).

#include "orbit/core/asteroid_belt.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/solar_system.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>

using namespace orbit;
using Catch::Matchers::WithinAbs;

TEST_CASE("The Kirkwood gaps are where Kepler's third law puts them", "[belt]") {
    // a = a_J k^(−2/3): about 2.50, 2.82, 2.95 and 3.28 au.
    const auto gaps = kirkwoodGaps();
    REQUIRE(gaps.size() == 4);
    CHECK_THAT(gaps[0] / kAstronomicalUnit, WithinAbs(2.50, 0.02));
    CHECK_THAT(gaps[1] / kAstronomicalUnit, WithinAbs(2.82, 0.02));
    CHECK_THAT(gaps[2] / kAstronomicalUnit, WithinAbs(2.95, 0.02));
    CHECK_THAT(gaps[3] / kAstronomicalUnit, WithinAbs(3.28, 0.02));
}

TEST_CASE("The belt: between Mars and Jupiter, the gaps empty", "[belt]") {
    const auto belt = makeAsteroidBelt(3000, 7);
    REQUIRE(belt.size() == 3000);
    // The same seed, the same belt.
    CHECK(makeAsteroidBelt(10, 7)[3].semiMajorAxis == belt[3].semiMajorAxis);
    int inGap = 0;
    int nearGap = 0;
    const double gap = kirkwoodGaps()[0];
    for (const Asteroid& a : belt) {
        const double d = std::abs(a.semiMajorAxis - gap) / kAstronomicalUnit;
        if (d < 0.03) ++inGap;
        if (d > 0.05 && d < 0.08) ++nearGap;
        const double r = norm(asteroidPosition(a, 1e8));
        CHECK(r > 1.4 * kAstronomicalUnit);
        CHECK(r < 4.3 * kAstronomicalUnit);
    }
    CHECK(inGap == 0);
    CHECK(nearGap > 50);
}

TEST_CASE("An asteroid goes round in the period Kepler's third law gives", "[belt]") {
    const Asteroid a = makeAsteroidBelt(1, 3).front();
    const double period = 2.0 * std::numbers::pi * std::sqrt(std::pow(a.semiMajorAxis, 3) / kMuSun);
    const Vec3 p0 = asteroidPosition(a, 0.0);
    CHECK(norm(asteroidPosition(a, period) - p0) < 1e3);        // back where it was
    CHECK(norm(asteroidPosition(a, 0.5 * period) - p0) > 1e11); // across the Sun
}
