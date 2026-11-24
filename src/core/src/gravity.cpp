// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/gravity.hpp"

#include "orbit/core/ephemeris.hpp"

namespace orbit {

Vec3 gravityAcceleration(const Vec3& r, double epoch, bool thirdBodies, double mu) {
    const double rNorm = norm(r);
    Vec3 acceleration = (-mu / (rNorm * rNorm * rNorm)) * r;
    if (!thirdBodies) return acceleration;

    // A body at rb pulls the craft towards it, and the Earth too; the
    // difference is what moves the craft relative to the Earth.
    const auto pull = [&](const Vec3& rb, double muBody) {
        const Vec3 toBody = rb - r;
        const double d = norm(toBody);
        const double rbNorm = norm(rb);
        return muBody * (toBody / (d * d * d) - rb / (rbNorm * rbNorm * rbNorm));
    };
    acceleration += pull(moonPosition(epoch), kMuMoon);
    acceleration += pull(sunPosition(epoch), kMuSun);
    return acceleration;
}

} // namespace orbit
