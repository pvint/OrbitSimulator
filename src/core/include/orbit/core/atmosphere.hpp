// SPDX-License-Identifier: GPL-3.0-or-later
// The Earth's atmosphere: how dense, how warm and how much pressure at a
// given height.
//
// UP TO 86 km: the 1976 US Standard Atmosphere
// The air is split into seven layers. In each, the temperature changes at a
// constant rate with height (the "lapse rate" L), or stays constant. Air
// pressure then follows from the hydrostatic equation dP/dh = −ρ·g (each
// layer of air carries the weight of the air above it) and the ideal gas law
// ρ = P·M / (R·T):
//     L ≠ 0:   P = P_b · (T_b / T)^(g0·M / (R·L)),   T = T_b + L·(H − H_b)
//     L = 0:   P = P_b · exp(−g0·M·(H − H_b) / (R·T_b))
// H is *geopotential* height, which accounts for gravity weakening with
// height: H = r0·z / (r0 + z) for geometric height z.
//
// ABOVE 86 km: an exponential table
// The upper atmosphere is heated by the Sun and changes with solar activity,
// so a single "standard" profile means less. We use the densities of the
// classic exponential model (Vallado, *Fundamentals of Astrodynamics and
// Applications*, table 8-4, built from the 1976 standard and CIRA-72) at
// 90, 100, 110, ... 1000 km. Between two of those heights the density falls
// exponentially, as it would in air at a constant temperature:
//     ρ = ρ_1 · exp(−(z − z_1) / H_s),   H_s = (z_2 − z_1) / ln(ρ_1 / ρ_2)
// H_s is the "scale height", the climb over which the density falls by a
// factor e: about 6 km low down, over 50 km at 300 km. The table starts at
// the standard atmosphere's own density at 86 km, so density is continuous
// all the way up. Above 1000 km we treat space as empty: the density there
// is about 10⁻¹⁵ of sea level.
//
// The atmosphere turns with the Earth, so drag depends on the velocity
// relative to the ground (aerodynamics.hpp), not the inertial velocity.
#pragma once

namespace orbit {

/// Above this altitude (m) there is no atmosphere.
inline constexpr double kAtmosphereTop = 1000e3;

/// The state of the air at one place.
struct Air {
    double temperature = 0.0;  ///< K
    double pressure = 0.0;     ///< Pa
    double density = 0.0;      ///< kg/m³
    double speedOfSound = 0.0; ///< m/s: a = √(γ·R·T / M), with γ = 1.4 for air
};

/// The air at a geometric altitude (m above the surface). Below the surface
/// it continues the lowest layer; above kAtmosphereTop it is a vacuum (zero
/// density and pressure). Above 86 km the temperature is held at its 86 km
/// value and the pressure follows from it by the ideal gas law. Both are
/// rough up there (the real thermosphere is far hotter), but nothing depends
/// on them: the speed of sound only matters near Mach 1, far lower down.
Air atmosphereAt(double altitude);

} // namespace orbit
