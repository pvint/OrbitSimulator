// SPDX-License-Identifier: GPL-3.0-or-later
// Earth's geometry and rotation.
//
// FRAMES
// The simulation runs in an Earth-centred inertial frame (ECI): the origin is
// Earth's centre, z points along the spin axis to the north pole, and the
// axes do not rotate with the Earth. Points on the ground are given in an
// Earth-fixed frame (ECEF), which turns with the Earth. The two share the z
// axis and differ by the Earth rotation angle θ:
//     r_eci = Rz(θ) · r_ecef,      θ(t) = θ₀ + ω·t
// θ₀ is the angle at launch. Until the Moon arrives (M5) it is simply 0, so
// at t = 0 the two frames coincide.
#pragma once

#include "orbit/core/vec3.hpp"

namespace orbit {

/// Local directions at a point above the Earth: east, north, and up (away
/// from Earth's centre). All unit vectors, mutually perpendicular.
struct LocalFrame {
    Vec3 east;
    Vec3 north;
    Vec3 up;
};

/// Latitude and longitude (radians) and altitude above the sphere (m).
struct GeoPoint {
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
};

/// East/north/up at position r. The frame only depends on the direction of r,
/// so it is the same whether r is given in ECI or ECEF.
/// At the poles, east is undefined. There we pick east = +y, so that north
/// still points along the meridian of longitude 0.
LocalFrame localFrame(const Vec3& r);

/// Direction pointing along the ground at the given azimuth (radians,
/// clockwise from north: 0 = north, π/2 = east).
Vec3 horizontalDirection(const LocalFrame& frame, double azimuth);

/// Position of a point at the given latitude/longitude (radians) and distance
/// from Earth's centre, in the Earth-fixed frame.
Vec3 positionFromLatLon(double latitude, double longitude, double radius);

/// Earth rotation angle θ at time t (s), given the angle at t = 0.
double earthRotationAngle(double t, double angleAtStart);

/// The point on the ground directly below an ECI position.
GeoPoint groundPoint(const Vec3& rEci, double rotationAngle);

/// Velocity of the ground (or of anything resting on it) at ECI position r:
/// ω × r, with ω along +z. At the equator this is about 465 m/s eastward.
Vec3 groundVelocity(const Vec3& r);

} // namespace orbit
