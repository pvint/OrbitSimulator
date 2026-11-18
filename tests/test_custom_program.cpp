// SPDX-License-Identifier: GPL-3.0-or-later
// Custom flight programs (the maneuver editor): new steering modes and end
// conditions, scenario files, and sweeping a step's value.

#include "orbit/core/angles.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/orbit.hpp"
#include "orbit/core/scenario.hpp"
#include "orbit/core/simulation.hpp"
#include "orbit/core/sweep.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>

using namespace orbit;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// The default rocket with a custom program: the Orbit plan's climb to a
/// 200 km circular orbit, then `more`. Isp 450 gives Δv to spare.
Mission customMission(const FlightProgram& more) {
    Mission m = defaultMission();
    m.rocket.stages[0].isp = 450.0;
    FlightProgram program = flightProgramFor(m);
    program.pop_back(); // the plan's final one-revolution coast
    program.insert(program.end(), more.begin(), more.end());
    m.plan = FlightPlan::Custom;
    m.program = program;
    return m;
}

const FlightEvent* findEvent(const SimResult& r, const std::string& description) {
    const auto it = std::ranges::find(r.events, description, &FlightEvent::description);
    return it == r.events.end() ? nullptr : &*it;
}

FlightStep burn(std::string name, Steering steering, StepEnd end, double value = 0.0) {
    return {.name = std::move(name),
            .engineOn = true,
            .steering = steering,
            .end = end,
            .value = value};
}

FlightStep coast(std::string name, StepEnd end, double value = 0.0) {
    return {.name = std::move(name), .end = end, .value = value};
}

} // namespace

TEST_CASE("A custom copy of a plan's program flies the same", "[program]") {
    Mission plan = defaultMission();
    Mission custom = plan;
    custom.plan = FlightPlan::Custom;
    custom.program = flightProgramFor(plan);
    REQUIRE(flightProgramFor(custom).size() == custom.program.size());

    const SimResult a = simulate(plan);
    const SimResult b = simulate(custom);
    REQUIRE(a.duration == b.duration);
    REQUIRE(a.orbit.periapsis == b.orbit.periapsis);
}

TEST_CASE("A Hohmann transfer from 200 to 1000 km costs what the textbook says", "[program]") {
    // Raise the apoapsis, coast half an orbit to it, circularize there.
    const Mission m = customMission({
        burn("Raise apoapsis", Steering::Prograde, StepEnd::ApoapsisAltitude, 1000e3),
        coast("Transfer", StepEnd::Apoapsis),
        burn("Circularize high", Steering::Circularize, StepEnd::Circular),
        coast("Coast", StepEnd::Revolutions, 1.0),
    });
    const SimResult r = simulate(m);
    REQUIRE(r.outcome == Outcome::Orbit);
    REQUIRE_THAT(r.orbit.periapsisAltitude, WithinAbs(1000e3, 5e3));
    REQUIRE_THAT(r.orbit.apoapsisAltitude, WithinAbs(1000e3, 5e3));

    // Δv1 = √(μ/r1)(√(2r2/(r1+r2)) − 1), Δv2 = √(μ/r2)(1 − √(2r1/(r1+r2)))
    const double r1 = kRadiusEarth + 200e3;
    const double r2 = kRadiusEarth + 1000e3;
    const double dv1 = std::sqrt(kMuEarth / r1) * (std::sqrt(2.0 * r2 / (r1 + r2)) - 1.0);
    const double dv2 = std::sqrt(kMuEarth / r2) * (1.0 - std::sqrt(2.0 * r1 / (r1 + r2)));
    REQUIRE_THAT(dv1 + dv2, WithinAbs(434.0, 1.0));

    // The speed the two burns added, from the rocket equation over their mass.
    const FlightEvent* start = findEvent(r, "Raise apoapsis: engine on");
    const FlightEvent* end = findEvent(r, "Circularize high: engine off");
    REQUIRE(start != nullptr);
    REQUIRE(end != nullptr);
    const double ve = exhaustVelocity(450.0);
    const FlightEvent* firstOff = findEvent(r, "Raise apoapsis: engine off");
    const FlightEvent* secondOn = findEvent(r, "Circularize high: engine on");
    const double spent = ve * std::log(start->sample.state.m / firstOff->sample.state.m) +
                         ve * std::log(secondOn->sample.state.m / end->sample.state.m);
    // Short burns: within a percent of the impulsive ideal.
    REQUIRE_THAT(spent, WithinRel(dv1 + dv2, 0.01));
}

