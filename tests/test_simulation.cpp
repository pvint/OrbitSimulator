// SPDX-License-Identifier: GPL-3.0-or-later
// Whole flights: the prototype's checks in 3D, plus launch geometry.

#include "orbit/core/angles.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/simulation.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

const FlightEvent* findEvent(const SimResult& r, EventType type) {
    for (const auto& e : r.events) {
        if (e.type == type) return &e;
    }
    return nullptr;
}

/// Angle difference wrapped into (−180°, 180°].
double angleDiffDeg(double a, double b) {
    double d = std::fmod(radToDeg(a - b), 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}

} // namespace

TEST_CASE("The default rocket reaches a low, circular orbit", "[simulation]") {
    const SimResult r = simulate(defaultMission());

    REQUIRE(r.outcome == Outcome::Orbit);
    REQUIRE(r.endReason == EndReason::ProgramDone);
    REQUIRE(r.orbit.periapsisAltitude > 190e3);
    REQUIRE(r.orbit.apoapsisAltitude < 210e3);
    REQUIRE(r.orbit.eccentricity < 1e-3);

    // Cutoff, coast up, circularize: in that order.
    REQUIRE(r.events.size() == 3);
    REQUIRE(r.events[0].type == EventType::EngineStop);  // ascent: apoapsis at target
    REQUIRE(r.events[1].type == EventType::EngineStart); // circularization
    REQUIRE(r.events[2].type == EventType::EngineStop);
    REQUIRE(r.events[0].description == "Ascent: engine off");
    REQUIRE(r.events[1].description == "Circularization: engine on");
    REQUIRE(r.events[0].sample.t < r.events[1].sample.t);
    // Circularization starts at apoapsis = the target altitude.
    REQUIRE_THAT(r.events[1].sample.altitude(), WithinAbs(200e3, 1.0));
}

TEST_CASE("Samples are in time order and the orbit closes", "[simulation]") {
    const SimResult r = simulate(defaultMission());
    for (std::size_t i = 1; i < r.samples.size(); ++i) {
        REQUIRE(r.samples[i].t >= r.samples[i - 1].t);
    }
    // After exactly one revolution, the rocket is back where the engine stopped.
    // (The last engine event is the final shutdown, the same moment as r.cutoff.)
    REQUIRE_FALSE(r.events.empty());
    const Sample& cutoff = r.events.back().sample;
    const Vec3 end = r.samples.back().state.r;
    REQUIRE(norm(end - cutoff.state.r) < 10.0);
    REQUIRE_THAT(r.duration - cutoff.t, WithinRel(r.orbit.period, 1e-9));
}

TEST_CASE("Simulated burn matches the Tsiolkovsky rocket equation", "[simulation]") {
    // No gravity, no ground velocity (launched from the pole) and no pitch-over,
    // so all of the Δv goes into speed.
    Mission m = defaultMission();
    m.site.latitude = degToRad(90.0);
    m.guidance.pitchStartTime = 1e9;
    m.plan = FlightPlan::BurnToDepletion;
    SimOptions noGravity;
    noGravity.mu = 0.0;
    noGravity.thirdBodyGravity = false;
    const SimResult r = simulate(m, noGravity);

    const FlightEvent* burnout = findEvent(r, EventType::Burnout);
    REQUIRE(burnout != nullptr);
    const double m0 = m.rocket.dryMass + m.rocket.propellantMass;
    const double expected = m.rocket.isp * kG0 * std::log(m0 / m.rocket.dryMass);

    REQUIRE_THAT(idealDeltaV(m.rocket), WithinRel(expected, 1e-15));
    REQUIRE_THAT(burnout->sample.speed(), WithinRel(expected, 1e-9));
    REQUIRE_THAT(burnout->sample.t, WithinRel(burnTime(m.rocket), 1e-12));
    REQUIRE_THAT(r.deltaVUsed, WithinRel(expected, 1e-12));
}

