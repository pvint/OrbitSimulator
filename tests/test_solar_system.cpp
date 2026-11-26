// SPDX-License-Identifier: GPL-3.0-or-later
// The planets from JPL's approximate elements, checked against JPL Horizons.

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/orbit.hpp"
#include "orbit/core/solar_system.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// A heliocentric ecliptic J2000 position from JPL Horizons (km), and how
/// far ours may be from it: the tables' stated accuracy, as an angle seen
/// from the Sun and a distance from it.
/// Stands for Horizons's 1066-03-20, in its Julian calendar (see below).
constexpr CalendarTime kNormanConquestYear{1066, 3, 20, 0, 0, 0.0};

struct Reference {
    Body planet;
    CalendarTime when;
    Vec3 km;
    double angleArcsec;
    double radiusKm;
};

/// Angle between two directions (arcseconds).
double arcsecBetween(const Vec3& a, const Vec3& b) {
    const double c = std::clamp(dot(a, b) / (norm(a) * norm(b)), -1.0, 1.0);
    return radToDeg(std::acos(c)) * 3600.0;
}

} // namespace

TEST_CASE("Kepler's equation is solved for any ellipse", "[solar]") {
    const double e = GENERATE(0.0, 0.0167, 0.2, 0.6, 0.967);
    for (const double m : {-3.0, -1.0, 0.0, 0.5, 2.0, 3.1}) {
        const double ecc = eccentricAnomaly(m, e);
        CHECK_THAT(ecc - e * std::sin(ecc), WithinAbs(m, 1e-12));
    }
}

TEST_CASE("The ecliptic is the equator tilted by 23.4°", "[solar]") {
    // The ecliptic's north pole, seen from the equatorial frame, lies 23.4°
    // from the celestial pole, towards right ascension 18h (−y).
    const Vec3 pole = eclipticToEquatorial({0.0, 0.0, 1.0});
    CHECK_THAT(radToDeg(std::acos(pole.z)), WithinAbs(23.43928, 1e-9));
    CHECK(pole.y < 0.0);
    CHECK(norm(equatorialToEcliptic(pole) - Vec3{0.0, 0.0, 1.0}) < 1e-15);
    // The equinox direction is shared.
    CHECK(norm(eclipticToEquatorial({1.0, 0.0, 0.0}) - Vec3{1.0, 0.0, 0.0}) < 1e-15);
}

TEST_CASE("Planets are where JPL Horizons puts them", "[solar]") {
    // Horizons (DE441), heliocentric ecliptic J2000, 0h UTC. The tolerances
    // are JPL's stated errors for the approximate elements, with a margin:
    // table 1 for 2024, table 2 for 1066. (For 1066, Mars and Jupiter are
    // their systems' barycentres, which is what the tables describe.) In
    // 1066 our clock (UTC, taken as uniform) is about 27 minutes off the
    // tables' TDB (ΔT), in which fast Mercury moves about 270".
    const Reference ref =
        GENERATE(Reference{Body::Mercury,
                           {2024, 1, 1, 0, 0, 0.0},
                           {-4.108679314452778e7, 2.997117622127993e7, 6.217924601067722e6},
                           30.0,
                           2e3},
                 Reference{Body::Earth,
                           {2024, 1, 1, 0, 0, 0.0},
                           {-2.481305780560024e7, 1.449945066044991e8, -8.215121736310422e3},
                           30.0,
                           10e3},
                 Reference{Body::Mars,
                           {2024, 1, 1, 0, 0, 0.0},
                           {-4.388406836547421e7, -2.170851147980210e8, -3.473053082961291e6},
                           60.0,
                           40e3},
                 Reference{Body::Jupiter,
                           {2024, 1, 1, 0, 0, 0.0},
                           {5.225702016784671e8, 5.318275595243819e8, -1.390072099879226e7},
                           600.0,
                           900e3},
                 Reference{Body::Mercury,
                           kNormanConquestYear,
                           {-5.816083329431534e6, -6.913773977923742e7, -5.014266528713096e6},
                           300.0,
                           3e3},
                 Reference{Body::Earth,
                           kNormanConquestYear,
                           {-1.425982186735957e8, -4.624804425537154e7, -1.146012407525703e5},
                           120.0,
                           25e3},
                 Reference{Body::Mars,
                           kNormanConquestYear,
                           {-2.430936640586988e8, -2.712911810888930e7, 5.898599290618628e6},
                           200.0,
                           50e3},
                 Reference{Body::Jupiter,
                           kNormanConquestYear,
                           {-6.146938329410907e8, 5.107398580334556e8, 1.223373455972084e7},
                           900.0,
                           1500e3});

    // Horizons gives dates before 1582 in the Julian calendar: its
    // "1066-03-20" is JD 2110492.5, 26 March in our (Gregorian) calendar.
    const double epoch =
        ref.when.year == 1066 ? (2110492.5 - 2451545.0) * 86400.0 : epochFromCalendar(ref.when);
    const Vec3 ours = bodyPosition(ref.planet, epoch);
    const Vec3 jpl = ref.km * 1000.0;
    INFO(bodyName(ref.planet) << " " << ref.when.year << ": " << arcsecBetween(ours, jpl)
                              << "\" off, distance off by "
                              << std::abs(norm(ours) - norm(jpl)) / 1000.0 << " km");
    CHECK(arcsecBetween(ours, jpl) < ref.angleArcsec);
    CHECK(std::abs(norm(ours) - norm(jpl)) < ref.radiusKm * 1000.0);
}

