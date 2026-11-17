// SPDX-License-Identifier: GPL-3.0-or-later
// The rocket's state: everything the equations of motion need to step forward.
#pragma once

#include "orbit/core/vec3.hpp"

namespace orbit {

/// Position and velocity in the Earth-centred inertial frame (m, m/s), and
/// the rocket's total mass (kg).
///
/// The integrator also uses State for rates of change: then r holds dr/dt
/// (= velocity), v holds dv/dt (= acceleration) and m holds dm/dt.
struct State {
    Vec3 r;
    Vec3 v;
    double m = 0.0;

    constexpr State& operator+=(const State& o) {
        r += o.r;
        v += o.v;
        m += o.m;
        return *this;
    }
    constexpr State& operator*=(double s) {
        r *= s;
        v *= s;
        m *= s;
        return *this;
    }
};

constexpr State operator+(State a, const State& b) { return a += b; }
constexpr State operator-(State a, const State& b) { return a += State{-b.r, -b.v, -b.m}; }
constexpr State operator*(State a, double s) { return a *= s; }
constexpr State operator*(double s, State a) { return a *= s; }

} // namespace orbit
