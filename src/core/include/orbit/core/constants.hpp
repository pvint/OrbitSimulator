// Physical constants. Everything is in SI units: metres, kilograms, seconds.
#pragma once

namespace orbit {

/// Earth's standard gravitational parameter μ = G·M_earth (m³/s²).
/// Using μ directly is more accurate than multiplying G and M separately.
inline constexpr double kMuEarth = 3.986004418e14;

/// Mean radius of Earth (m). We treat Earth as a perfect sphere.
inline constexpr double kRadiusEarth = 6.371e6;

/// Earth's rotation rate relative to the stars (rad/s): one turn per sidereal
/// day of 23 h 56 min 4 s. At the equator this gives a surface speed of
/// ω·R ≈ 465 m/s, a free boost for eastward launches.
inline constexpr double kOmegaEarth = 7.2921159e-5;

/// Standard gravity (m/s²). Only used to convert specific impulse (seconds)
/// into exhaust velocity: v_e = Isp · g0.
inline constexpr double kG0 = 9.80665;

/// The Kármán line, the conventional "edge of space" (m above the surface).
inline constexpr double kKarmanLine = 100e3;

} // namespace orbit
