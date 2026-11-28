// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/free_roam.hpp"

#include "orbit/core/vec3.hpp"

#include <cmath>

namespace orbit {

namespace {

bool isGiant(Body body) {
    return body == Body::Jupiter || body == Body::Saturn || body == Body::Uranus ||
           body == Body::Neptune;
}

} // namespace

double freeRoamHorizon(Body body) {
    constexpr double kDay = 86400.0;
    return isGiant(planetOf(body)) ? 30.0 * kDay : 365.25 * kDay;
}

bool canRoamFrom(Body body) { return !isMoon(body) || canPark(body); }

ParkingOrbit freeRoamOrbit(Body body) {
    if (isGiant(body)) {
        const double r = 2.0 * bodyRadius(body);
        return {r, r};
    }
    return parkingOrbit(body);
}

Puzzle freeRoam(Body body, double epoch) {
    if (!canRoamFrom(body)) body = Body::Earth;
    Puzzle puzzle;
    puzzle.region = regionOf(body);
    puzzle.inOrbit = true;
    if (isMoon(body)) puzzle.startMoon = body;
    puzzle.destination = body;

    // The body's own motion: round the Sun for a planet, round its planet
    // for a moon. Its angular momentum h = r × v is square to that orbit's
    // plane, pointing the way it goes round.
    const Vec3 r = bodyPosition(body, epoch);
    const Vec3 v = bodyVelocity(body, epoch);
    const Vec3 centreR = isMoon(body) ? bodyPosition(planetOf(body), epoch) : Vec3{};
    const Vec3 centreV = isMoon(body) ? bodyVelocity(planetOf(body), epoch) : Vec3{};
    const Vec3 pole = normalized(cross(r - centreR, v - centreV));
    // The low point faces what the body goes round; the ship moves along
    // ĥ × p̂ there, at the circular speed √(μ/r) (or, for an ellipse, the
    // vis-viva speed at its low point, √(2μ/r_p − 2μ/(r_p + r_a))).
    const Vec3 down = normalized(centreR - r);
    const Vec3 along = cross(pole, down);
    const ParkingOrbit orbit = freeRoamOrbit(body);
    const double mu = bodyMu(body);
    const double speed =
        std::sqrt(2.0 * mu / orbit.periapsis - 2.0 * mu / (orbit.periapsis + orbit.apoapsis));

    Voyage& voyage = puzzle.voyage;
    voyage.startEpoch = epoch;
    voyage.position = r + orbit.periapsis * down;
    voyage.velocity = v + speed * along;
    voyage.goal = Goal::Explore;
    voyage.destination = body;
    voyage.ship.unlimitedFuel = true;
    voyage.horizon = freeRoamHorizon(body);
    return puzzle;
}

bool isFreeRoam(const Puzzle& puzzle) { return puzzle.voyage.goal == Goal::Explore; }

} // namespace orbit
