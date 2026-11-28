// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/gravity.hpp"

#include "orbit/core/ephemeris.hpp"
#include "orbit/core/solar_system.hpp"

#include <array>
#include <cstddef>

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
    // Each planet's sphere of influence, from its orbit at J2000 (they
    // barely change), to tell when the ship is among a giant's moons.
    static const std::array<double, kAllPlanets.size()> influence = [] {
        std::array<double, kAllPlanets.size()> radii{};
        for (const Body planet : kAllPlanets) {
            radii.at(static_cast<std::size_t>(planet)) = sphereOfInfluence(planet, 0.0);
        }
        return radii;
    }();
    for (const Body planet : kAllPlanets) {
        if (planet == Body::Earth) continue; // with the Moon, below
        const Vec3 p = bodyPosition(planet, epoch);
        const auto moons = moonsOf(planet);
        if (moons.empty() || norm(r - p) > influence.at(static_cast<std::size_t>(planet))) {
            // From far away a planet and its moons pull as one mass.
            acceleration += tidalPull(r, p, bodyMu(planet));
            continue;
        }
        // Among its moons: the planet alone, and each moon from where it
        // is. (The tables place the planet system's centre of mass; the
        // planet itself is at most about 100 km from it, left out.)
        acceleration += tidalPull(r, p, planetMuAlone(planet));
        for (const Body moon : moons) {
            acceleration += tidalPull(r, p + moonFromPlanet(moon, epoch).r, bodyMu(moon));
        }
    }
    const EarthMoon em = earthAndMoonHeliocentric(epoch);
    acceleration += tidalPull(r, em.earth, bodyMu(Body::Earth));
    acceleration += tidalPull(r, em.moon, kMuMoon);
    return acceleration;
}

} // namespace orbit
