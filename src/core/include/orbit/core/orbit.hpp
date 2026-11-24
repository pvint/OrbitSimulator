// SPDX-License-Identifier: GPL-3.0-or-later
// Turning a position and velocity into the shape and orientation of an orbit.
//
// SHAPE. Two quantities stay constant along a coasting orbit:
//   specific energy            ε = v²/2 − μ/r
//   specific angular momentum  h = r × v      (a vector, perpendicular to the orbit)
// They fix the orbit's shape:
//   semi-major axis   a = −μ / (2ε)           (negative for escape trajectories)
//   eccentricity      e = |e_vec|, where e_vec = ((v² − μ/r)·r − (r·v)·v) / μ
//                     points at periapsis (0 = circle, <1 ellipse, ≥1 escape)
//   semi-latus rect.  p = h²/μ
//   periapsis         r_p = p / (1 + e)
//   apoapsis          r_a = 2a − r_p          (bound orbits only)
//   period            T = 2π √(a³/μ)
//
// ORIENTATION, relative to the equator and the inertial x axis:
//   inclination   i   tilt of the orbit against the equator (0 = equatorial
//                     eastward, 90° = polar, >90° = retrograde)
//   RAAN          Ω   where the orbit crosses the equator going north
//                     ("right ascension of the ascending node")
//   arg. of periapsis ω   angle from that crossing to periapsis
//   true anomaly  ν   where the craft is now, measured from periapsis
// Ω is undefined for an equatorial orbit and ω for a circular one; those are
// reported as 0, with the angles measured from the x axis or the node instead.
#pragma once

#include "orbit/core/constants.hpp"
#include "orbit/core/vec3.hpp"

namespace orbit {

struct OrbitalElements {
    double energy = 0.0;              ///< ε, J/kg
    double angularMomentum = 0.0;     ///< |h|, m²/s
    double semiMajorAxis = 0.0;       ///< a, m (infinite for a parabola)
    double eccentricity = 0.0;        ///< e
    double inclination = 0.0;         ///< i, radians
    double raan = 0.0;                ///< Ω, radians
    double argumentOfPeriapsis = 0.0; ///< ω, radians
    double trueAnomaly = 0.0;         ///< ν, radians
    double periapsis = 0.0;           ///< r_p, distance from the body's centre, m
    double apoapsis = 0.0;            ///< r_a, m (infinite if not bound)
    double periapsisAltitude = 0.0;   ///< r_p minus the body's radius, m
    double apoapsisAltitude = 0.0;    ///< m
    double period = 0.0;              ///< s (infinite if not bound)
    bool bound = false;               ///< ε < 0: the craft will come back round
};

/// Orbital elements of the trajectory through r with velocity v.
/// Also works for straight up-and-down paths (h = 0): bound if ε < 0.
OrbitalElements elementsFromState(const Vec3& r, const Vec3& v, double mu = kMuEarth,
                                  double bodyRadius = kRadiusEarth);

/// The reverse, for elliptical orbits (e < 1): position and velocity from
/// a, e, i, Ω, ω, ν. Handy for starting a simulation directly in orbit.
struct PositionVelocity {
    Vec3 r;
    Vec3 v;
};
PositionVelocity stateFromElements(double semiMajorAxis, double eccentricity, double inclination,
                                   double raan, double argumentOfPeriapsis, double trueAnomaly,
                                   double mu = kMuEarth);

/// Position and velocity from elements that suit *any* conic section:
/// periapsis distance q, eccentricity e, and the orientation angles i, Ω, ω,
/// at true anomaly ν. The semi-major axis can't describe every orbit (it is
/// infinite for a parabola, negative for a hyperbola), but q always can:
/// the semi-latus rectum p = q(1 + e) holds for all of them, and the conic
/// equation r = p / (1 + e cos ν) does the rest. This is how comets and
/// asteroid flybys are usually described. For a hyperbola, ν must stay
/// within the asymptotes: |ν| < arccos(−1/e).
PositionVelocity stateFromPeriapsis(double periapsisDistance, double eccentricity,
                                    double inclination, double raan, double argumentOfPeriapsis,
                                    double trueAnomaly, double mu = kMuEarth);

/// Time since the last periapsis passage (s; negative: before the next
/// one) of a body at true anomaly ν on a conic with periapsis distance q.
/// For an ellipse the result lies within half a period of the periapsis.
/// Uses Kepler's equation in the form that suits the conic:
///   ellipse    M = E − e sin E,       tan(E/2) = √((1−e)/(1+e)) tan(ν/2)
///   hyperbola  M = e sinh H − H,      tanh(H/2) = √((e−1)/(e+1)) tan(ν/2)
///   parabola   Barker's equation:     t = √(2q³/μ) (D + D³/3),  D = tan(ν/2)
/// where the mean anomaly M grows evenly with time: t = M / n, with the mean
/// motion n = √(μ/|a|³).
double timeSincePeriapsis(double periapsisDistance, double eccentricity, double trueAnomaly,
                          double mu = kMuEarth);

/// The reverse for an ellipse: the eccentric anomaly E from the mean anomaly
/// M (radians), solving Kepler's equation M = E − e sin E by Newton's method,
/// E ← E + (M − E + e sin E) / (1 − e cos E), starting from E₀ = M + e sin M.
/// (There is no formula for E: this is the classic equation that has to be
/// solved by iteration.) For e < 1.
double eccentricAnomaly(double meanAnomaly, double eccentricity);

/// Kepler's third law: T = 2π √(a³/μ).
double orbitalPeriod(double semiMajorAxis, double mu = kMuEarth);

/// Speed for a circular orbit of radius r: √(μ/r).
double circularSpeed(double r, double mu = kMuEarth);

/// Speed at which energy reaches zero, so the craft never comes back: √(2μ/r).
double escapeSpeed(double r, double mu = kMuEarth);

} // namespace orbit
