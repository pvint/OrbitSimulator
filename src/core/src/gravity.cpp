// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/gravity.hpp"

#include "orbit/core/ephemeris.hpp"
#include "orbit/core/solar_system.hpp"

namespace orbit {

namespace {

/// A body at rb (relative to the frame's centre) pulls the craft at r, and
/// the centre too; the difference is what moves the craft relative to it.
Vec3 tidalPull(const Vec3& r, const Vec3& rb, double muBody) {
    const Vec3 toBody = rb - r;
    const double d = norm(toBody);
    const double rbNorm = norm(rb);
    return muBody * (toBody / (d * d * d) - rb / (rbNorm * rbNorm * rbNorm));
}

} // namespace

Vec3 gravityAcceleration(const Vec3& r, double epoch, bool thirdBodies, double mu) {
    const double rNorm = norm(r);
    Vec3 acceleration = (-mu / (rNorm * rNorm * rNorm)) * r;
    if (!thirdBodies) return acceleration;

    acceleration += tidalPull(r, moonPosition(epoch), kMuMoon);
    acceleration += tidalPull(r, sunPosition(epoch), kMuSun);
    return acceleration;
}

Vec3 heliocentricGravity(const Vec3& r, double epoch, bool thirdBodies) {
    const double rNorm = norm(r);
    Vec3 acceleration = (-kMuSun / (rNorm * rNorm * rNorm)) * r;
    if (!thirdBodies) return acceleration;
    for (const Body planet : kAllPlanets) {
        if (planet == Body::Earth) continue; // with the Moon, below
        acceleration += tidalPull(r, bodyPosition(planet, epoch), bodyMu(planet));
    }
    const EarthMoon em = earthAndMoonHeliocentric(epoch);
    acceleration += tidalPull(r, em.earth, bodyMu(Body::Earth));
    acceleration += tidalPull(r, em.moon, kMuMoon);
    return acceleration;
}

} // namespace orbit
