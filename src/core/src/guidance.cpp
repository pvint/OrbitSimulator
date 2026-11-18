// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/guidance.hpp"

#include "orbit/core/earth.hpp"

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
