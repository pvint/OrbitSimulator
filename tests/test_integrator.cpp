// SPDX-License-Identifier: GPL-3.0-or-later
// Dormand–Prince 5(4) on problems with known exact solutions.

#include "orbit/core/integrator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// Harmonic oscillator x'' = −x in r.x / v.x. Exact solution from x=1, v=0:
/// x = cos t, v = −sin t.
State oscillator(double /*t*/, const State& y) { return {y.v, -y.r, 0.0}; }

/// Integrate from t0 to t1, landing exactly on t1. Returns the number of
/// accepted steps.
int integrate(DormandPrince45& stepper, State& y, double t0, double t1, double h = 0.01) {
    double t = t0;
    int accepted = 0;
    while (t < t1) {
        const double hStep = std::min(h, t1 - t);
        const auto result = stepper.step(t, y, hStep);
        h = result.nextStep;
        if (!result.accepted) continue;
        y = result.y;
        t += hStep;
        ++accepted;
    }
    return accepted;
}

} // namespace

TEST_CASE("DP45 follows a harmonic oscillator", "[integrator]") {
    DormandPrince45 stepper(
        oscillator,
        Tolerances{.relative = 1e-12, .position = 1e-12, .velocity = 1e-12, .mass = 1e-12});
    State y{{1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 0.0};
    integrate(stepper, y, 0.0, 10.0);

    REQUIRE_THAT(y.r.x, WithinAbs(std::cos(10.0), 1e-9));
    REQUIRE_THAT(y.v.x, WithinAbs(-std::sin(10.0), 1e-9));
}

TEST_CASE("DP45 follows exponential decay", "[integrator]") {
    const auto decay = [](double /*t*/, const State& y) { return State{{}, {}, -y.m}; };
    DormandPrince45 stepper(decay, Tolerances{.relative = 1e-12, .mass = 1e-14});
    State y{{}, {}, 1.0};
    integrate(stepper, y, 0.0, 5.0);

    REQUIRE_THAT(y.m, WithinRel(std::exp(-5.0), 1e-10));
}

TEST_CASE("DP45 dense output matches the exact solution inside a step", "[integrator]") {
    DormandPrince45 stepper(
        oscillator,
        Tolerances{.relative = 1e-10, .position = 1e-10, .velocity = 1e-10, .mass = 1e-10});
    const State y0{{1.0, 0.0, 0.0}, {}, 0.0};
    // Ask for a large step and let the integrator shrink it until accepted,
    // then look inside that step.
    double h = 1.0;
    auto result = stepper.step(0.0, y0, h);
    while (!result.accepted) {
        h = result.nextStep;
        result = stepper.step(0.0, y0, h);
    }
    for (const double fraction : {0.0, 0.25, 0.5, 0.687, 1.0}) {
        const double t = fraction * h;
        const State y = stepper.interpolate(t);
        REQUIRE_THAT(y.r.x, WithinAbs(std::cos(t), 1e-9));
        REQUIRE_THAT(y.v.x, WithinAbs(-std::sin(t), 1e-9));
    }
}

TEST_CASE("DP45 takes more steps for tighter tolerances", "[integrator]") {
    DormandPrince45 loose(
        oscillator, Tolerances{.relative = 1e-6, .position = 1e-6, .velocity = 1e-6, .mass = 1e-6});
    DormandPrince45 tight(
        oscillator,
        Tolerances{.relative = 1e-12, .position = 1e-12, .velocity = 1e-12, .mass = 1e-12});
    State a{{1.0, 0.0, 0.0}, {}, 0.0};
    State b = a;
    const int looseSteps = integrate(loose, a, 0.0, 20.0);
    const int tightSteps = integrate(tight, b, 0.0, 20.0);

    REQUIRE(tightSteps > 3 * looseSteps);
    // Both should still be within their own tolerance, roughly.
    REQUIRE_THAT(a.r.x, WithinAbs(std::cos(20.0), 1e-4));
    REQUIRE_THAT(b.r.x, WithinAbs(std::cos(20.0), 1e-10));
}

TEST_CASE("DP45 rejects a step that is far too large", "[integrator]") {
    DormandPrince45 stepper(
        oscillator,
        Tolerances{.relative = 1e-12, .position = 1e-12, .velocity = 1e-12, .mass = 1e-12});
    const auto result = stepper.step(0.0, State{{1.0, 0.0, 0.0}, {}, 0.0}, 3.0);
    REQUIRE_FALSE(result.accepted);
    REQUIRE(result.nextStep < 3.0);
}