TEST_CASE("A deorbit burn lowers the periapsis into the air and brings it down", "[program]") {
    // Burn retrograde until the periapsis is down to 50 km (a "reaches"
    // condition going down), coast until the altitude falls to 100 km, then
    // let the air do the rest.
    const Mission m = customMission({
        burn("Deorbit burn", Steering::Retrograde, StepEnd::PeriapsisAltitude, 50e3),
        coast("Fall to 100 km", StepEnd::Altitude, 100e3),
        burn("Brake", Steering::Retrograde, StepEnd::Duration, 1.0),
        coast("Re-entry", StepEnd::Duration, 3600.0),
    });
    const SimResult r = simulate(m);
    REQUIRE(r.endReason == EndReason::Impact);

    const FlightEvent* off = findEvent(r, "Deorbit burn: engine off");
    REQUIRE(off != nullptr);
    const State& s = off->sample.state;
    REQUIRE_THAT(elementsFromState(s.r, s.v).periapsisAltitude, WithinAbs(50e3, 100.0));
    // The Δv for that: from 7.79 km/s circular to the apoapsis speed of a
    // 200 × 50 km ellipse, about 44 m/s.
    const double ra = kRadiusEarth + 200e3;
    const double rp = kRadiusEarth + 50e3;
    const double needed =
        std::sqrt(kMuEarth / ra) - std::sqrt(kMuEarth * 2.0 * rp / (ra * (ra + rp)));
    REQUIRE_THAT(needed, WithinAbs(44.0, 2.0));

    // The coast ended at exactly 100 km on the way down, where the brake fired.
    const FlightEvent* brake = findEvent(r, "Brake: engine on");
    REQUIRE(brake != nullptr);
    REQUIRE_THAT(brake->sample.altitude(), WithinAbs(100e3, 1.0));
    REQUIRE(dot(brake->sample.state.r, brake->sample.state.v) < 0.0);
}

TEST_CASE("Burning normal or anti-normal turns the orbit's plane", "[program]") {
    // A sideways Δv turns the velocity, and with it the plane, by Δv / v.
    const auto planeTurn = [](Steering steering) {
        const SimResult r = simulate(customMission({
            burn("Plane change", steering, StepEnd::Duration, 5.0),
            coast("Coast", StepEnd::Duration, 600.0),
        }));
        const Sample& on = findEvent(r, "Plane change: engine on")->sample;
        const Sample& off = findEvent(r, "Plane change: engine off")->sample;
        const Vec3 h0 = normalized(cross(on.state.r, on.state.v));
        const Vec3 h1 = normalized(cross(off.state.r, off.state.v));
        const double deltaV = exhaustVelocity(450.0) * std::log(on.state.m / off.state.m);
        return std::pair{h1 - h0, deltaV / on.speed()};
    };
    const auto [up, expectedUp] = planeTurn(Steering::Normal);
    const auto [down, expectedDown] = planeTurn(Steering::Antinormal);
    REQUIRE(expectedUp > degToRad(1.0));
    REQUIRE_THAT(norm(up), WithinRel(expectedUp, 0.01));
    REQUIRE_THAT(norm(down), WithinRel(expectedDown, 0.01));
    REQUIRE(dot(up, down) < 0.0); // opposite ways
}

TEST_CASE("Radial burns change the shape, not the energy much", "[program]") {
    const SimResult r = simulate(customMission({
        burn("Radial out", Steering::RadialOut, StepEnd::Duration, 2.0),
        coast("Coast", StepEnd::Revolutions, 1.0),
    }));
    // A radial kick raises the orbit ahead and lowers it behind: apoapsis
    // up, periapsis down by about as much.
    const double rise = r.orbit.apoapsisAltitude - 200e3;
    const double fall = 200e3 - r.orbit.periapsisAltitude;
    REQUIRE(rise > 10e3);
    REQUIRE_THAT(fall, WithinRel(rise, 0.1));
}

