// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/solar_system.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/natural_body.hpp"
#include "orbit/core/orbit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace orbit {

namespace {

/// One planet's row of a table: each element at J2000 and its rate per
/// Julian century. a in au, angles in degrees: I the inclination, L the mean
/// longitude, ϖ ("long. peri.") the longitude of perihelion, Ω the node.
struct ElementRow {
    double a, e, i, l, varpi, node;
    double aRate, eRate, iRate, lRate, varpiRate, nodeRate;
};

/// Table 2b: extra terms in the mean anomaly of the outer planets, for
/// table 2a (degrees; f in degrees per century):
/// M += b T² + c cos(f T) + s sin(f T).
struct ExtraTerms {
    double b, c, s, f;
};

// Table 1, for 1800–2050. Order: kAllPlanets (the Earth row is the
// Earth–Moon barycentre).
constexpr std::array<ElementRow, 8> kTable1{{
    {0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593, //
     0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081},
    {0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255, //
     0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418},
    {1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0, //
     0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0},
    {1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891, //
     0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343},
    {5.20288700, 0.04838624, 1.30439695, 34.39644051, 14.72847983, 100.47390909, //
     -0.00011607, -0.00013253, -0.00183714, 3034.74612775, 0.21252668, 0.20469106},
    {9.53667594, 0.05386179, 2.48599187, 49.95424423, 92.59887831, 113.66242448, //
     -0.00125060, -0.00050991, 0.00193609, 1222.49362201, -0.41897216, -0.28867794},
    {19.18916464, 0.04725744, 0.77263783, 313.23810451, 170.95427630, 74.01692503, //
     -0.00196176, -0.00004397, -0.00242939, 428.48202785, 0.40805281, 0.04240589},
    {30.06992276, 0.00859048, 1.77004347, -55.12002969, 44.96476227, 131.78422574, //
     0.00026291, 0.00005105, 0.00035372, 218.45945325, -0.32241464, -0.00508664},
}};

// Table 2a, for 3000 BC – 3000 AD. (JPL's numbers: one happens to be near
// 1/π, which clang-tidy would like to "fix".)
// NOLINTBEGIN(modernize-use-std-numbers)
constexpr std::array<ElementRow, 8> kTable2a{{
    {0.38709843, 0.20563661, 7.00559432, 252.25166724, 77.45771895, 48.33961819, //
     0.00000000, 0.00002123, -0.00590158, 149472.67486623, 0.15940013, -0.12214182},
    {0.72332102, 0.00676399, 3.39777545, 181.97970850, 131.76755713, 76.67261496, //
     -0.00000026, -0.00005107, 0.00043494, 58517.81560260, 0.05679648, -0.27274174},
    {1.00000018, 0.01673163, -0.00054346, 100.46691572, 102.93005885, -5.11260389, //
     -0.00000003, -0.00003661, -0.01337178, 35999.37306329, 0.31795260, -0.24123856},
    {1.52371243, 0.09336511, 1.85181869, -4.56813164, -23.91744784, 49.71320984, //
     0.00000097, 0.00009149, -0.00724757, 19140.29934243, 0.45223625, -0.26852431},
    {5.20248019, 0.04853590, 1.29861416, 34.33479152, 14.27495244, 100.29282654, //
     -0.00002864, 0.00018026, -0.00322699, 3034.90371757, 0.18199196, 0.13024619},
    {9.54149883, 0.05550825, 2.49424102, 50.07571329, 92.86136063, 113.63998702, //
     -0.00003065, -0.00032044, 0.00451969, 1222.11494724, 0.54179478, -0.25015002},
    {19.18797948, 0.04685740, 0.77298127, 314.20276625, 172.43404441, 73.96250215, //
     -0.00020455, -0.00001550, -0.00180155, 428.49512595, 0.09266985, 0.05739699},
    {30.06952752, 0.00895439, 1.77005520, 304.22289287, 46.68158724, 131.78635853, //
     0.00006447, 0.00000818, 0.00022400, 218.46515314, 0.01009938, -0.00606302},
}};
// NOLINTEND(modernize-use-std-numbers)

// Table 2b (zero for Mercury to Mars).
constexpr std::array<ExtraTerms, 8> kTable2b{{
    {0.0, 0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0, 0.0},
    {-0.00012452, 0.06064060, -0.35635438, 38.35125000},
    {0.00025899, -0.13434469, 0.87320147, 38.35125000},
    {0.00058331, -0.97731848, 0.17689245, 7.67025000},
    {-0.00041348, 0.68346318, -0.10162547, 7.67025000},
}};

// μ (km³/s²) from DE440, planet with its moons except the Earth; mean radii
// (km) from JPL's planetary physical parameters.
constexpr std::array<double, 8> kMuKm3{22031.868551,   324858.592000,    398600.435507,
                                       42828.375816,   126712764.100000, 37940584.841800,
                                       5794556.400000, 6836527.100580};
constexpr std::array<double, 8> kRadiusKm{2439.4,  6051.8,  6371.0,  3389.5,
                                          69911.0, 58232.0, 25362.0, 24622.0};

constexpr double kMuMoonKm3 = 4902.800118;

std::size_t index(Planet planet) { return static_cast<std::size_t>(planet); }

/// Table 1 between 1800 and 2050, where it is the more accurate; table 2
/// elsewhere.
bool useTable1(double t) { return t >= -2.0 && t <= 0.5; }

/// Heliocentric ecliptic position of a planet's (for the Earth row, the
/// Earth–Moon barycentre's) orbit, from the tables, as JPL's formulae say.
Vec3 tablePosition(Planet planet, double epoch) {
    const PlanetOrbit o = planetOrbit(planet, epoch);
    const double ecc = eccentricAnomaly(o.meanAnomaly, o.eccentricity);
    // In the orbit's plane, x towards perihelion.
    const Vec3 inPlane{
        o.semiMajorAxis * (std::cos(ecc) - o.eccentricity),
        o.semiMajorAxis * std::sqrt(1.0 - o.eccentricity * o.eccentricity) * std::sin(ecc), 0.0};
    return rotateZ(rotateX(rotateZ(inPlane, o.argumentOfPeriapsis), o.inclination), o.raan);
}

/// The Moon relative to the Earth, in the ecliptic J2000 frame (m).
Vec3 moonFromEarth(double epoch) {
    return equatorialToEcliptic(precessionToJ2000(moonPosition(epoch), epoch));
}

} // namespace