TEST_CASE("Launching east from the equator saves about 930 m/s over west", "[simulation]") {
    Mission base = defaultMission();
    base.site.latitude = 0.0;
    base.rocket.isp = 420.0; // enough Δv for the westward rocket to reach orbit too

    Mission east = base;
    east.guidance.azimuth = degToRad(90.0);
    Mission west = base;
    west.guidance.azimuth = degToRad(270.0);
    const SimResult e = simulate(east);
    const SimResult w = simulate(west);

    // On the pad the ground already moves east at ω·R.
    REQUIRE_THAT(e.samples.front().speed(), WithinAbs(kOmegaEarth * kRadiusEarth, 1e-6));

    REQUIRE(e.outcome == Outcome::Orbit);
    REQUIRE(w.outcome == Outcome::Orbit);
    // Westward orbits are retrograde.
    REQUIRE_THAT(radToDeg(e.orbit.inclination), WithinAbs(0.0, 0.01));
    REQUIRE_THAT(radToDeg(w.orbit.inclination), WithinAbs(180.0, 0.01));
    // Going west costs about 2·ω·R more (not exactly: the two climbs differ).
    const double extra = w.deltaVUsed - e.deltaVUsed;
    REQUIRE_THAT(extra, WithinAbs(2.0 * kOmegaEarth * kRadiusEarth, 100.0));
}

TEST_CASE("A due-east launch gives an inclination equal to the latitude", "[simulation]") {
    const double latDeg = GENERATE(0.0, 5.2, 28.5, 45.6, 62.9);
    CAPTURE(latDeg);
    Mission m = defaultMission();
    m.site.latitude = degToRad(latDeg);
    const SimResult r = simulate(m);

    REQUIRE(r.outcome == Outcome::Orbit);
    REQUIRE_THAT(radToDeg(r.orbit.inclination), WithinAbs(latDeg, 0.05));
}

TEST_CASE("Other azimuths give the expected inclinations", "[simulation]") {
    Mission m = defaultMission();

    SECTION("Due north from the pole is exactly polar") {
        m.site.latitude = degToRad(90.0);
        m.guidance.azimuth = 0.0;
        const SimResult r = simulate(m);
        REQUIRE(r.outcome == Outcome::Orbit);
        REQUIRE_THAT(radToDeg(r.orbit.inclination), WithinAbs(90.0, 0.01));
    }
    SECTION("Due north from Florida is slightly prograde of polar") {
        // The ground's eastward speed tilts the plane: i a few degrees below 90.
        m.guidance.azimuth = 0.0;
        const SimResult r = simulate(m);
        REQUIRE(r.outcome == Outcome::Orbit);
        const double i = radToDeg(r.orbit.inclination);
        REQUIRE(i > 85.0);
        REQUIRE(i < 89.0);
    }
    SECTION("Azimuth 45° from 28.5°N: close to spherical trigonometry") {
        // Without Earth's rotation: cos i = cos(lat) · sin(azimuth) → i = 51.6°.
        // The eastward boost turns the real heading a little east, lowering i.
        m.guidance.azimuth = degToRad(45.0);
        const SimResult r = simulate(m);
        REQUIRE(r.outcome == Outcome::Orbit);
        const double i = radToDeg(r.orbit.inclination);
        const double iNoRotation =
            radToDeg(std::acos(std::cos(degToRad(28.5)) * std::sin(degToRad(45.0))));
        REQUIRE(i < iNoRotation);
        REQUIRE(i > iNoRotation - 4.0);
    }
}

TEST_CASE("Moving the launch site in longitude only turns the orbit", "[simulation]") {
    Mission a = defaultMission();
    Mission b = a;
    b.site.longitude = a.site.longitude + degToRad(100.0);
    // Exact only with the Earth alone: the Moon and Sun break the symmetry
    // (very slightly) because they sit in different directions from each site.
    SimOptions earthOnly;
    earthOnly.thirdBodyGravity = false;
    const SimResult ra = simulate(a, earthOnly);
    const SimResult rb = simulate(b, earthOnly);

    REQUIRE_THAT(rb.orbit.periapsis, WithinAbs(ra.orbit.periapsis, 1.0));
    REQUIRE_THAT(rb.orbit.apoapsis, WithinAbs(ra.orbit.apoapsis, 1.0));
    REQUIRE_THAT(rb.orbit.inclination, WithinAbs(ra.orbit.inclination, 1e-9));
    REQUIRE_THAT(angleDiffDeg(rb.orbit.raan, ra.orbit.raan), WithinAbs(100.0, 1e-6));
}