TEST_CASE("Custom programs survive a scenario file, in display units", "[program][scenario]") {
    Scenario s;
    s.name = "Deorbit";
    s.mission = customMission({
        burn("Deorbit burn", Steering::Retrograde, StepEnd::PeriapsisAltitude, 50e3),
        coast("Wait", StepEnd::Duration, 1234.0),
    });
    s.mission.program.back().timeout = 5000.0;
    const std::string json = scenarioToJson(s);
    REQUIRE_THAT(json, ContainsSubstring(R"("plan": "custom")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("until": "periapsis-altitude")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("value": 50)")); // km
    REQUIRE_THAT(json, ContainsSubstring(R"("steering": "retrograde")"));

    const LoadedScenario back = scenarioFromJson(json);
    REQUIRE(back.warnings.empty());
    const FlightProgram& p = back.scenario.mission.program;
    REQUIRE(p.size() == s.mission.program.size());
    for (std::size_t i = 0; i < p.size(); ++i) {
        CAPTURE(i);
        REQUIRE(p[i].name == s.mission.program[i].name);
        REQUIRE(p[i].engineOn == s.mission.program[i].engineOn);
        REQUIRE(p[i].steering == s.mission.program[i].steering);
        REQUIRE(p[i].end == s.mission.program[i].end);
        REQUIRE_THAT(p[i].value, WithinAbs(s.mission.program[i].value, 1e-6));
        REQUIRE(p[i].timeout == s.mission.program[i].timeout);
        REQUIRE_THAT(p[i].lookAhead, WithinAbs(s.mission.program[i].lookAhead, 1e-6));
    }

    REQUIRE_THROWS_WITH(scenarioFromJson(R"({"format": "orbitsim-scenario", "version": 3,
        "mission": {"plan": "custom", "program": [{"until": "lunch"}]}})"),
                        ContainsSubstring("program[0].until"));
}

TEST_CASE("Sweeping a coast's length sweeps when the next burn starts", "[program][sweep]") {
    const Mission m = customMission({
        coast("Wait", StepEnd::Duration, 600.0),
        burn("Kick", Steering::Prograde, StepEnd::Duration, 10.0),
        coast("Coast", StepEnd::Duration, 600.0),
    });
    // "Wait" is step 4: Ascent, Coast to apoapsis, Circularization, Wait.
    const auto target = findSweepTarget(m, "step-4-value");
    REQUIRE(target.has_value());
    REQUIRE(target->title == "Step 4 (Wait): Time");
    REQUIRE(target->unit == "s");
    // Steps without a value (at apoapsis, circular) can't be swept.
    REQUIRE_FALSE(findSweepTarget(m, "step-2-value").has_value());
    // Plans other than Custom have no step targets.
    REQUIRE_FALSE(findSweepTarget(defaultMission(), "step-4-value").has_value());

    const auto missions = sweepMissions(m, {"step-4-value", 0.0, 1200.0, 3});
    REQUIRE(missions.size() == 3);
    std::vector<double> starts;
    for (const Mission& mission : missions) {
        const SimResult r = simulate(mission);
        starts.push_back(findEvent(r, "Kick: engine on")->sample.t);
    }
    REQUIRE_THAT(starts[1] - starts[0], WithinAbs(600.0, 1e-6));
    REQUIRE_THAT(starts[2] - starts[1], WithinAbs(600.0, 1e-6));

    // And a sweep of it survives a scenario file.
    Scenario s;
    s.mission = m;
    s.sweep = SweepSpec{"step-4-value", 0.0, 1200.0, 3};
    REQUIRE(scenarioFromJson(scenarioToJson(s)).scenario.sweep->parameter == "step-4-value");
}

TEST_CASE("A step that takes no time is skipped, not stalled on", "[program]") {
    // A 0 s wait used to make the integrator take a zero-length step, whose
    // zero "next step size" stalled the flight until the step limit.
    const Mission m = customMission({
        coast("Wait", StepEnd::Duration, 0.0),
        burn("Kick", Steering::Prograde, StepEnd::Duration, 5.0),
        coast("Coast", StepEnd::Revolutions, 0.0),
        coast("Coast", StepEnd::Duration, 60.0),
    });
    const SimResult r = simulate(m);
    REQUIRE(r.endReason == EndReason::ProgramDone);
    const FlightEvent* kick = findEvent(r, "Kick: engine on");
    REQUIRE(kick != nullptr);
    REQUIRE(findEvent(r, "Kick: engine off") != nullptr);
    REQUIRE_THAT(r.duration - kick->sample.t, WithinAbs(65.0, 1e-6));
}
