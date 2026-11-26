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
constexpr double kRadiusMoonKm = 1737.4;

std::size_t index(Body planet) { return static_cast<std::size_t>(planet); }

/// The moons other than ours: semi-major axis (km), eccentricity,
/// inclination to the planet's equator (°), μ (km³/s²), mean radius (km),
/// from JPL's satellite mean elements and physical parameters; and a mean
/// anomaly at J2000 (°) that only spreads them round their orbits.
struct MoonRow {
    Body planet;
    double aKm, e, iDeg, muKm3, radiusKm, m0Deg;
};
constexpr std::array<MoonRow, kMoons.size() - 1> kMoonRows{{
    {Body::Jupiter, 421700.0, 0.0041, 0.050, 5959.916, 1821.6, 106.1},     // Io
    {Body::Jupiter, 671034.0, 0.0094, 0.471, 3202.739, 1560.8, 175.7},     // Europa
    {Body::Jupiter, 1070412.0, 0.0013, 0.204, 9887.834, 2631.2, 120.6},    // Ganymede
    {Body::Jupiter, 1882709.0, 0.0074, 0.205, 7179.289, 2410.3, 84.4},     // Callisto
    {Body::Saturn, 185539.0, 0.0196, 1.574, 2.503, 198.2, 10.0},           // Mimas
    {Body::Saturn, 237948.0, 0.0047, 0.009, 7.211, 252.1, 60.0},           // Enceladus
    {Body::Saturn, 294619.0, 0.0001, 1.091, 41.21, 531.1, 120.0},          // Tethys
    {Body::Saturn, 377396.0, 0.0022, 0.028, 73.116, 561.4, 200.0},         // Dione
    {Body::Saturn, 527108.0, 0.0013, 0.331, 153.94, 763.8, 270.0},         // Rhea
    {Body::Saturn, 1221870.0, 0.0288, 0.312, 8978.14, 2574.7, 30.0},       // Titan
    {Body::Saturn, 3560820.0, 0.0283, 15.47, 120.51, 734.5, 300.0},        // Iapetus
    {Body::Uranus, 129390.0, 0.0013, 4.338, 4.4, 235.8, 50.0},             // Miranda
    {Body::Uranus, 191020.0, 0.0012, 0.041, 83.46, 578.9, 140.0},          // Ariel
    {Body::Uranus, 266300.0, 0.0039, 0.128, 85.09, 584.7, 220.0},          // Umbriel
    {Body::Uranus, 435910.0, 0.0011, 0.079, 226.94, 788.9, 310.0},         // Titania
    {Body::Uranus, 583520.0, 0.0014, 0.068, 205.32, 761.4, 20.0},          // Oberon
    {Body::Neptune, 354759.0, 0.000016, 156.885, 1428.495, 1353.4, 100.0}, // Triton
}};

const MoonRow& moonRow(Body moon) { return kMoonRows.at(index(moon) - index(Body::Io)); }

/// A giant planet's north pole, from the IAU's right ascension and
/// declination of the pole at J2000 (degrees), turned into the ecliptic.
Vec3 poleOf(Body planet) {
    double ra = 0.0;
    double dec = 90.0;
    switch (planet) {
    case Body::Jupiter:
        ra = 268.056595;
        dec = 64.495303;
        break;
    case Body::Saturn:
        ra = 40.589;
        dec = 83.537;
        break;
    case Body::Uranus: // tipped right over: its pole lies almost in its orbit's plane
        ra = 257.311;
        dec = -15.175;
        break;
    case Body::Neptune:
        ra = 299.36;
        dec = 43.46;
        break;
    default:
        break;
    }
    const double a = degToRad(ra);
    const double d = degToRad(dec);
    return equatorialToEcliptic(
        Vec3{std::cos(d) * std::cos(a), std::cos(d) * std::sin(a), std::sin(d)});
}

