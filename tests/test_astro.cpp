// SPDX-License-Identifier: GPL-3.0-or-later
// Dates, Earth rotation, Sun and Moon positions, checked against known values.

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/ephemeris.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace orbit;
using Catch::Matchers::WithinAbs;

namespace {
double angleDiffDeg(double a, double b) {
    double d = std::fmod(radToDeg(a - b), 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}
} // namespace

TEST_CASE("Julian dates match published values", "[time]") {
    // Meeus, Astronomical Algorithms, example 7.a and table 7.a.
    REQUIRE_THAT(julianDate({2000, 1, 1, 12, 0, 0.0}), WithinAbs(2451545.0, 1e-9));
    REQUIRE_THAT(julianDate({1957, 10, 4, 19, 26, 24.0}), WithinAbs(2436116.31, 1e-6)); // Sputnik 1
    REQUIRE_THAT(julianDate({1987, 1, 27, 0, 0, 0.0}), WithinAbs(2446822.5, 1e-9));
    REQUIRE_THAT(julianDate({1600, 1, 1, 0, 0, 0.0}), WithinAbs(2305447.5, 1e-9));
}

TEST_CASE("Calendar dates round-trip through epochs and ISO text", "[time]") {
    const CalendarTime apollo{1969, 7, 16, 13, 32, 0.0};
    const double epoch = epochFromCalendar(apollo);
    // Back to a calendar time: equal to within floating-point rounding
    // (13:31:59.99999… is the same moment as 13:32:00).
    const CalendarTime back = calendarFromEpoch(epoch);
    REQUIRE(back.year == 1969);
    REQUIRE(back.month == 7);
    REQUIRE(back.day == 16);
    REQUIRE_THAT(epochFromCalendar(back), WithinAbs(epoch, 1e-3));

    REQUIRE(formatIsoUtc(epoch) == "1969-07-16T13:32:00Z");
    REQUIRE(parseIsoUtc("1969-07-16T13:32:00Z") == epoch);
    REQUIRE(parseIsoUtc("1969-07-16T13:32") == epoch);
    REQUIRE(formatIsoUtc(epochFromCalendar({2024, 2, 29, 23, 59, 59.6})) == "2024-03-01T00:00:00Z");
    REQUIRE(formatIsoUtc(0.0) == "2000-01-01T12:00:00Z");

    REQUIRE_FALSE(parseIsoUtc("1969-13-01T00:00:00Z"));
    REQUIRE_FALSE(parseIsoUtc("yesterday"));
    REQUIRE_FALSE(parseIsoUtc("1969-07-16T13:32:00Zextra"));
}

TEST_CASE("The Earth rotation angle matches the IERS definition", "[time]") {
    // At J2000.0 the angle is 0.7790572732640 turns = 280.46°.
    REQUIRE_THAT(radToDeg(earthRotationAngleAt(0.0)), WithinAbs(280.4606, 1e-3));
    // It gains 360.9856° per day: a full turn relative to the stars takes a
    // sidereal day, 23 h 56 min 4.1 s.
    const double siderealDay = kSecondsPerDay / 1.00273781191135448;
    REQUIRE_THAT(siderealDay, WithinAbs(86164.09, 0.01));
    REQUIRE_THAT(angleDiffDeg(earthRotationAngleAt(siderealDay), earthRotationAngleAt(0.0)),
                 WithinAbs(0.0, 1e-6));
    // And it is consistent with the simulation's constant rotation rate.
    REQUIRE_THAT(2.0 * kPi / siderealDay, WithinAbs(kOmegaEarth, 1e-18));
}

TEST_CASE("The Sun is about 1 AU away and moves 1° a day", "[ephemeris]") {
    // Perihelion in early January, aphelion in early July.
    const double january = norm(sunPosition(epochFromCalendar({2024, 1, 3, 0, 0, 0.0})));
    const double july = norm(sunPosition(epochFromCalendar({2024, 7, 5, 0, 0, 0.0})));
    REQUIRE_THAT(january / kAstronomicalUnit, WithinAbs(0.9833, 0.0005));
    REQUIRE_THAT(july / kAstronomicalUnit, WithinAbs(1.0167, 0.0005));

    // At the March equinox the Sun crosses the equator going north.
    const Vec3 equinox = sunPosition(epochFromCalendar({2024, 3, 20, 3, 6, 0.0}));
    REQUIRE_THAT(radToDeg(std::asin(equinox.z / norm(equinox))), WithinAbs(0.0, 0.02));
    // At the June solstice it stands 23.44° north.
    const Vec3 solstice = sunPosition(epochFromCalendar({2024, 6, 20, 20, 51, 0.0}));
    REQUIRE_THAT(radToDeg(std::asin(solstice.z / norm(solstice))), WithinAbs(23.44, 0.02));
}

TEST_CASE("The Moon's phases fall on the published dates", "[ephemeris]") {
    // New moon 2024-01-11 11:57 UTC, full moon 2024-01-25 17:54 UTC.
    const double newMoon = moonElongation(epochFromCalendar({2024, 1, 11, 11, 57, 0.0}));
    const double fullMoon = moonElongation(epochFromCalendar({2024, 1, 25, 17, 54, 0.0}));
    REQUIRE_THAT(angleDiffDeg(newMoon, 0.0), WithinAbs(0.0, 1.0));
    REQUIRE_THAT(angleDiffDeg(fullMoon, kPi), WithinAbs(0.0, 1.0));
}

TEST_CASE("The Moon stays 356,000-407,000 km away and circles in 27.3 days", "[ephemeris]") {
    const double start = epochFromCalendar({2024, 1, 1, 0, 0, 0.0});
    double nearest = 1e12;
    double farthest = 0.0;
    for (int hour = 0; hour < 24 * 365; hour += 6) {
        const double r = norm(moonPosition(start + hour * 3600.0));
        nearest = std::min(nearest, r);
        farthest = std::max(farthest, r);
    }
    REQUIRE(nearest > 355.5e6);
    REQUIRE(nearest < 360.0e6);
    REQUIRE(farthest > 404.0e6);
    REQUIRE(farthest < 407.5e6);

    // After one sidereal month the Moon is back in (almost) the same direction.
    const double month = 27.321661 * kSecondsPerDay;
    const Vec3 a = normalized(moonPosition(start));
    const Vec3 b = normalized(moonPosition(start + month));
    REQUIRE(radToDeg(std::acos(dot(a, b))) < 2.0);

    // Its velocity is about 1 km/s.
    REQUIRE_THAT(norm(moonVelocity(start)), WithinAbs(1022.0, 60.0));
}
