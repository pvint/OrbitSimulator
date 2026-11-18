// SPDX-License-Identifier: GPL-3.0-or-later
// The Moon's gravity, lunar events, and flying there.

#include "orbit/core/angles.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/lunar_targeting.hpp"
#include "orbit/core/mission_parameters.hpp"
#include "orbit/core/simulation.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// A mission that only coasts, starting from `start` (relative to the Earth).
SimResult coastFrom(const State& start, double seconds) {
    Mission m = defaultMission();
    m.rocket.stages[0].propellantMass = 0.0;
    SimOptions options;
    options.initialState = start;
    return simulateProgram(m, {{"Coast", false, Steering::Prograde, StepEnd::Duration, seconds}},
                           options);
}

bool hasEvent(const SimResult& r, EventType type) {
    return std::ranges::any_of(r.events, [&](const FlightEvent& e) { return e.type == type; });
}

/// Apollo 11's launch: 1969-07-16 13:32 UTC from Cape Canaveral.
Mission apolloMission() {
    Mission m = defaultMission(); // already Apollo 11's date and time
    m.plan = FlightPlan::LunarOrbit;
    m.rocket.stages[0].isp = 450.0;
    return m;
}

} // namespace

TEST_CASE("A craft in orbit round the Moon stays there", "[moon]") {
    // 3000 km from the Moon's centre, at circular speed relative to it.
    const double epoch = defaultMission().launchEpoch;
    const Vec3 moon = moonPosition(epoch);
    const Vec3 moonV = moonVelocity(epoch);
    const double r = 3.0e6;
    const Vec3 out = normalized(cross(moon, Vec3{0.0, 0.0, 1.0})); // a direction ⟂ to the Moon's
    const Vec3 along = normalized(cross(normalized(moon), out));
    const State start{moon + r * out, moonV + std::sqrt(kMuMoon / r) * along, 1000.0};

    const SimResult result = coastFrom(start, 86400.0);
    REQUIRE(result.outcome == Outcome::LunarOrbit);
    REQUIRE(result.lunar.finalOrbit.has_value());
    const OrbitalElements& el = result.lunar.finalOrbit.value();
    // The Earth's tide stretches the orbit a little over a day, but no more.
    REQUIRE_THAT(el.periapsis, WithinRel(r, 0.1));
    REQUIRE_THAT(el.apoapsis, WithinRel(r, 0.1));
}

TEST_CASE("A craft at rest beside the Moon falls onto it", "[moon]") {
    const double epoch = defaultMission().launchEpoch;
    const Vec3 moon = moonPosition(epoch);
    const double d = 2.0e7; // 20,000 km from the Moon's centre, on the side facing the Earth
    const State start{moon - d * normalized(moon), moonVelocity(epoch), 1000.0};

    const SimResult result = coastFrom(start, 3.0 * 86400.0);
    REQUIRE(result.outcome == Outcome::LunarImpact);
    REQUIRE(result.endReason == EndReason::MoonImpact);
    // Free fall from rest takes (π/2)·√(d³/2μ) towards a point mass; the
    // Earth's pull on this side slows it a little.
    const double freeFall = std::numbers::pi / 2.0 * std::sqrt(d * d * d / (2.0 * kMuMoon));
    REQUIRE(result.duration > 0.9 * freeFall);
    REQUIRE(result.duration < 1.3 * freeFall);
}

TEST_CASE("The lead angle and transfer time are textbook values", "[moon]") {
    const Mission m = apolloMission();
    REQUIRE(radToDeg(leadAngle(m)) > 100.0);
    REQUIRE(radToDeg(leadAngle(m)) < 140.0);
    REQUIRE(lunarTransferTime(m) / 86400.0 > 3.0);
    REQUIRE(lunarTransferTime(m) / 86400.0 < 4.5);
    // The travel angle (to the Moon's arrival point) is a bit under 180°.
    REQUIRE(radToDeg(lunarTravelAngle(m)) > 160.0);
    REQUIRE(radToDeg(lunarTravelAngle(m)) < 180.0);
}

