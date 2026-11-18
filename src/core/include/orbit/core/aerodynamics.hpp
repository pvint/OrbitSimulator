// SPDX-License-Identifier: GPL-3.0-or-later
// Air resistance (drag) on the rocket.
//
// DRAG
//     D = ½ ρ v² · Cd · A        pointing against the velocity through the air
// ρ is the air density (atmosphere.hpp), v the speed *relative to the air*,
// A the frontal area π·d²/4 and Cd the drag coefficient, a number that says
// how streamlined the shape is (about 0.2–0.4 for a slender rocket).
//
// The air turns with the Earth, so the velocity through it is the velocity
// relative to the ground: v_air = v − ω × r. On the pad, the rocket and the
// air move together and there is no drag at all.
//
// DYNAMIC PRESSURE
//     q = ½ ρ v²
// the pressure the oncoming air exerts on the nose. Drag is q · Cd · A. As a
// rocket climbs, v grows but ρ falls, so q rises to a peak and falls again.
// That peak, "max-Q", typically comes about a minute after liftoff, around
// 10–15 km up, and is the hardest moment for the structure: real rockets
// throttle down through it.
//
// MACH NUMBER AND THE SOUND BARRIER
// The Mach number is the speed through the air divided by the speed of sound
// there. Approaching Mach 1, shock waves form on the rocket and Cd roughly
// doubles ("transonic drag rise"), then eases off at higher Mach numbers.
// We scale the subsonic Cd by a typical curve for slender rockets:
//     Mach   0–0.8   1.0   1.2   1.5   2.0   3.0   5+
//     Cd ×   1.0     1.6   1.8   1.65  1.4   1.15  1.0
// with straight lines in between. It is a representative shape, not the
// curve of any particular rocket.
#pragma once

#include "orbit/core/rocket.hpp"
#include "orbit/core/vec3.hpp"

namespace orbit {

/// How the drag coefficient changes with the Mach number: the subsonic Cd
/// is multiplied by this (1 below Mach 0.8, up to 1.8 just above Mach 1).
double machDragFactor(double mach);

/// Frontal area A = π·d²/4 (m²).
double frontalArea(const Rocket& rocket);

/// The air's effect on the rocket at one moment.
struct Aerodynamics {
    Vec3 airVelocity;             ///< m/s, velocity relative to the air (ECI axes)
    double density = 0.0;         ///< kg/m³
    double mach = 0.0;            ///< speed through the air ÷ speed of sound
    double dynamicPressure = 0.0; ///< q = ½ρv², Pa
    double dragCoefficient = 0.0; ///< Cd at this Mach number
    Vec3 drag;                    ///< N, the drag force (against airVelocity)
};

/// Drag and friends for a rocket at ECI position r with inertial velocity v.
Aerodynamics aerodynamicsAt(const Rocket& rocket, const Vec3& r, const Vec3& v);

/// Does this rocket feel drag at all? (Cd > 0 and a diameter.)
bool hasDrag(const Rocket& rocket);

} // namespace orbit
