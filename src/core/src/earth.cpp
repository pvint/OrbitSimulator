// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/earth.hpp"

#include "orbit/core/constants.hpp"

#include <cmath>

namespace orbit {

namespace {
constexpr Vec3 kSpinAxis{0.0, 0.0, 1.0};
} // namespace

LocalFrame localFrame(const Vec3& r) {
    const Vec3 up = normalized(r);
    Vec3 east = cross(kSpinAxis, up);
    // At a pole the spin axis and up are parallel and the cross product vanishes.
    east = norm(east) > 1e-12 ? normalized(east) : Vec3{0.0, 1.0, 0.0};
    const Vec3 north = cross(up, east);
    return {east, north, up};
}

Vec3 horizontalDirection(const LocalFrame& frame, double azimuth) {
    return std::cos(azimuth) * frame.north + std::sin(azimuth) * frame.east;
}

Vec3 positionFromLatLon(double latitude, double longitude, double radius) {
    const double c = std::cos(latitude);
    return radius * Vec3{c * std::cos(longitude), c * std::sin(longitude), std::sin(latitude)};
}

double earthRotationAngle(double t, double angleAtStart) { return angleAtStart + kOmegaEarth * t; }

GeoPoint groundPoint(const Vec3& rEci, double rotationAngle) {
    const Vec3 rEcef = rotateZ(rEci, -rotationAngle);
    const double r = norm(rEcef);
    return {std::asin(rEcef.z / r), std::atan2(rEcef.y, rEcef.x), r - kRadiusEarth};
}

Vec3 groundVelocity(const Vec3& r) { return cross(kOmegaEarth * kSpinAxis, r); }

} // namespace orbit
