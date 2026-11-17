// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/integrator.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace orbit {

namespace {

// Butcher tableau of the Dormand–Prince 5(4) method. c are the stage times
// as fractions of the step; a are the weights that combine earlier stages.
constexpr double c2 = 1.0 / 5.0, c3 = 3.0 / 10.0, c4 = 4.0 / 5.0, c5 = 8.0 / 9.0;

constexpr double a21 = 1.0 / 5.0;
constexpr double a31 = 3.0 / 40.0, a32 = 9.0 / 40.0;
constexpr double a41 = 44.0 / 45.0, a42 = -56.0 / 15.0, a43 = 32.0 / 9.0;
constexpr double a51 = 19372.0 / 6561.0, a52 = -25360.0 / 2187.0, a53 = 64448.0 / 6561.0,
                 a54 = -212.0 / 729.0;
constexpr double a61 = 9017.0 / 3168.0, a62 = -355.0 / 33.0, a63 = 46732.0 / 5247.0,
                 a64 = 49.0 / 176.0, a65 = -5103.0 / 18656.0;

// 5th-order solution weights (also the 7th stage's row: "first same as last").
constexpr double b1 = 35.0 / 384.0, b3 = 500.0 / 1113.0, b4 = 125.0 / 192.0, b5 = -2187.0 / 6784.0,
                 b6 = 11.0 / 84.0;

// Error weights: 5th-order minus 4th-order weights.
constexpr double e1 = 71.0 / 57600.0, e3 = -71.0 / 16695.0, e4 = 71.0 / 1920.0,
                 e5 = -17253.0 / 339200.0, e6 = 22.0 / 525.0, e7 = -1.0 / 40.0;

// Dense output weights (Hairer's DOPRI5 "contd5").
constexpr double d1 = -12715105075.0 / 11282082432.0, d3 = 87487479700.0 / 32700410799.0,
                 d4 = -10690763975.0 / 1880347072.0, d5 = 701980252875.0 / 199316789632.0,
                 d6 = -1453857185.0 / 822651844.0, d7 = 69997945.0 / 29380423.0;

// Step size control: never shrink below 1/5 or grow beyond 5× in one go,
// and aim a little under the tolerance (safety factor).
constexpr double kSafety = 0.9;
constexpr double kMinFactor = 0.2;
constexpr double kMaxFactor = 5.0;

} // namespace

DormandPrince45::DormandPrince45(Derivative f, Tolerances tolerances)
    : m_f(std::move(f)), m_tol(tolerances) {}

DormandPrince45::StepResult DormandPrince45::step(double t, const State& y, double h) {
    const State k1 = m_f(t, y);
    const State k2 = m_f(t + c2 * h, y + h * (a21 * k1));
    const State k3 = m_f(t + c3 * h, y + h * (a31 * k1 + a32 * k2));
    const State k4 = m_f(t + c4 * h, y + h * (a41 * k1 + a42 * k2 + a43 * k3));
    const State k5 = m_f(t + c5 * h, y + h * (a51 * k1 + a52 * k2 + a53 * k3 + a54 * k4));
    const State k6 = m_f(t + h, y + h * (a61 * k1 + a62 * k2 + a63 * k3 + a64 * k4 + a65 * k5));
    const State y1 = y + h * (b1 * k1 + b3 * k3 + b4 * k4 + b5 * k5 + b6 * k6);
    const State k7 = m_f(t + h, y1);

    const State errorState = h * (e1 * k1 + e3 * k3 + e4 * k4 + e5 * k5 + e6 * k6 + e7 * k7);
    const double err = errorNorm(errorState, y, y1);

    StepResult result;
    result.error = err;
    // The error of a 5(4) pair scales like h⁵, hence the 1/5 power.
    const double factor = err > 0.0 ? kSafety * std::pow(err, -1.0 / 5.0) : kMaxFactor;

    if (err <= 1.0) {
        result.accepted = true;
        result.y = y1;
        result.nextStep = h * std::clamp(factor, kMinFactor, kMaxFactor);

        m_t0 = t;
        m_h = h;
        const State dy = y1 - y;
        const State bspl = h * k1 - dy;
        m_dense = {y, dy, bspl, dy - h * k7 - bspl,
                   h * (d1 * k1 + d3 * k3 + d4 * k4 + d5 * k5 + d6 * k6 + d7 * k7)};
    } else {
        // Rejected: retry with a smaller step (never grow after a failure).
        result.nextStep = h * std::clamp(factor, kMinFactor, 1.0);
    }
    return result;
}

State DormandPrince45::interpolate(double t) const {
    const double theta = m_h != 0.0 ? (t - m_t0) / m_h : 0.0;
    const double theta1 = 1.0 - theta;
    const auto& c = m_dense;
    return c[0] + theta * (c[1] + theta1 * (c[2] + theta * (c[3] + theta1 * c[4])));
}

/// Root-mean-square of each component's error divided by its allowed error.
double DormandPrince45::errorNorm(const State& error, const State& y0, const State& y1) const {
    double sum = 0.0;
    const auto add = [&](double err, double v0, double v1, double absolute) {
        const double scale = absolute + m_tol.relative * std::max(std::abs(v0), std::abs(v1));
        const double ratio = err / scale;
        sum += ratio * ratio;
    };
    add(error.r.x, y0.r.x, y1.r.x, m_tol.position);
    add(error.r.y, y0.r.y, y1.r.y, m_tol.position);
    add(error.r.z, y0.r.z, y1.r.z, m_tol.position);
    add(error.v.x, y0.v.x, y1.v.x, m_tol.velocity);
    add(error.v.y, y0.v.y, y1.v.y, m_tol.velocity);
    add(error.v.z, y0.v.z, y1.v.z, m_tol.velocity);
    add(error.m, y0.m, y1.m, m_tol.mass);
    return std::sqrt(sum / 7.0);
}

} // namespace orbit