/// A giant planet's equatorial axes in the ecliptic frame: x along the line
/// where its equator crosses the ecliptic, z its pole. Worked out once (the
/// moons are asked for at every step of a flight near a giant planet).
struct EquatorAxes {
    Vec3 x, y, z;
};
const EquatorAxes& equatorOf(Body planet) {
    static const std::array<EquatorAxes, 4> axes = [] {
        std::array<EquatorAxes, 4> all{};
        for (std::size_t i = 0; i < all.size(); ++i) {
            const Vec3 z = poleOf(static_cast<Body>(index(Body::Jupiter) + i));
            const Vec3 x = normalized(cross(Vec3{0.0, 0.0, 1.0}, z));
            all.at(i) = {x, cross(z, x), z};
        }
        return all;
    }();
    return axes.at(index(planet) - index(Body::Jupiter));
}

/// A moon (not ours) relative to its planet: Kepler's equation on its mean
/// elements, in the planet's equatorial plane, turned into the ecliptic.
PositionVelocity moonRelative(Body moon, double epoch) {
    const MoonRow& row = moonRow(moon);
    const double mu = bodyMu(row.planet);
    const double a = row.aKm * 1e3;
    const double e = row.e;
    const double n = std::sqrt(mu / (a * a * a));
    const double meanAnomaly = std::fmod(degToRad(row.m0Deg) + n * epoch, 2.0 * std::numbers::pi);
    const double bigE = eccentricAnomaly(meanAnomaly, e);
    const double nu = 2.0 * std::atan2(std::sqrt(1.0 + e) * std::sin(bigE / 2.0),
                                       std::sqrt(1.0 - e) * std::cos(bigE / 2.0));
    const PositionVelocity inPlane = stateFromElements(a, e, degToRad(row.iDeg), 0.0, 0.0, nu, mu);
    const EquatorAxes& eq = equatorOf(row.planet);
    const auto turn = [&](const Vec3& w) { return w.x * eq.x + w.y * eq.y + w.z * eq.z; };
    return {turn(inPlane.r), turn(inPlane.v)};
}

/// Table 1 between 1800 and 2050, where it is the more accurate; table 2
/// elsewhere.
bool useTable1(double t) { return t >= -2.0 && t <= 0.5; }

