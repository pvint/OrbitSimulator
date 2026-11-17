// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/orbit.hpp"

#include "orbit/core/angles.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orbit {

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// Below these, an orbit counts as circular / equatorial and the angles that
/// depend on periapsis / the node are measured from elsewhere instead.
constexpr double kCircularEpsilon = 1e-9;
constexpr double kEquatorialEpsilon = 1e-9;

/// Angle between two vectors in [0, π]. Clamped so rounding can't push acos
/// outside its domain.
double angleBetween(const Vec3& a, const Vec3& b) {
    const double denom = norm(a) * norm(b);
    if (denom == 0.0) return 0.0;
    return std::acos(std::clamp(dot(a, b) / denom, -1.0, 1.0));
}

} // namespace

OrbitalElements elementsFromState(const Vec3& r, const Vec3& v, double mu, double bodyRadius) {
    OrbitalElements el;
    const double rMag = norm(r);
    const double v2 = normSquared(v);

    el.energy = v2 / 2.0 - mu / rMag;
    const Vec3 h = cross(r, v);
    el.angularMomentum = norm(h);

    // Eccentricity vector: points at periapsis, length e.
    const Vec3 eVec = ((v2 - mu / rMag) * r - dot(r, v) * v) / mu;
    el.eccentricity = norm(eVec);

    el.bound = el.energy < 0.0;
    el.semiMajorAxis = el.energy != 0.0 ? -mu / (2.0 * el.energy) : kInfinity;
    const double p = el.angularMomentum * el.angularMomentum / mu;
    el.periapsis = p / (1.0 + el.eccentricity);
    // 2a − r_p equals p/(1 − e) for an ordinary ellipse, and still works for
    // a straight up-and-down path, where h = 0 and e = 1 although it's bound.
    el.apoapsis = el.bound ? 2.0 * el.semiMajorAxis - el.periapsis : kInfinity;
    el.periapsisAltitude = el.periapsis - bodyRadius;
    el.apoapsisAltitude = el.apoapsis - bodyRadius;
    el.period = el.bound ? orbitalPeriod(el.semiMajorAxis, mu) : kInfinity;

    if (el.angularMomentum == 0.0) return el; // radial path: no orbital plane

    // Orientation. The node vector n = z × h points to the ascending node.
    el.inclination = std::acos(std::clamp(h.z / el.angularMomentum, -1.0, 1.0));
    const Vec3 n = cross(Vec3{0.0, 0.0, 1.0}, h);
    const bool equatorial = norm(n) < kEquatorialEpsilon * el.angularMomentum;
    const bool circular = el.eccentricity < kCircularEpsilon;

    // Reference direction in the orbit plane from which ω and ν are measured:
    // the ascending node, or the x axis if the orbit has no node.
    const Vec3 reference = equatorial ? Vec3{1.0, 0.0, 0.0} : normalized(n);
    el.raan = equatorial ? 0.0 : wrapTwoPi(std::atan2(n.y, n.x));

    // Angle from `reference` to `target`, measured in the direction of motion.
    const auto angleInPlane = [&](const Vec3& target) {
        const double angle = angleBetween(reference, target);
        // dot(h, reference × target) < 0 means target lies "behind" reference.
        return dot(h, cross(reference, target)) < 0.0 ? kTwoPi - angle : angle;
    };

    if (circular) {
        el.argumentOfPeriapsis = 0.0;
        el.trueAnomaly = angleInPlane(r); // argument of latitude / true longitude
    } else {
        el.argumentOfPeriapsis = angleInPlane(eVec);
        const double nu = angleBetween(eVec, r);
        el.trueAnomaly = dot(r, v) < 0.0 ? kTwoPi - nu : nu; // falling back = past apoapsis
    }
    return el;
}

PositionVelocity stateFromElements(double semiMajorAxis, double eccentricity, double inclination,
                                   double raan, double argumentOfPeriapsis, double trueAnomaly,
                                   double mu) {
    // Position and velocity in the orbit's own plane, with x towards periapsis.
    const double p = semiMajorAxis * (1.0 - eccentricity * eccentricity);
    const double cosNu = std::cos(trueAnomaly);
    const double sinNu = std::sin(trueAnomaly);
    const double r = p / (1.0 + eccentricity * cosNu);
    const Vec3 rPlane{r * cosNu, r * sinNu, 0.0};
    const Vec3 vPlane = std::sqrt(mu / p) * Vec3{-sinNu, eccentricity + cosNu, 0.0};

    // Turn the plane into place: rotate by ω within the plane, tilt by i about
    // the node line, then swing the node round by Ω.
    const auto orient = [&](const Vec3& a) {
        return rotateZ(rotateX(rotateZ(a, argumentOfPeriapsis), inclination), raan);
    };
    return {orient(rPlane), orient(vPlane)};
}

double orbitalPeriod(double semiMajorAxis, double mu) {
    return kTwoPi * std::sqrt(semiMajorAxis * semiMajorAxis * semiMajorAxis / mu);
}

double circularSpeed(double r, double mu) { return std::sqrt(mu / r); }

double escapeSpeed(double r, double mu) { return std::sqrt(2.0 * mu / r); }

} // namespace orbit