TEST_CASE("Obvious failures are classified correctly", "[simulation]") {
    Mission m = defaultMission();

    SECTION("Thrust below weight never leaves the pad") {
        m.rocket.thrust = 100e3;
        const SimResult r = simulate(m);
        REQUIRE(r.outcome == Outcome::Crashed);
        REQUIRE(r.endReason == EndReason::Impact);
        REQUIRE(r.duration < 1.0);
    }
    SECTION("A big kick turns the rocket into the ground") {
        m.guidance.kickAngle = degToRad(45.0);
        REQUIRE(simulate(m).outcome == Outcome::Crashed);
    }
    SECTION("Straight up with little propellant: a hop into space and back") {
        m.rocket.propellantMass = 10e3;
        m.guidance.kickAngle = 0.0;
        m.plan = FlightPlan::BurnToDepletion;
        const SimResult r = simulate(m);
        REQUIRE(r.outcome == Outcome::Suborbital);
        REQUIRE(r.maxAltitude > kKarmanLine);
    }
    SECTION("A huge Isp escapes") {
        m.rocket.isp = 1000.0;
        m.guidance.kickAngle = degToRad(5.0);
        m.plan = FlightPlan::BurnToDepletion;
        const SimResult r = simulate(m);
        REQUIRE(r.outcome == Outcome::Escape);
        REQUIRE(r.endReason == EndReason::EscapeDistance);
    }
    SECTION("No propellant at all") {
        m.rocket.propellantMass = 0.0;
        REQUIRE(simulate(m).outcome == Outcome::Crashed);
    }
}

TEST_CASE("Running out of propellant while circularizing falls short of orbit", "[simulation]") {
    Mission m = defaultMission();
    m.rocket.propellantMass = 50e3; // not quite enough (52 t just makes it)
    const SimResult r = simulate(m);

    const FlightEvent* start = findEvent(r, EventType::EngineStart);
    REQUIRE(start != nullptr);
    REQUIRE(start->description == "Circularization: engine on");
    REQUIRE(findEvent(r, EventType::Burnout) != nullptr);
    // The circularization never finished: one engine stop (the ascent), not two.
    REQUIRE(std::ranges::count(r.events, EventType::EngineStop, &FlightEvent::type) == 1);
    REQUIRE(r.propellantLeft == 0.0);
    // The burn raised the periapsis, but not out of the ground.
    REQUIRE(r.outcome == Outcome::Suborbital);
    REQUIRE(r.orbit.periapsisAltitude < 0.0);
}

TEST_CASE("stateAt interpolates between samples and clamps at the ends", "[simulation]") {
    const SimResult r = simulate(defaultMission());
    const Sample& first = r.samples.front();
    const Sample& last = r.samples.back();

    REQUIRE(norm(stateAt(r, -10.0).r - first.state.r) == 0.0);
    REQUIRE(norm(stateAt(r, last.t + 100.0).r - last.state.r) == 0.0);

    // On a sample it returns that sample; between two it lies between them.
    const Sample& a = r.samples[100];
    const Sample& b = r.samples[101];
    REQUIRE(norm(stateAt(r, a.t).r - a.state.r) < 1e-6);
    const State mid = stateAt(r, (a.t + b.t) / 2.0);
    REQUIRE(norm(mid.r - 0.5 * (a.state.r + b.state.r)) < 1e-6);
    REQUIRE_THAT(mid.m, WithinAbs(0.5 * (a.state.m + b.state.m), 1e-9));
}