TEST_CASE("Aiming at the Moon reproduces Apollo 11's launch azimuth", "[moon]") {
    // Apollo 11 launched on an azimuth of 72.0°. Our simple model, aiming the
    // parking orbit's plane through the Moon at arrival, lands within a degree.
    const std::optional<double> azimuth = azimuthToMoon(apolloMission());
    REQUIRE(azimuth.has_value());
    REQUIRE_THAT(radToDeg(azimuth.value()), WithinAbs(72.0, 1.0));
}

TEST_CASE("The best launch hour lets the rocket fly due east", "[moon]") {
    Mission m = apolloMission();
    setLaunchHourOfDay(m, bestLaunchHourForMoon(m));
    const std::optional<double> azimuth = azimuthToMoon(m);
    REQUIRE(azimuth.has_value());
    REQUIRE_THAT(radToDeg(azimuth.value()), WithinAbs(90.0, 2.0));
}

TEST_CASE("Apollo 11's launch geometry reaches lunar orbit", "[moon]") {
    Mission m = apolloMission();
    m.guidance.azimuth = azimuthToMoon(m).value();
    m.lunar.leadAngleAdjust = degToRad(2.0); // arrive beside the Moon, not at its centre
    const SimResult r = simulate(m);

    REQUIRE(r.outcome == Outcome::LunarOrbit);
    REQUIRE(r.lunar.enteredSphereOfInfluence);
    REQUIRE(hasEvent(r, EventType::EnterMoonSphere));
    REQUIRE(hasEvent(r, EventType::MoonClosestApproach));
    REQUIRE(std::ranges::any_of(r.events, [](const FlightEvent& e) {
        return e.description == "Lunar orbit insertion: engine off";
    }));
    // About three and a half days out, like the real thing.
    REQUIRE(r.lunar.closestApproachTime / 86400.0 > 3.0);
    REQUIRE(r.lunar.closestApproachTime / 86400.0 < 4.0);
    REQUIRE(r.lunar.finalOrbit->periapsisAltitude > 0.0);
    REQUIRE(r.lunar.finalOrbit->apoapsisAltitude < 12.0e6);
}

TEST_CASE("Aiming straight at the Moon hits it", "[moon]") {
    Mission m = apolloMission();
    m.plan = FlightPlan::LunarFlyby;
    m.guidance.azimuth = azimuthToMoon(m).value();
    const SimResult r = simulate(m);
    REQUIRE(r.outcome == Outcome::LunarImpact);
}

TEST_CASE("Without aiming, a lunar flight misses and skips the capture burn", "[moon]") {
    Mission m = apolloMission(); // due east at 13:32: the Moon is far out of plane
    const SimResult r = simulate(m);
    REQUIRE_FALSE(r.lunar.enteredSphereOfInfluence);
    REQUIRE(r.lunar.closestApproachAltitude > kMoonSphereOfInfluence);
    REQUIRE_FALSE(std::ranges::any_of(
        r.events, [](const FlightEvent& e) { return e.description.starts_with("Lunar orbit"); }));
    REQUIRE(r.endReason == EndReason::ProgramDone);
}

TEST_CASE("Flight plans build the expected programs", "[moon]") {
    const auto names = [](FlightPlan plan) {
        Mission m = defaultMission();
        m.plan = plan;
        std::vector<std::string> out;
        for (const FlightStep& s : flightProgramFor(m)) out.push_back(s.name);
        return out;
    };
    using V = std::vector<std::string>;
    REQUIRE(names(FlightPlan::Orbit) ==
            V{"Ascent", "Coast to apoapsis", "Circularization", "Coast"});
    REQUIRE(names(FlightPlan::BurnToDepletion) == V{"Ascent", "Coast"});
    REQUIRE(names(FlightPlan::LunarOrbit) == V{"Ascent", "Coast to apoapsis", "Circularization",
                                               "Parking orbit", "Trans-lunar injection",
                                               "Coast to the Moon", "Lunar orbit insertion",
                                               "Lunar orbit"});
}
