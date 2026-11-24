// SPDX-License-Identifier: GPL-3.0-or-later
// Natural bodies: an asteroid, comet or moon with no engine, as the "vehicle".
//
// A natural body only coasts, under the Earth's, the Moon's and the Sun's
// gravity (gravity.hpp). What makes it interesting is where it starts: it is
// placed at a moment, the *epoch*, in one of two ways.
//
// ORBITAL ELEMENTS AT CLOSEST APPROACH
// The orbit around the Earth as the body passes its periapsis at the epoch:
//   q   periapsis distance, from the Earth's centre
//   e   eccentricity: 0 a circle, below 1 an ellipse (a moon), 1 a parabola,
//       above 1 a hyperbola (a flyby: it comes from far away and leaves again)
//   i, Ω, ω   inclination, right ascension of the ascending node, argument
//       of periapsis: how the orbit is tilted and turned (orbit.hpp)
// q and e rather than a and e, because they describe every kind of orbit;
// comets are catalogued this way too, with the time of periapsis.
//
// POSITION AND VELOCITY AT THE EPOCH
// A state vector, as JPL Horizons prints it ("vector table").
//
// FRAME
// Both are given in the Earth's equatorial frame of J2000 (ICRF), the frame
// every catalogue uses: x towards the vernal equinox of 2000, z along the
// Earth's axis in 2000. The simulation works in the equator and equinox of
// *date*, so the body is turned through the precession of the Earth's axis
// since 2000 (precessionFromJ2000): about 0.36° by 2029.
//
// TIME SPAN
// The simulation runs from timeBefore the epoch to timeAfter it: backwards
// from the epoch first, then forwards. The epoch is where the given values
// hold exactly, and for elements that is the closest approach.
#pragma once

#include "orbit/core/orbit.hpp"
#include "orbit/core/vec3.hpp"

#include <array>
#include <string_view>

namespace orbit {

/// What flies: a rocket from a launch site, or a natural body.
enum class Vehicle { Rocket, NaturalBody };
inline constexpr std::array kAllVehicles{Vehicle::Rocket, Vehicle::NaturalBody};
std::string_view vehicleKey(Vehicle vehicle); ///< "rocket", "natural-body"
std::string_view vehicleLabel(Vehicle vehicle);
bool vehicleFromKey(std::string_view key, Vehicle& vehicle);

/// How a natural body is placed.
enum class BodyGiven { Elements, StateVector };
inline constexpr std::array kAllBodyGivens{BodyGiven::Elements, BodyGiven::StateVector};
std::string_view bodyGivenKey(BodyGiven given); ///< "elements", "state-vector"
std::string_view bodyGivenLabel(BodyGiven given);
bool bodyGivenFromKey(std::string_view key, BodyGiven& given);

struct NaturalBody {
    BodyGiven given = BodyGiven::Elements;

    // Orbital elements at periapsis (the epoch), J2000 equatorial frame.
    double periapsisDistance = 3.8011e7; ///< q, m from the Earth's centre
    double eccentricity = 4.2;           ///< e
    double inclination = 0.0;            ///< i, radians
    double raan = 0.0;                   ///< Ω, radians
    double argumentOfPeriapsis = 0.0;    ///< ω, radians

    // Position and velocity at the epoch, J2000 equatorial frame.
    Vec3 position; ///< m
    Vec3 velocity; ///< m/s

    /// When the values above hold: seconds since J2000 (astro_time.hpp).
    double epoch = 0.0;
    double timeBefore = 2.0 * 86400.0; ///< s simulated before the epoch
    double timeAfter = 2.0 * 86400.0;  ///< s simulated after it
};

/// Asteroid 99942 Apophis passing the Earth on 13 April 2029, as the
/// elements at its closest approach (38,011 km from the Earth's centre at
/// 21:45 UTC, from JPL Horizons). The default natural body.
NaturalBody apophisFlyby();

/// The body's position and velocity at the epoch, in the J2000 frame it is
/// given in.
PositionVelocity bodyStateJ2000(const NaturalBody& body, double mu = kMuEarth);

/// The same, turned into the simulation's frame (equator of date).
PositionVelocity bodyStateAtEpoch(const NaturalBody& body, double mu = kMuEarth);

/// Switch how the body is given, keeping the same path. Elements hold at
/// periapsis, so going from a state vector to elements moves the epoch to
/// the periapsis nearest to it (found from the two-body orbit through the
/// state, Kepler's equation); the time span moves with it so it still
/// covers the same stretch of time, and grows to include the new epoch if
/// it has to. Going to a state vector keeps the epoch.
void setBodyGiven(NaturalBody& body, BodyGiven given, double mu = kMuEarth);

/// Precession: the Earth's axis swings round a circle of 23.4° radius once
/// in 26,000 years, dragging the equator and the equinox with it. Turns a
/// vector from the J2000 equatorial frame into the equator and equinox of
/// this epoch, with the IAU 1976 angles ζ, z, θ (J. H. Lieske et al. 1977;
/// J. Meeus, "Astronomical Algorithms", chapter 21):
///   r_date = R_z(z) · R_y(−θ) · R_z(ζ) · r_J2000
/// (rotations of the vector; ζ and z turn about the poles of J2000 and of
/// date, θ tilts between the two equators).
Vec3 precessionFromJ2000(const Vec3& v, double epoch);

/// The reverse: from the equator of date back to J2000.
Vec3 precessionToJ2000(const Vec3& v, double epoch);

} // namespace orbit
