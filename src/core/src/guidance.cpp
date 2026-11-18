// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/guidance.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/earth.hpp"
#include "orbit/core/orbit.hpp"

#include <algorithm>
#include <cmath>

namespace orbit {

GuidancePhase pitchProgramPhase(double t, const AscentGuidance& guidance) {
    if (t < guidance.pitchStartTime) return GuidancePhase::Vertical;
    if (t < guidance.pitchStartTime + guidance.kickHoldTime) return GuidancePhase::Kick;
    return GuidancePhase::GravityTurn;
}

Vec3 launchPlaneNormal(const Vec3& launchPosition, double azimuth) {
    const LocalFrame frame = localFrame(launchPosition);
    return normalized(cross(frame.up, horizontalDirection(frame, azimuth)));
}

Vec3 downrangeDirection(const Vec3& r, const Vec3& launchPlaneNormal) {
    // n × up is horizontal and in the plane; at the launch site it equals the
    // launch direction, since (up × d) × up = d for perpendicular unit vectors.
    return normalized(cross(launchPlaneNormal, normalized(r)));
}

Vec3 circularizationDeltaV(const Vec3& r, const Vec3& v, const Vec3& fallbackDirection, double mu) {
    const Vec3 up = normalized(r);
    Vec3 horizontal = v - dot(v, up) * up;
    if (norm(horizontal) < 1e-6) horizontal = fallbackDirection;
    const Vec3 vCircular = std::sqrt(mu / norm(r)) * normalized(horizontal);
    return vCircular - v;
}

Vec3 orbitInsertionDirection(const Vec3& r, const Vec3& v, double targetRadius,
                             double thrustAcceleration, double timeToGo,
                             const Vec3& fallbackDirection, double mu) {
    const Vec3 up = normalized(r);
    Vec3 horizontal = v - dot(v, up) * up;
    horizontal = norm(horizontal) > 1e-6 ? normalized(horizontal) : fallbackDirection;
    const auto direction = [&](double pitch) {
        return std::sin(pitch) * up + std::cos(pitch) * horizontal;
    };

    // Where the apoapsis should be one second from now: closing on the
    // target so as to get there about when the burn reaches orbital speed
    // (but not slower than 2 minutes, nor faster than 15 s).
    constexpr double kLookAhead = 1.0; // s
    const double pace = std::clamp(timeToGo, 15.0, 120.0);
    const double apoapsis = elementsFromState(r, v, mu).apoapsis;
    const double wanted = std::isfinite(apoapsis)
                              ? apoapsis + (targetRadius - apoapsis) * kLookAhead / pace
                              : targetRadius;
    // The apoapsis a second of thrust at this pitch would give. Gravity
    // alone doesn't change the apoapsis (it's a property of the whole path),
    // so only the push from the engine counts: treat it as a small kick here.
    const auto apoapsisAfter = [&](double pitch) {
        return elementsFromState(r, v + thrustAcceleration * kLookAhead * direction(pitch), mu)
            .apoapsis;
    };

    // Bisect on the pitch: the apoapsis rises as the nose comes up.
    double low = -kPi / 6.0;       // 30° below level
    double high = 4.0 * kPi / 9.0; // 80° above
    if (apoapsisAfter(low) >= wanted) return direction(low);
    if (apoapsisAfter(high) <= wanted) return direction(high);
    for (int i = 0; i < 40; ++i) {
        const double mid = 0.5 * (low + high);
        (apoapsisAfter(mid) < wanted ? low : high) = mid;
    }
    return direction(0.5 * (low + high));
}

Vec3 thrustDirection(GuidancePhase phase, const Vec3& r, const Vec3& v, const Vec3& relativeVel,
                     double kickAngle, const Vec3& launchPlaneNormal, double mu,
                     double thrustAcceleration) {
    const Vec3 up = normalized(r);
    const Vec3 downrange = downrangeDirection(r, launchPlaneNormal);

    switch (phase) {
    case GuidancePhase::Vertical:
        return up;
    case GuidancePhase::Kick:
        return std::cos(kickAngle) * up + std::sin(kickAngle) * downrange;
    case GuidancePhase::GravityTurn:
        // Barely moving relative to the ground: no useful direction yet, keep going up.
        return norm(relativeVel) > 1e-3 ? normalized(relativeVel) : up;
    case GuidancePhase::Circularize: {
        const Vec3 deltaV = circularizationDeltaV(r, v, downrange, mu);
        if (thrustAcceleration <= 0.0) return normalized(deltaV);
        // Gravity compensation: over the time the burn still needs, gravity
        // (less what the sideways speed already holds up) will pull the
        // rocket down by netGravity × timeToGo. Aim to supply that too.
        const double radius = norm(r);
        const Vec3 horizontal = v - dot(v, up) * up;
        const double netGravity =
            mu / (radius * radius) - normSquared(horizontal) / radius; // g − v_h²/r
        const double timeToGo = norm(deltaV) / thrustAcceleration;
        return normalized(deltaV + netGravity * timeToGo * up);
    }
    }
    return up; // unreachable; keeps compilers happy
}

} // namespace orbit
