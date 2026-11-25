// SPDX-License-Identifier: GPL-3.0-or-later
// The planets: where they are, how strongly they pull, and the frame they
// are described in.
//
// FRAME
// Heliocentric ecliptic J2000: the Sun at the origin, x towards the vernal
// equinox of 2000, z towards the north pole of the ecliptic (the plane of
// the Earth's orbit), the frame planetary and small-body catalogues use. The
// Earth's equator is tilted to it by the obliquity ε = 23.43928°, so the
// Earth-centred frame of J2000 is this one turned by ε about the x axis.
//
// EPHEMERIS
// JPL's "Approximate Positions of the Planets" (E. M. Standish and
// J. G. Williams, https://ssd.jpl.nasa.gov/planets/approx_pos.html): each
// planet's six Keplerian elements, with rates per century, for the
// Earth–Moon barycentre and the other seven planets. Between 1800 and 2050
// (table 1) they are within about 20 arcseconds for the inner planets
// (6000 km for the Earth–Moon barycentre) and 400–600" for Jupiter and
// Saturn. From 3000 BC to 3000 AD (tables 2a and 2b, used outside 1800–2050)
// errors are a few times larger. No data files: just these numbers.
//
// The Earth and the Moon are placed about their barycentre with the Moon's
// low-precision position (ephemeris.hpp): the Earth sits μ_moon/(μ_earth +
// μ_moon) = 1/82 of the way to the Moon, about 4,700 km from the
// barycentre.
//
// Time: the tables want TDB; UTC is used, as elsewhere in the simulator. The
// difference (69 s now, minutes a few centuries ago) moves the Earth by a
// few thousand km.
#pragma once

#include "orbit/core/vec3.hpp"

#include <array>
#include <numbers>
#include <string_view>

namespace orbit {

/// The Sun's radius (m). (Its μ = G·M_sun is kMuSun, in ephemeris.hpp.)
inline constexpr double kRadiusSun = 6.957e8;

/// The obliquity of the ecliptic at J2000: the Earth's axis is tilted this
/// much against the pole of its orbit (radians).
inline constexpr double kObliquityJ2000 = 23.43928 * std::numbers::pi / 180.0;

enum class Planet { Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune };
inline constexpr std::array kAllPlanets{Planet::Mercury, Planet::Venus,   Planet::Earth,
                                        Planet::Mars,    Planet::Jupiter, Planet::Saturn,
                                        Planet::Uranus,  Planet::Neptune};

std::string_view planetName(Planet planet);

/// μ = G·M (m³/s²) of the planet with its moons (the Earth without its
/// Moon), from JPL's DE440.
double planetMu(Planet planet);

/// Mean radius (m).
double planetRadius(Planet planet);

/// Heliocentric ecliptic J2000 position of the planet's centre (m) at an
/// epoch (s since J2000). For the Earth, the Earth itself, not the
/// Earth–Moon barycentre.
Vec3 planetPosition(Planet planet, double epoch);

/// Its velocity (m/s), by differencing positions an hour apart.
Vec3 planetVelocity(Planet planet, double epoch);

/// Heliocentric ecliptic J2000 position of the Moon (m).
Vec3 moonHeliocentric(double epoch);

/// Both at once (one evaluation of the Moon's series instead of three).
struct EarthMoon {
    Vec3 earth;
    Vec3 moon;
};
EarthMoon earthAndMoonHeliocentric(double epoch);

/// The planet's orbital elements at the epoch, from the tables (the
/// Earth–Moon barycentre's for the Earth): for drawing its orbit.
struct PlanetOrbit {
    double semiMajorAxis = 0.0; ///< m
    double eccentricity = 0.0;
    double inclination = 0.0;         ///< radians, to the ecliptic
    double raan = 0.0;                ///< Ω, radians
    double argumentOfPeriapsis = 0.0; ///< ω, radians
    double meanAnomaly = 0.0;         ///< M, radians
};
PlanetOrbit planetOrbit(Planet planet, double epoch);

/// The planet's sphere of influence (m), a (μ_planet / μ_sun)^(2/5) with a
/// its orbit's semi-major axis: within it, the planet's pull shapes a
/// passing path more than the Sun's does (Laplace's estimate). The Earth's
/// reaches about 925,000 km.
double sphereOfInfluence(Planet planet, double epoch);

/// Turning between the J2000 ecliptic and the J2000 equator: a rotation by
/// the obliquity about the x axis, the line where the two planes cross
/// (towards the vernal equinox).
Vec3 eclipticToEquatorial(const Vec3& v);
Vec3 equatorialToEcliptic(const Vec3& v);

} // namespace orbit