/// Heliocentric ecliptic position of a planet's (for the Earth row, the
/// Earth–Moon barycentre's) orbit, from the tables, as JPL's formulae say.
Vec3 tablePosition(Body planet, double epoch) {
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

bool isMoon(Body body) { return index(body) >= index(Body::Moon); }

Body planetOf(Body body) {
    if (!isMoon(body)) return body;
    if (body == Body::Moon) return Body::Earth;
    return moonRow(body).planet;
}

MoonOrbit moonOrbit(Body moon) {
    if (moon == Body::Moon) {
        // Ours: about 384,400 km, e 0.055, tilted 5.1° to the ecliptic.
        return {384400e3, 0.0549, degToRad(5.145), Vec3{0.0, 0.0, 1.0}};
    }
    const MoonRow& row = moonRow(moon);
    return {row.aKm * 1e3, row.e, degToRad(row.iDeg), poleOf(row.planet)};
}

std::span<const Body> moonsOf(Body planet) {
    // kMoons keeps each planet's moons together, innermost first.
    const std::span<const Body> all(kMoons);
    switch (planet) {
    case Body::Earth:
        return all.subspan(0, 1);
    case Body::Jupiter:
        return all.subspan(1, 4);
    case Body::Saturn:
        return all.subspan(5, 7);
    case Body::Uranus:
        return all.subspan(12, 5);
    case Body::Neptune:
        return all.subspan(17, 1);
    default:
        return {};
    }
}

MoonState moonFromPlanet(Body moon, double epoch) {
    if (moon == Body::Moon) {
        constexpr double dt = 1800.0;
        return {moonFromEarth(epoch),
                (moonFromEarth(epoch + dt) - moonFromEarth(epoch - dt)) / (2.0 * dt)};
    }
    const PositionVelocity rel = moonRelative(moon, epoch);
    return {rel.r, rel.v};
}

double planetMuAlone(Body planet) {
    if (planet == Body::Earth) return bodyMu(planet);
    double mu = bodyMu(planet);
    for (const Body moon : moonsOf(planet)) mu -= bodyMu(moon);
    return mu;
}

std::string_view bodyName(Body body) {
    switch (body) {
    case Body::Mercury:
        return "Mercury";
    case Body::Venus:
        return "Venus";
    case Body::Earth:
        return "Earth";
    case Body::Mars:
        return "Mars";
    case Body::Jupiter:
        return "Jupiter";
    case Body::Saturn:
        return "Saturn";
    case Body::Uranus:
        return "Uranus";
    case Body::Neptune:
        return "Neptune";
    case Body::Moon:
        return "Moon";
    case Body::Io:
        return "Io";
    case Body::Europa:
        return "Europa";
    case Body::Ganymede:
        return "Ganymede";
    case Body::Callisto:
        return "Callisto";
    case Body::Mimas:
        return "Mimas";
    case Body::Enceladus:
        return "Enceladus";
    case Body::Tethys:
        return "Tethys";
    case Body::Dione:
        return "Dione";
    case Body::Rhea:
        return "Rhea";
    case Body::Titan:
        return "Titan";
    case Body::Iapetus:
        return "Iapetus";
    case Body::Miranda:
        return "Miranda";
    case Body::Ariel:
        return "Ariel";
    case Body::Umbriel:
        return "Umbriel";
    case Body::Titania:
        return "Titania";
    case Body::Oberon:
        return "Oberon";
    case Body::Triton:
        return "Triton";
    }
    return {};
}

std::optional<Body> bodyFromKey(std::string_view key) {
    const auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (const Body body : kAllBodies) {
        const std::string_view name = bodyName(body);
        if (name.size() == key.size() && std::ranges::equal(name, key, {}, lower, lower)) {
            return body;
        }
    }
    return std::nullopt;
}

double bodyMu(Body body) {
    if (body == Body::Moon) return kMuMoonKm3 * 1e9;
    if (isMoon(body)) return moonRow(body).muKm3 * 1e9;
    return kMuKm3.at(index(body)) * 1e9;
}

double bodyRadius(Body body) {
    if (body == Body::Moon) return kRadiusMoonKm * 1e3;
    if (isMoon(body)) return moonRow(body).radiusKm * 1e3;
    return kRadiusKm.at(index(body)) * 1e3;
}

PlanetOrbit planetOrbit(Body body, double epoch) {
    const Body planet = planetOf(body);
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

double sphereOfInfluence(Body body, double epoch) {
    if (isMoon(body)) {
        const Body planet = planetOf(body);
        const double mu = planet == Body::Earth ? bodyMu(Body::Earth) : bodyMu(planet);
        return moonOrbit(body).semiMajorAxis * std::pow(bodyMu(body) / mu, 0.4);
    }
    return planetOrbit(body, epoch).semiMajorAxis * std::pow(bodyMu(body) / kMuSun, 0.4);
}

Vec3 bodyPosition(Body body, double epoch) {
    if (body == Body::Moon) return earthAndMoonHeliocentric(epoch).moon;
    if (isMoon(body)) {
        return bodyPosition(planetOf(body), epoch) + moonRelative(body, epoch).r;
    }
    if (body != Body::Earth) return tablePosition(body, epoch);
    // The table gives the Earth–Moon barycentre; the Earth is 1/82 of the
    // way from it to the Moon, on the other side.
    return earthAndMoonHeliocentric(epoch).earth;
}

Vec3 bodyVelocity(Body body, double epoch) {
    if (isMoon(body) && body != Body::Moon) {
        return bodyVelocity(planetOf(body), epoch) + moonRelative(body, epoch).v;
    }
    constexpr double dt = 1800.0;
    return (bodyPosition(body, epoch + dt) - bodyPosition(body, epoch - dt)) / (2.0 * dt);
}

Vec3 moonHeliocentric(double epoch) { return earthAndMoonHeliocentric(epoch).moon; }

EarthMoon earthAndMoonHeliocentric(double epoch) {
    const Vec3 barycentre = tablePosition(Body::Earth, epoch);
    const Vec3 moon = moonFromEarth(epoch);
    const double moonShare = kMuMoonKm3 / (kMuKm3.at(index(Body::Earth)) + kMuMoonKm3);
    const Vec3 earth = barycentre - moonShare * moon;
    return {earth, earth + moon};
}

Vec3 eclipticToEquatorial(const Vec3& v) { return rotateX(v, kObliquityJ2000); }

Vec3 equatorialToEcliptic(const Vec3& v) { return rotateX(v, -kObliquityJ2000); }

} // namespace orbit