std::string_view planetName(Planet planet) {
    switch (planet) {
    case Planet::Mercury:
        return "Mercury";
    case Planet::Venus:
        return "Venus";
    case Planet::Earth:
        return "Earth";
    case Planet::Mars:
        return "Mars";
    case Planet::Jupiter:
        return "Jupiter";
    case Planet::Saturn:
        return "Saturn";
    case Planet::Uranus:
        return "Uranus";
    case Planet::Neptune:
        return "Neptune";
    }
    return {};
}

double planetMu(Planet planet) { return kMuKm3.at(index(planet)) * 1e9; }

double planetRadius(Planet planet) { return kRadiusKm.at(index(planet)) * 1e3; }

PlanetOrbit planetOrbit(Planet planet, double epoch) {
    const double t = julianCenturies(epoch);
    const bool table1 = useTable1(t);
    const ElementRow& row = (table1 ? kTable1 : kTable2a).at(index(planet));
    const double a = row.a + row.aRate * t;
    const double e = row.e + row.eRate * t;
    const double i = row.i + row.iRate * t;
    const double l = row.l + row.lRate * t;
    const double varpi = row.varpi + row.varpiRate * t;
    const double node = row.node + row.nodeRate * t;

    // ω = ϖ − Ω; M = L − ϖ (+ table 2b's terms for the outer planets).
    double m = l - varpi;
    if (!table1) {
        const ExtraTerms& x = kTable2b.at(index(planet));
        m += x.b * t * t + x.c * std::cos(degToRad(x.f * t)) + x.s * std::sin(degToRad(x.f * t));
    }
    PlanetOrbit o;
    o.semiMajorAxis = a * kAstronomicalUnit;
    o.eccentricity = e;
    o.inclination = degToRad(i);
    o.raan = degToRad(node);
    o.argumentOfPeriapsis = degToRad(varpi - node);
    o.meanAnomaly = degToRad(m);
    return o;
}

Vec3 planetPosition(Planet planet, double epoch) {
    const Vec3 r = tablePosition(planet, epoch);
    if (planet != Planet::Earth) return r;
    // The table gives the Earth–Moon barycentre; the Earth is 1/82 of the
    // way from it to the Moon, on the other side.
    const double moonShare = kMuMoonKm3 / (kMuKm3.at(index(Planet::Earth)) + kMuMoonKm3);
    return r - moonShare * moonFromEarth(epoch);
}

Vec3 planetVelocity(Planet planet, double epoch) {
    constexpr double dt = 1800.0;
    return (planetPosition(planet, epoch + dt) - planetPosition(planet, epoch - dt)) / (2.0 * dt);
}

Vec3 moonHeliocentric(double epoch) {
    return planetPosition(Planet::Earth, epoch) + moonFromEarth(epoch);
}

Vec3 eclipticToEquatorial(const Vec3& v) { return rotateX(v, kObliquityJ2000); }

Vec3 equatorialToEcliptic(const Vec3& v) { return rotateX(v, -kObliquityJ2000); }

} // namespace orbit
