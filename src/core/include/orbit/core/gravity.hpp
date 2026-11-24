// SPDX-License-Identifier: GPL-3.0-or-later
// Gravity in the Earth-centred frame: the Earth, plus the Moon and the Sun.
//
//   Earth    a = −μ r / |r|³              (a point mass)
//   Moon,    a = μ_b ((r_b − r)/|r_b − r|³ − r_b/|r_b|³)   for a body b at r_b
//   Sun
//
// The second term of the Moon's and Sun's pull is there because the frame's
// origin, the Earth, is itself pulled towards the body: only the *difference*
// in pull (the tide) moves something relative to the Earth. Rockets and
// natural bodies both fall under this.
#pragma once

#include "orbit/core/constants.hpp"
#include "orbit/core/vec3.hpp"

namespace orbit {

/// Gravitational acceleration (m/s²) at r (m, Earth-centred) at this epoch
/// (s since J2000, for where the Moon and Sun are). Without third bodies,
/// only the Earth pulls.
Vec3 gravityAcceleration(const Vec3& r, double epoch, bool thirdBodies, double mu = kMuEarth);

/// The same around the Sun (solar_system.hpp), for r in the heliocentric
/// ecliptic J2000 frame: the Sun, a = −μ_sun r/|r|³, plus the eight planets
/// and the Moon, each pulling by the same tidal formula (the Sun is pulled
/// by them too, and only the difference moves the body relative to it).
/// Without third bodies, only the Sun pulls.
Vec3 heliocentricGravity(const Vec3& r, double epoch, bool thirdBodies);

} // namespace orbit
