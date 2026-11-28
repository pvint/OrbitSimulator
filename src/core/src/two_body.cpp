// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/two_body.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace orbit {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// Below this |z|, the series: the closed forms subtract nearly equal numbers.
constexpr double kSeriesLimit = 1e-3;

} // namespace

double stumpffC(double z) {
    if (std::abs(z) < kSeriesLimit) {
        // 1/2! − z/4! + z²/6! − z³/8!
        return 1.0 / 2.0 - z / 24.0 + z * z / 720.0 - z * z * z / 40320.0;
    }
    if (z > 0.0) {
        // 1 − cos u = 2 sin²(u/2): the same, but it keeps its precision
        // near a full turn (u → 2π), where cos u rounds to exactly 1.
        const double half = std::sin(0.5 * std::sqrt(z));
        return 2.0 * half * half / z;
    }
    return (std::cosh(std::sqrt(-z)) - 1.0) / (-z);
}

double stumpffS(double z) {
    if (std::abs(z) < kSeriesLimit) {
        // 1/3! − z/5! + z²/7! − z³/9!
        return 1.0 / 6.0 - z / 120.0 + z * z / 5040.0 - z * z * z / 362880.0;
    }
    if (z > 0.0) {
        const double s = std::sqrt(z);
        return (s - std::sin(s)) / (s * s * s);
    }
    const double s = std::sqrt(-z);
    return (std::sinh(s) - s) / (s * s * s);
}

TwoBodyState propagateKepler(const TwoBodyState& start, double dt, double mu) {
    const double r0 = norm(start.r);
    const double v0 = norm(start.v);
    const double sqrtMu = std::sqrt(mu);
    const double vr0 = dot(start.r, start.v) / r0; // radial speed
    const double alpha = 2.0 / r0 - v0 * v0 / mu;  // 1/a (energy, in disguise)

    // An ellipse repeats: whole periods change nothing, and leaving them out
    // keeps Newton's method close to home.
    if (alpha > 1e-15) {
        const double period = kTwoPi / std::sqrt(mu * alpha * alpha * alpha);
        dt = std::fmod(dt, period);
    }
    if (dt == 0.0) return start;

    // A first guess for χ (Vallado, "Fundamentals of Astrodynamics and
    // Applications", algorithm 8): for an ellipse, χ grows about evenly with
    // time; for a hyperbola, logarithmically.
    double chi = 0.0;
    if (alpha > 1e-15) {
        chi = sqrtMu * alpha * dt;
    } else if (alpha < -1e-15) {
        const double a = 1.0 / alpha;
        const double sign = dt > 0.0 ? 1.0 : -1.0;
        const double arg = -2.0 * mu * alpha * dt /
                           (dot(start.r, start.v) + sign * std::sqrt(-mu * a) * (1.0 - r0 * alpha));
        chi = arg > 0.0 ? sign * std::sqrt(-a) * std::log(arg) : sqrtMu * dt / r0;
    } else {
        chi = sqrtMu * dt / r0;
    }

    // Newton's method on the universal Kepler equation F(χ) = 0. Its
    // derivative dF/dχ happens to be r, the distance at that χ.
    for (int i = 0; i < 100; ++i) {
        const double z = alpha * chi * chi;
        const double c = stumpffC(z);
        const double s = stumpffS(z);
        const double f = r0 * vr0 / sqrtMu * chi * chi * c +
                         (1.0 - alpha * r0) * chi * chi * chi * s + r0 * chi - sqrtMu * dt;
        const double df =
            r0 * vr0 / sqrtMu * chi * (1.0 - z * s) + (1.0 - alpha * r0) * chi * chi * c + r0;
        const double step = f / df;
        chi -= step;
        if (std::abs(step) <= 1e-12 * std::max(1.0, std::abs(chi))) break;
    }

    // The Lagrange coefficients: the new state is a mix of the old position
    // and velocity, because the orbit stays in their plane.
    const double z = alpha * chi * chi;
    const double c = stumpffC(z);
    const double s = stumpffS(z);
    const double f = 1.0 - chi * chi / r0 * c;
    const double g = dt - chi * chi * chi * s / sqrtMu;
    const Vec3 r = f * start.r + g * start.v;
    const double rNorm = norm(r);
    const double fDot = sqrtMu / (rNorm * r0) * (alpha * chi * chi * chi * s - chi);
    const double gDot = 1.0 - chi * chi / rNorm * c;
    return {r, fDot * start.r + gDot * start.v};
}

std::optional<LambertSolution> solveLambert(const Vec3& r1, const Vec3& r2, double dt, double mu,
                                            bool prograde) {
    if (!(dt > 0.0)) return std::nullopt;
    const double r1n = norm(r1);
    const double r2n = norm(r2);
    if (r1n == 0.0 || r2n == 0.0) return std::nullopt;

    // The angle swept from r1 to r2 in the direction of travel: the short
    // way if that is the way the craft goes round, otherwise the long way.
    const double cosTheta = std::clamp(dot(r1, r2) / (r1n * r2n), -1.0, 1.0);
    double theta = std::acos(cosTheta);
    const bool anticlockwise = cross(r1, r2).z >= 0.0;
    if (anticlockwise != prograde) theta = kTwoPi - theta;

    // A depends only on the geometry. It is zero when the points are exactly
    // opposite (or the same), where the plane of the orbit is not defined.
    const double a = std::sin(theta) * std::sqrt(r1n * r2n / (1.0 - cosTheta));
    if (!std::isfinite(a) || std::abs(a) < 1e-9 * std::sqrt(r1n * r2n)) return std::nullopt;

    // y(z) and the flight time t(z) for a trial z = α χ²: t grows steadily
    // with z, so bisection finds the z whose time matches. Where y < 0 there
    // is no orbit; it lies below every real one, so it counts as "too fast".
    const auto y = [&](double z) {
        return r1n + r2n + a * (z * stumpffS(z) - 1.0) / std::sqrt(stumpffC(z));
    };
    const double sqrtMu = std::sqrt(mu);
    const auto timeOfFlight = [&](double z) {
        const double yz = y(z);
        if (yz < 0.0) return -std::numeric_limits<double>::infinity();
        const double c = stumpffC(z);
        return (std::pow(yz / c, 1.5) * stumpffS(z) + a * std::sqrt(yz)) / sqrtMu;
    };

    // z = 4π² is a full revolution (C = 0 there): the upper end. The lower
    // end, deep in hyperbolas, moves down until it is fast enough.
    double high = 4.0 * std::numbers::pi * std::numbers::pi - 1e-6;
    double low = -4.0 * std::numbers::pi * std::numbers::pi;
    while (timeOfFlight(low) > dt) {
        low *= 2.0;
        if (low < -1e5) return std::nullopt; // would need an absurd speed
    }
    if (timeOfFlight(high) < dt) return std::nullopt; // more than one revolution
    for (int i = 0; i < 200 && high - low > 1e-14 * std::max(1.0, std::abs(low)); ++i) {
        const double mid = 0.5 * (low + high);
        (timeOfFlight(mid) < dt ? low : high) = mid;
    }
    const double z = 0.5 * (low + high);
    const double yz = y(z);
    if (!(yz > 0.0) || std::abs(timeOfFlight(z) - dt) > 1e-6 * dt) return std::nullopt;

    // Lagrange coefficients again, this time solved for the velocities.
    const double f = 1.0 - yz / r1n;
    const double g = a * std::sqrt(yz / mu);
    const double gDot = 1.0 - yz / r2n;
    return LambertSolution{(r2 - f * r1) / g, (gDot * r2 - r1) / g};
}

} // namespace orbit
