// SPDX-License-Identifier: GPL-3.0-or-later
// Earth geometry, rotation and the launch plane.

#include "orbit/core/angles.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/earth.hpp"
#include "orbit/core/guidance.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace orbit;
using Catch::Matchers::WithinAbs;

namespace {
void requireNear(const Vec3& a, const Vec3& b, double tolerance) {
    CAPTURE(a, b);
    REQUIRE(norm(a - b) < tolerance);
}
} // namespace

TEST_CASE("The local frame is orthonormal and right-handed everywhere", "[earth]") {
    const double latDeg = GENERATE(-90.0, -45.0, 0.0, 28.5, 89.9, 90.0);
    const double lonDeg = GENERATE(-170.0, 0.0, 80.0);
    CAPTURE(latDeg, lonDeg);

    const LocalFrame f = localFrame(positionFromLatLon(degToRad(latDeg), degToRad(lonDeg), 7e6));
    REQUIRE_THAT(norm(f.east), WithinAbs(1.0, 1e-12));
    REQUIRE_THAT(norm(f.north), WithinAbs(1.0, 1e-12));
    REQUIRE_THAT(norm(f.up), WithinAbs(1.0, 1e-12));
    REQUIRE_THAT(dot(f.east, f.north), WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(dot(f.east, f.up), WithinAbs(0.0, 1e-12));
    requireNear(cross(f.east, f.north), f.up, 1e-12); // east × north = up
    // North never points away from the north pole (except at the poles, where
    // it is undefined).
    if (std::abs(latDeg) < 89.0) REQUIRE(f.north.z > 0.0);
}

TEST_CASE("Azimuth 0 is north and 90° is east", "[earth]") {
    const LocalFrame f = localFrame(positionFromLatLon(degToRad(40.0), degToRad(10.0), 7e6));
    requireNear(horizontalDirection(f, 0.0), f.north, 1e-12);
    requireNear(horizontalDirection(f, degToRad(90.0)), f.east, 1e-12);
    requireNear(horizontalDirection(f, degToRad(180.0)), -f.north, 1e-12);
}

TEST_CASE("The ground moves east at ω·R·cos(latitude)", "[earth]") {
    const double lat = degToRad(GENERATE(0.0, 28.5, 60.0, 90.0));
    const Vec3 r = positionFromLatLon(lat, degToRad(-80.0), kRadiusEarth);
    const Vec3 v = groundVelocity(r);

    REQUIRE_THAT(norm(v), WithinAbs(kOmegaEarth * kRadiusEarth * std::cos(lat), 1e-9));
    if (std::cos(lat) > 1e-6) requireNear(normalized(v), localFrame(r).east, 1e-12);
    // At the equator this is the famous ~465 m/s.
    if (lat == 0.0) REQUIRE_THAT(norm(v), WithinAbs(464.6, 0.1));
}

TEST_CASE("Ground point round-trips through the Earth's rotation", "[earth]") {
    const double lat = degToRad(-33.9);
    const double lon = degToRad(151.2);
    const double theta = earthRotationAngle(3600.0, degToRad(20.0));
    const Vec3 rEci = rotateZ(positionFromLatLon(lat, lon, kRadiusEarth + 1000.0), theta);

    const GeoPoint p = groundPoint(rEci, theta);
    REQUIRE_THAT(p.latitude, WithinAbs(lat, 1e-12));
    REQUIRE_THAT(p.longitude, WithinAbs(lon, 1e-12));
    REQUIRE_THAT(p.altitude, WithinAbs(1000.0, 1e-6));
    // One hour of rotation is 15.04° (a sidereal day is a bit under 24 h).
    REQUIRE_THAT(radToDeg(theta - degToRad(20.0)), WithinAbs(15.041, 0.001));
}

TEST_CASE("Downrange starts along the launch azimuth and stays horizontal", "[earth]") {
    const double latDeg = GENERATE(0.0, 28.5, 90.0, -90.0);
    const double azDeg = GENERATE(0.0, 45.0, 90.0, 200.0);
    CAPTURE(latDeg, azDeg);

    const Vec3 site = positionFromLatLon(degToRad(latDeg), degToRad(-80.6), kRadiusEarth);
    const Vec3 n = launchPlaneNormal(site, degToRad(azDeg));
    requireNear(downrangeDirection(site, n), horizontalDirection(localFrame(site), degToRad(azDeg)),
                1e-12);

    // 1000 km further along the launch plane, downrange is still horizontal
    // and still in the plane.
    const Vec3 later = rotateAxis(site, n, 1e6 / kRadiusEarth);
    const Vec3 d = downrangeDirection(later, n);
    REQUIRE_THAT(dot(d, normalized(later)), WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(dot(d, n), WithinAbs(0.0, 1e-12));
}

TEST_CASE("eciToEcef undoes the Earth's rotation", "[earth]") {
    const Vec3 ecef = positionFromLatLon(degToRad(28.5), degToRad(-80.6), kRadiusEarth);
    const double theta = earthRotationAngle(5000.0, 0.3);
    const Vec3 eci = rotateZ(ecef, theta);
    requireNear(eciToEcef(eci, theta), ecef, 1e-6);
    REQUIRE_THAT(groundPoint(eci, theta).latitude, WithinAbs(degToRad(28.5), 1e-12));
}
