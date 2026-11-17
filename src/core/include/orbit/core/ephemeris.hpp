// SPDX-License-Identifier: GPL-3.0-or-later
// Where the Sun and the Moon are, from low-precision analytic series.
//
// Both use the formulas in the Astronomical Almanac's "low-precision" section,
// as given in D. Vallado, "Fundamentals of Astrodynamics and Applications",
// algorithms 29 (Sun) and 31 (Moon). Accuracy: about 0.01° for the Sun and
// 0.3° (≈ 2000 km) for the Moon over several centuries around 2000. That is
// plenty for intuition and for flying to the Moon in this simulator, but not
// for real mission planning, which uses numerical ephemerides (JPL DE440).
//
// Positions are geocentric, in the equatorial frame of date (z = Earth's
// spin axis), the same frame the simulation uses. Time is an epoch in seconds
// since J2000 (astro_time.hpp).
#pragma once

#include "orbit/core/vec3.hpp"

namespace orbit {

/// Moon: μ = G·M_moon (m³/s²) and mean radius (m).
inline constexpr double kMuMoon = 4.9048695e12;
inline constexpr double kRadiusMoon = 1.7374e6;

/// Sun: μ = G·M_sun (m³/s²), and the astronomical unit (m).
inline constexpr double kMuSun = 1.32712440018e20;
inline constexpr double kAstronomicalUnit = 1.495978707e11;

/// The Moon's sphere of influence around the Earth: inside roughly this
/// distance from the Moon, its gravity matters more than the Earth's for the
/// shape of the path, r_SOI = a·(m_moon/m_earth)^(2/5) ≈ 66,000 km.
inline constexpr double kMoonSphereOfInfluence = 6.61e7;

/// Geocentric position of the Sun (m).
Vec3 sunPosition(double epoch);

/// Geocentric position of the Moon (m).
Vec3 moonPosition(double epoch);

/// Geocentric velocity of the Moon (m/s), by differencing positions a
/// minute apart.
Vec3 moonVelocity(double epoch);

/// The Moon's ecliptic longitude minus the Sun's (radians, 0–2π): 0 at new
/// moon, π at full moon. Used by tests and handy for choosing dates.
double moonElongation(double epoch);

} // namespace orbit
