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