TEST_CASE("Dates are Gregorian both ways, even before 1582", "[solar][time]") {
    for (const CalendarTime t :
         {CalendarTime{1066, 3, 26, 12, 0, 0.0}, CalendarTime{-500, 1, 1, 0, 0, 0.0},
          CalendarTime{1582, 10, 4, 0, 0, 0.0}}) {
        const CalendarTime back = calendarFromEpoch(epochFromCalendar(t));
        CHECK(back.year == t.year);
        CHECK(back.month == t.month);
        CHECK(back.day == t.day);
        CHECK(back.hour == t.hour);
    }
    // Julian 1066-03-20 (JD 2110492.5) is Gregorian 1066-03-26.
    const CalendarTime t = calendarFromJulianDate(2110492.5);
    CHECK(t.day == 26);
    CHECK(julianDate({1066, 3, 26, 0, 0, 0.0}) == 2110492.5);
}

TEST_CASE("The Earth and Moon go round their barycentre", "[solar]") {
    const double epoch = epochFromCalendar({2024, 6, 1, 0, 0, 0.0});
    const Vec3 earth = bodyPosition(Body::Earth, epoch);
    const Vec3 moon = moonHeliocentric(epoch);
    // The Moon is where the geocentric series says, about 384,000 km away.
    CHECK_THAT(norm(moon - earth), WithinRel(norm(moonPosition(epoch)), 1e-12));
    // The barycentre lies between them, 1/82 of the way from the Earth.
    const Vec3 barycentre = earth + (moon - earth) / 82.3;
    CHECK(norm(barycentre - earth) > 4000e3);
    CHECK(norm(barycentre - earth) < 5000e3);
}

TEST_CASE("The Earth goes round the Sun in a year at 30 km/s", "[solar]") {
    const double epoch = epochFromCalendar({2024, 3, 20, 0, 0, 0.0});
    CHECK_THAT(norm(bodyVelocity(Body::Earth, epoch)), WithinRel(29.8e3, 0.03));
    // A sidereal year later it is back where it was (to within the Moon's
    // pull on it, a few thousand km).
    const double year = 365.25636 * 86400.0;
    CHECK(norm(bodyPosition(Body::Earth, epoch + year) - bodyPosition(Body::Earth, epoch)) <
          20000e3);
    // And Jupiter's period follows from Kepler's third law.
    const PlanetOrbit jupiter = planetOrbit(Body::Jupiter, epoch);
    CHECK_THAT(orbitalPeriod(jupiter.semiMajorAxis, kMuSun) / (365.25 * 86400.0),
               WithinRel(11.86, 0.01));
}

TEST_CASE("Moons go round their planets at the right distance and period", "[moons]") {
    const double t = 8.0e8; // early 2025
    // Titan: 1.22 million km from Saturn, once round in 15.95 days.
    const Vec3 titan = bodyPosition(Body::Titan, t) - bodyPosition(Body::Saturn, t);
    CHECK_THAT(norm(titan), WithinRel(1.2219e9, 0.04));
    const double period =
        2.0 * std::numbers::pi *
        std::sqrt(std::pow(moonOrbit(Body::Titan).semiMajorAxis, 3) / bodyMu(Body::Saturn));
    CHECK_THAT(period / 86400.0, WithinRel(15.945, 0.01));
    const Vec3 later =
        bodyPosition(Body::Titan, t + period) - bodyPosition(Body::Saturn, t + period);
    CHECK(norm(later - titan) < 1e5); // back where it was, to 100 km
    // Its velocity is its planet's plus its own on its orbit.
    const Vec3 v = bodyVelocity(Body::Titan, t);
    const Vec3 dv =
        (bodyPosition(Body::Titan, t + 60.0) - bodyPosition(Body::Titan, t - 60.0)) / 120.0;
    CHECK(norm(v - dv) < 1.0);
    CHECK(planetOf(Body::Titan) == Body::Saturn);
    CHECK(planetOf(Body::Moon) == Body::Earth);
    CHECK(isMoon(Body::Io));
    CHECK_FALSE(isMoon(Body::Neptune));
}

TEST_CASE("Moons orbit in their planet's equator; Triton goes backwards", "[moons]") {
    const double t = 8.0e8;
    const auto normal = [&](Body moon) {
        const Vec3 r = bodyPosition(moon, t) - bodyPosition(planetOf(moon), t);
        const Vec3 v = bodyVelocity(moon, t) - bodyVelocity(planetOf(moon), t);
        return normalized(cross(r, v));
    };
    // Io within a degree of Jupiter's pole; Saturn's moons tipped 27° to
    // the ecliptic with their planet; Uranus's nearly on their side.
    CHECK(dot(normal(Body::Io), moonOrbit(Body::Io).pole) > std::cos(1.0 * std::numbers::pi / 180));
    CHECK(std::abs(dot(normal(Body::Titan), Vec3{0, 0, 1})) <
          std::cos(20.0 * std::numbers::pi / 180));
    CHECK(std::abs(dot(normal(Body::Titania), Vec3{0, 0, 1})) < 0.3);
    CHECK(dot(normal(Body::Triton), moonOrbit(Body::Triton).pole) < -0.9);
}

TEST_CASE("A moon's sphere of influence is set by its planet's pull", "[moons]") {
    // a (μ_moon / μ_planet)^(2/5): Titan's about 43,000 km, the Moon's 66,000.
    CHECK_THAT(sphereOfInfluence(Body::Titan, 0.0), WithinRel(4.33e7, 0.05));
    CHECK_THAT(sphereOfInfluence(Body::Moon, 0.0), WithinRel(6.6e7, 0.05));
    for (const Body moon : kMoons) {
        CAPTURE(bodyName(moon));
        CHECK(sphereOfInfluence(moon, 0.0) > bodyRadius(moon));
        CHECK(sphereOfInfluence(moon, 0.0) < 0.2 * moonOrbit(moon).semiMajorAxis);
    }
}
