// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/aerodynamics.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/atmosphere.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/earth.hpp"

#include <array>

namespace orbit {

namespace {

struct MachPoint {
    double mach;
    double factor;
};

// The transonic drag rise (see the table in aerodynamics.hpp).
constexpr std::array kMachCurve{
    MachPoint{0.8, 1.0}, MachPoint{1.0, 1.6},  MachPoint{1.2, 1.8}, MachPoint{1.5, 1.65},
    MachPoint{2.0, 1.4}, MachPoint{3.0, 1.15}, MachPoint{5.0, 1.0},
};

} // namespace

double machDragFactor(double mach) {
    if (mach <= kMachCurve.front().mach) return kMachCurve.front().factor;
    for (std::size_t i = 1; i < kMachCurve.size(); ++i) {
        const MachPoint& a = kMachCurve[i - 1];
        const MachPoint& b = kMachCurve[i];
        if (mach <= b.mach) {
            // A straight line between the two neighbouring points.
            return a.factor + (b.factor - a.factor) * (mach - a.mach) / (b.mach - a.mach);
        }
    }
    return kMachCurve.back().factor;
}

double frontalArea(const Rocket& rocket) { return kPi * rocket.diameter * rocket.diameter / 4.0; }

bool hasDrag(const Rocket& rocket) { return rocket.dragCoefficient > 0.0 && rocket.diameter > 0.0; }

Aerodynamics aerodynamicsAt(const Rocket& rocket, const Vec3& r, const Vec3& v) {
    Aerodynamics aero;
    const double altitude = norm(r) - kRadiusEarth;
    if (altitude > kAtmosphereTop) return aero; // in space: nothing to push against

    // The air moves with the ground.
    aero.airVelocity = v - groundVelocity(r);
    const double speed = norm(aero.airVelocity);
    const Air air = atmosphereAt(altitude);
    aero.density = air.density;
    aero.mach = speed / air.speedOfSound;
    aero.dynamicPressure = 0.5 * air.density * speed * speed; // q = ½ρv²
    aero.dragCoefficient = rocket.dragCoefficient * machDragFactor(aero.mach);
    // D = q·Cd·A against the motion through the air. Written as
    // −(½ρ·Cd·A·|v|)·v, which is zero (not 0/0) when the rocket is at rest.
    aero.drag = (-0.5 * air.density * aero.dragCoefficient * frontalArea(rocket) * speed) *
                aero.airVelocity;
    return aero;
}

} // namespace orbit
