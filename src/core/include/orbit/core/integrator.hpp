// SPDX-License-Identifier: GPL-3.0-or-later
// Dormand–Prince 5(4): an adaptive Runge–Kutta integrator.
//
// HOW IT WORKS
// Each step evaluates the equations of motion at 7 points within the step
// and combines them in two ways: a 5th-order estimate (used to advance) and a
// 4th-order one. Their difference estimates the error of the step. If that
// error is within tolerance the step is accepted; either way, the size of the
// next step is adjusted so the error lands just under tolerance. The result
// is long steps where little happens (coasting far from Earth) and short ones
// where things change fast (a hard burn, a close flyby).
//
// DENSE OUTPUT
// After an accepted step, interpolate(t) gives the state at any time inside
// that step, accurate to 4th order, at no extra cost. The simulation uses it
// to draw smooth output and to find the exact moment an event happens
// (engine cutoff, apoapsis, impact) without forcing tiny steps everywhere.
//
// Reference: E. Hairer, S. P. Nørsett, G. Wanner, "Solving Ordinary
// Differential Equations I", 2nd ed., Springer 1993, section II.5 and the
// DOPRI5 code.
#pragma once

#include "orbit/core/state.hpp"

#include <array>
#include <functional>

namespace orbit {

/// How accurate each step must be. A component's allowed error is
/// absolute + relative · |value|.
struct Tolerances {
    double relative = 1e-10;
    double position = 1e-4; ///< m
    double velocity = 1e-7; ///< m/s
    double mass = 1e-6;     ///< kg
};

/// Equations of motion: rate of change of the state at time t.
using Derivative = std::function<State(double t, const State& y)>;

class DormandPrince45 {
public:
    DormandPrince45(Derivative f, Tolerances tolerances);

    struct StepResult {
        bool accepted = false;
        State y;               ///< state at t + h (only meaningful if accepted)
        double error = 0.0;    ///< error estimate relative to tolerance (≤ 1 to accept)
        double nextStep = 0.0; ///< suggested size of the next step (or of the retry)
    };

    /// Try one step of size h from state y at time t. If accepted, the step
    /// becomes available for interpolate().
    StepResult step(double t, const State& y, double h);

    /// State at time t within the last accepted step [t0, t0 + h].
    State interpolate(double t) const;

private:
    Derivative m_f;
    Tolerances m_tol;

    // Dense output of the last accepted step: the state at t0 + θh is
    // c0 + θ(c1 + (1−θ)(c2 + θ(c3 + (1−θ)c4))).
    double m_t0 = 0.0;
    double m_h = 0.0;
    std::array<State, 5> m_dense{};

    double errorNorm(const State& error, const State& y0, const State& y1) const;
};

} // namespace orbit
