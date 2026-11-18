// SPDX-License-Identifier: GPL-3.0-or-later
// Ascent guidance: which way the engine points.
//
// PITCH PROGRAM
//   1. VERTICAL      t < pitchStartTime
//                    Climb straight up, away from Earth's centre.
//   2. KICK          pitchStartTime ≤ t < pitchStartTime + kickHoldTime
//                    Tilt kickAngle away from vertical, downrange, and hold
//                    that angle.
//   3. GRAVITY_TURN  after that
//                    Point along the velocity relative to the ground. Gravity
//                    bends the path towards horizontal and the thrust follows
//                    it round. This is how real rockets climb through the
//                    atmosphere without turning sideways into the airflow.
// Velocity relative to the *ground* matters: just after liftoff the
// inertial velocity is mostly Earth's rotation (up to 465 m/s eastward), and
// following that would tip the rocket over at once.
//
// "Downrange" is the horizontal direction within the LAUNCH PLANE: the plane
// through Earth's centre that contains the launch site and the launch
// direction (azimuth), fixed in inertial space at liftoff. Using a fixed plane
// rather than the local compass keeps the direction well defined everywhere,
// even for a launch from a pole, where "north" flips as soon as you move.
//
// A bigger or earlier kick gives a flatter trajectory. Too much and the rocket
// turns over before it is high enough and hits the ground. Too little and it
// climbs too steeply and wastes propellant fighting gravity.
//
// CIRCULARIZE
// Used for the auto-circularize burn. The engine points along
//     Δv = v_circular − v
// where v_circular is circular-orbit speed √(μ/r) along the horizontal part of
// the current velocity (so the orbit keeps its plane). That is the velocity
// change that would turn the current motion into a circular orbit at this
// altitude. At apoapsis it means "burn horizontally". If the rocket starts to
// sink during the burn, Δv tilts upward to correct it. The burn ends when Δv
// reaches zero.
//
// GRAVITY COMPENSATION
// That correction comes late: the rocket has to sink before Δv tilts up. A
// weak engine (an upper stage pushing about its own weight) takes minutes to
// circularize and sinks tens of kilometres meanwhile. So the engine aims at
//     Δv + (g − v_h²/r) · t_go · up
// where t_go = |Δv| / (F/m) is roughly how long the burn still needs, and
// g − v_h²/r is the part of gravity the rocket's sideways speed doesn't yet
// hold up (none, at circular speed). That is the speed gravity will take
// away before the burn ends, so the rocket supplies it in advance, tilting
// its nose up the way real upper stages do. For a short, hard burn t_go is
// tiny and this changes almost nothing.
#pragma once

#include "orbit/core/constants.hpp"
#include "orbit/core/vec3.hpp"

namespace orbit {

struct AscentGuidance {
    double azimuth = 0.0;        ///< launch direction, radians clockwise from north (π/2 = east)
    double pitchStartTime = 0.0; ///< s after liftoff
    double kickAngle = 0.0;      ///< radians from vertical
    double kickHoldTime = 0.0;   ///< s
};

enum class GuidancePhase { Vertical, Kick, GravityTurn, Circularize };

/// Phase of the pitch program at time t (never returns Circularize).
GuidancePhase pitchProgramPhase(double t, const AscentGuidance& guidance);

/// Normal of the launch plane, for a rocket at `launchPosition` (ECI) heading
/// off at `azimuth` (radians clockwise from north). At a pole, azimuth is
/// measured from the meridian of longitude 0, as for localFrame().
Vec3 launchPlaneNormal(const Vec3& launchPosition, double azimuth);

/// Horizontal direction at r that lies in the launch plane, pointing the way
/// the rocket is heading.
Vec3 downrangeDirection(const Vec3& r, const Vec3& launchPlaneNormal);

/// Velocity change still needed to be in a circular orbit at the current
/// radius, in the current orbital plane. `fallbackDirection` is used as
/// "horizontal" if the rocket is moving straight up or down.
Vec3 circularizationDeltaV(const Vec3& r, const Vec3& v, const Vec3& fallbackDirection,
                           double mu = kMuEarth);

/// Unit vector the engine points along.
/// @param r, v              position and inertial velocity (ECI)
/// @param relativeVel       velocity relative to the rotating ground
/// @param kickAngle         radians from vertical, used in the KICK phase
/// @param launchPlaneNormal from launchPlaneNormal()
/// @param thrustAcceleration F/m (m/s²), for gravity compensation while
///                          circularizing; 0 leaves it out
Vec3 thrustDirection(GuidancePhase phase, const Vec3& r, const Vec3& v, const Vec3& relativeVel,
                     double kickAngle, const Vec3& launchPlaneNormal, double mu = kMuEarth,
                     double thrustAcceleration = 0.0);

} // namespace orbit
