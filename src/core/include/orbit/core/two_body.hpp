// SPDX-License-Identifier: GPL-3.0-or-later
// Two classic problems of a body falling around one centre, solved exactly.
//
// KEPLER'S PROBLEM: where will it be?
//   Given the position and velocity now, find them a time Δt later. There is
//   no formula in time for an orbit, so an equation has to be solved: here in
//   "universal variables", one form that works for ellipses, parabolas and
//   hyperbolas alike (H. D. Curtis, "Orbital Mechanics for Engineering
//   Students", 3rd ed., sections 3.7–3.8; R. R. Bate, D. D. Mueller,
//   J. E. White, "Fundamentals of Astrodynamics", chapter 4).
//
// LAMBERT'S PROBLEM: how do I get there?
//   Given two positions and the time to fly between them, find the orbit
//   that joins them, and so the velocities needed at both ends. This is the
//   question behind every transfer between planets: "if I leave from here on
//   this day and want to arrive there on that day, how fast must I go, and in
//   which direction?" The difference between that velocity and the one the
//   craft already has is the burn. Johann Lambert showed in 1761 that the
//   time depends only on the two distances, the straight-line distance
//   between the points and the orbit's size; the universal-variable form used
//   here is Curtis's algorithm 5.2, solved by bisection, which is slower
//   than Newton's method but never fails to converge.
//
// Both ignore every pull except the centre's (a planet's, for a transfer
// around the Sun): they give the starting guess, and the full simulation,
// with every planet pulling, shows how far off it is.
#pragma once

#include "orbit/core/vec3.hpp"

#include <optional>

namespace orbit {

/// Stumpff's functions, which turn up wherever universal variables do:
///   C(z) = (1 − cos √z)/z,           S(z) = (√z − sin √z)/√z³      for z > 0
///   C(z) = (cosh √−z − 1)/(−z),       S(z) = (sinh √−z − √−z)/√−z³  for z < 0
///   C(0) = 1/2,                      S(0) = 1/6
/// z = α χ² measures how far round the orbit the craft has gone (positive
/// for an ellipse, negative for a hyperbola). Near zero, both are computed
/// from their power series, where the formulas above lose all precision.
double stumpffC(double z);
double stumpffS(double z);

struct TwoBodyState {
    Vec3 r; ///< m
    Vec3 v; ///< m/s
};

/// Kepler's problem: the state Δt seconds after (r, v), falling around a
/// centre of gravitational parameter μ (Δt may be negative). Solves the
/// universal Kepler equation
///   √μ Δt = (r₀v_r₀/√μ) χ² C(αχ²) + (1 − αr₀) χ³ S(αχ²) + r₀ χ,
/// with α = 1/a = 2/r₀ − v₀²/μ, for the universal anomaly χ by Newton's
/// method, then uses the Lagrange coefficients f, g, ḟ, ġ:
///   r = f r₀ + g v₀,  v = ḟ r₀ + ġ v₀.
TwoBodyState propagateKepler(const TwoBodyState& start, double dt, double mu);

/// The orbit joining r1 to r2 in time Δt (s), the "short" or "long" way
/// round decided by the sense of travel: prograde (anticlockwise seen from
/// +z, the way every planet goes) or retrograde. Less than one full
/// revolution. Returns nothing if there is no such orbit, or when r1 and r2
/// lie exactly opposite each other (the plane of the orbit is then not
/// defined: any plane through the two points will do).
struct LambertSolution {
    Vec3 v1; ///< velocity needed at r1 (m/s)
    Vec3 v2; ///< velocity on arrival at r2 (m/s)
};
std::optional<LambertSolution> solveLambert(const Vec3& r1, const Vec3& r2, double dt, double mu,
                                            bool prograde = true);

} // namespace orbit
