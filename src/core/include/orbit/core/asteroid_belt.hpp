// SPDX-License-Identifier: GPL-3.0-or-later
// The main asteroid belt, for drawing: a few thousand made-up asteroids on
// orbits shaped like the real ones. (The real belt holds over a million
// bodies larger than 1 km, but so spread out that a spacecraft crossing it
// almost never comes near one: drawn to scale it would be invisible. It's
// drawn because it "feels right" to see where it is.)
//
// SHAPE OF THE BELT
// Semi-major axes from 2.1 to 3.3 au, most between 2.2 and 3.2; orbits
// mildly eccentric (typically e ≈ 0.1) and tilted (typically 5–10°).
// Jupiter's gravity has cleared the "Kirkwood gaps" where an asteroid
// would go round exactly 3, 5/2, 7/3 or 2 times for each of Jupiter's
// orbits: the repeated tug at the same place pumps up the eccentricity
// until the orbit crosses Mars's or the Earth's and the asteroid is lost.
// By Kepler's third law, a period T_J / k means a = a_J k^(−2/3): 2.50 au
// (3:1), 2.82 au (5:2), 2.95 au (7:3) and 3.28 au (2:1).
#pragma once

#include "orbit/core/vec3.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orbit {

/// One asteroid's orbit around the Sun (heliocentric ecliptic J2000).
struct Asteroid {
    double semiMajorAxis = 0.0; ///< m
    double eccentricity = 0.0;
    double inclination = 0.0;         ///< radians
    double raan = 0.0;                ///< Ω, radians
    double argumentOfPeriapsis = 0.0; ///< ω, radians
    double meanAnomaly = 0.0;         ///< M at J2000 (epoch 0), radians
};

/// The Kirkwood gaps' semi-major axes (m), from Jupiter's by Kepler's third law.
std::vector<double> kirkwoodGaps();

/// `count` asteroids, always the same for the same seed.
std::vector<Asteroid> makeAsteroidBelt(std::size_t count, std::uint64_t seed = 1);

/// Where an asteroid is at `epoch` (s since J2000; m, heliocentric): the
/// mean anomaly grows at n = √(μ/a³), and Kepler's equation gives the
/// position on the ellipse.
Vec3 asteroidPosition(const Asteroid& asteroid, double epoch);

} // namespace orbit
