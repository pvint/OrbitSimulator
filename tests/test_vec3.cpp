// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/vec3.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using orbit::Vec3;

TEST_CASE("Vec3 arithmetic", "[vec3]") {
    constexpr Vec3 a{1.0, 2.0, 3.0};
    constexpr Vec3 b{4.0, -5.0, 6.0};

    STATIC_REQUIRE(a + b == Vec3{5.0, -3.0, 9.0});
    STATIC_REQUIRE(a - b == Vec3{-3.0, 7.0, -3.0});
    STATIC_REQUIRE(2.0 * a == Vec3{2.0, 4.0, 6.0});
    STATIC_REQUIRE(a * 2.0 == 2.0 * a);
    STATIC_REQUIRE(-a == Vec3{-1.0, -2.0, -3.0});
    STATIC_REQUIRE(a / 2.0 == Vec3{0.5, 1.0, 1.5});
}

TEST_CASE("Vec3 dot and cross products", "[vec3]") {
    constexpr Vec3 x{1.0, 0.0, 0.0};
    constexpr Vec3 y{0.0, 1.0, 0.0};
    constexpr Vec3 z{0.0, 0.0, 1.0};

    STATIC_REQUIRE(orbit::dot(x, y) == 0.0);
    STATIC_REQUIRE(orbit::dot(Vec3{1, 2, 3}, Vec3{4, 5, 6}) == 32.0);

    // Right-handed: x × y = z, y × z = x, z × x = y.
    STATIC_REQUIRE(orbit::cross(x, y) == z);
    STATIC_REQUIRE(orbit::cross(y, z) == x);
    STATIC_REQUIRE(orbit::cross(z, x) == y);
    STATIC_REQUIRE(orbit::cross(y, x) == -z);
}

TEST_CASE("Vec3 length and normalisation", "[vec3]") {
    REQUIRE_THAT(orbit::norm(Vec3{3.0, 4.0, 12.0}), WithinAbs(13.0, 1e-12));

    const Vec3 n = orbit::normalized(Vec3{0.0, -7.0, 0.0});
    REQUIRE(n == Vec3{0.0, -1.0, 0.0});

    // The zero vector has no direction; normalized() returns zero rather than NaN.
    REQUIRE(orbit::normalized(Vec3{}) == Vec3{});
}

TEST_CASE("Vec3 rotations", "[vec3]") {
    constexpr double quarterTurn = 1.5707963267948966; // π/2
    const auto near = [](const Vec3& a, const Vec3& b) { return orbit::norm(a - b) < 1e-12; };

    // Counter-clockwise about +z takes x to y; about +x takes y to z.
    REQUIRE(near(orbit::rotateZ(Vec3{1, 0, 0}, quarterTurn), Vec3{0, 1, 0}));
    REQUIRE(near(orbit::rotateX(Vec3{0, 1, 0}, quarterTurn), Vec3{0, 0, 1}));

    // rotateAxis agrees with the special cases, and keeps lengths.
    const Vec3 v{0.3, -1.2, 2.5};
    REQUIRE(near(orbit::rotateAxis(v, Vec3{0, 0, 1}, 0.7), orbit::rotateZ(v, 0.7)));
    REQUIRE(near(orbit::rotateAxis(v, Vec3{1, 0, 0}, -2.1), orbit::rotateX(v, -2.1)));
    const Vec3 axis = orbit::normalized(Vec3{1, 1, 1});
    REQUIRE_THAT(orbit::norm(orbit::rotateAxis(v, axis, 1.234)), WithinAbs(orbit::norm(v), 1e-12));
    // A third of a turn about (1,1,1) cycles the axes: x → y.
    REQUIRE(near(orbit::rotateAxis(Vec3{1, 0, 0}, axis, 2.0943951023931957), Vec3{0, 1, 0}));
}
