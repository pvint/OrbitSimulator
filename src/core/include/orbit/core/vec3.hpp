// SPDX-License-Identifier: GPL-3.0-or-later
// A small 3D vector of doubles for positions, velocities and accelerations.
//
// Physics always uses double precision. Qt's own QVector3D uses float, which
// only resolves about 0.5 m at Earth's radius and about 30 m at the Moon's
// distance. That is fine for drawing but not for integrating orbits.
#pragma once

#include <cmath>
#include <iosfwd>

namespace orbit {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Vec3& operator+=(const Vec3& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    constexpr Vec3& operator-=(const Vec3& o) {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    constexpr Vec3& operator*=(double s) {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }

    friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

constexpr Vec3 operator+(Vec3 a, const Vec3& b) { return a += b; }
constexpr Vec3 operator-(Vec3 a, const Vec3& b) { return a -= b; }
constexpr Vec3 operator-(const Vec3& a) { return {-a.x, -a.y, -a.z}; }
constexpr Vec3 operator*(Vec3 a, double s) { return a *= s; }
constexpr Vec3 operator*(double s, Vec3 a) { return a *= s; }
constexpr Vec3 operator/(const Vec3& a, double s) { return {a.x / s, a.y / s, a.z / s}; }

constexpr double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// a × b, perpendicular to both. For position × velocity it is the angular
/// momentum per unit mass, which points along the orbit's axis.
constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

constexpr double normSquared(const Vec3& a) { return dot(a, a); }
inline double norm(const Vec3& a) { return std::sqrt(normSquared(a)); }

/// Unit vector in the direction of a, or the zero vector if a is zero.
inline Vec3 normalized(const Vec3& a) {
    const double n = norm(a);
    return n > 0.0 ? a / n : Vec3{};
}

/// Prints as "(x, y, z)". Used by test failure messages and the CLI.
std::ostream& operator<<(std::ostream& os, const Vec3& v);

} // namespace orbit
