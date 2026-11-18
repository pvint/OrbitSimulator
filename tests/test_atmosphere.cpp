// SPDX-License-Identifier: GPL-3.0-or-later
// The atmosphere, drag, and what drag does to flights.

#include "orbit/core/aerodynamics.hpp"
#include "orbit/core/angles.hpp"
#include "orbit/core/atmosphere.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/earth.hpp"
#include "orbit/core/simulation.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// A lump with no engine, for dropping through the air or leaving in orbit.
Mission unpoweredMission(double mass, double dragCoefficient, double frontalArea) {
    Mission m = defaultMission();
    m.rocket = {.stages = {Stage{.dryMass = mass, .isp = 300.0}},
                .dragCoefficient = dragCoefficient,
                .diameter = std::sqrt(4.0 * frontalArea / kPi)};
    return m;
}

/// Fly a lump from a given state, coasting for one step.
SimResult coastFrom(const Mission& m, const State& start, const FlightStep& step) {
    SimOptions options;
    options.thirdBodyGravity = false;
    options.initialState = start;
    return simulateProgram(m, {step}, options);
}

} // namespace

TEST_CASE("The atmosphere matches the 1976 US Standard Atmosphere tables", "[atmosphere]") {
    struct Row {
        double altitudeKm, temperature, pressure, density, speedOfSound;
    };
    // From the published tables (geometric altitude). The speed of sound is
    // only listed where we check it.
    const std::array rows{
        Row{0.0, 288.15, 101325.0, 1.2250, 340.29},   Row{10.0, 223.252, 26499.9, 0.41351, 299.53},
        Row{20.0, 216.65, 5529.3, 0.088910, 295.07},  Row{30.0, 226.509, 1197.0, 0.018410, 0.0},
        Row{50.0, 270.65, 79.779, 1.0269e-3, 329.80}, Row{80.0, 198.639, 1.0524, 1.8458e-5, 0.0},
        Row{86.0, 186.87, 0.37338, 6.958e-6, 0.0},
    };
    for (const Row& row : rows) {
        CAPTURE(row.altitudeKm);
        const Air air = atmosphereAt(row.altitudeKm * 1000.0);
        REQUIRE_THAT(air.temperature, WithinRel(row.temperature, 1e-3));
        REQUIRE_THAT(air.pressure, WithinRel(row.pressure, 2e-3));
        REQUIRE_THAT(air.density, WithinRel(row.density, 2e-3));
        if (row.speedOfSound > 0.0) {
            REQUIRE_THAT(air.speedOfSound, WithinRel(row.speedOfSound, 1e-3));
        }
    }
}

TEST_CASE("Pressure and density are continuous across the layers", "[atmosphere]") {
    // Layer bases in geopotential km; the geometric altitude is r0·H/(r0 − H).
    const double r0 = 6356.766;
    for (const double baseKm : {11.0, 20.0, 32.0, 47.0, 51.0, 71.0}) {
        CAPTURE(baseKm);
        const double z = 1000.0 * r0 * baseKm / (r0 - baseKm);
        const Air below = atmosphereAt(z - 0.01);
        const Air above = atmosphereAt(z + 0.01);
        REQUIRE_THAT(above.pressure, WithinRel(below.pressure, 1e-5));
        REQUIRE_THAT(above.temperature, WithinAbs(below.temperature, 1e-3));
    }
    // Where the standard hands over to the upper table.
    REQUIRE_THAT(atmosphereAt(86e3 + 0.01).density,
                 WithinRel(atmosphereAt(86e3 - 0.01).density, 1e-5));
}

TEST_CASE("Density falls all the way up and stops at 1000 km", "[atmosphere]") {
    double previous = atmosphereAt(0.0).density;
    for (int metres = 500; metres <= 1'000'000; metres += 500) {
        const double z = metres;
        CAPTURE(z);
        const double density = atmosphereAt(z).density;
        REQUIRE(density < previous);
        REQUIRE(density > 0.0);
        previous = density;
    }
    // The upper table's own values (Vallado, table 8-4).
    REQUIRE_THAT(atmosphereAt(200e3).density, WithinRel(2.789e-10, 1e-9));
    REQUIRE_THAT(atmosphereAt(400e3).density, WithinRel(3.725e-12, 1e-9));
    REQUIRE_THAT(atmosphereAt(1000e3).density, WithinRel(3.019e-15, 1e-9));
    // Scale height near 300 km: the density falls by e in about 50 km.
    const double h = 1000.0 / std::log(atmosphereAt(300e3).density / atmosphereAt(301e3).density);
    REQUIRE_THAT(h, WithinRel(53e3, 0.1));

    const Air space = atmosphereAt(1001e3);
    REQUIRE(space.density == 0.0);
    REQUIRE(space.pressure == 0.0);
}

TEST_CASE("Drag rises through the sound barrier, then eases off", "[aerodynamics]") {
    REQUIRE(machDragFactor(0.0) == 1.0);
    REQUIRE(machDragFactor(0.8) == 1.0);
    REQUIRE_THAT(machDragFactor(1.2), WithinAbs(1.8, 1e-12));
    REQUIRE_THAT(machDragFactor(1.1), WithinAbs(1.7, 1e-12)); // halfway from 1.6 to 1.8
    REQUIRE(machDragFactor(5.0) == 1.0);
    REQUIRE(machDragFactor(25.0) == 1.0);
    // Continuous: no jumps anywhere.
    for (int i = 0; i < 6000; ++i) {
        const double mach = i * 0.001;
        REQUIRE_THAT(machDragFactor(mach + 0.001), WithinAbs(machDragFactor(mach), 0.004));
    }
}

TEST_CASE("Drag acts against the velocity through the air, which turns with the Earth",
          "[aerodynamics]") {
    Rocket rocket = defaultMission().rocket;
    const Vec3 pad = positionFromLatLon(0.0, 0.0, kRadiusEarth);

    SECTION("On the pad the air and the rocket move together: no drag") {
        const Aerodynamics a = aerodynamicsAt(rocket, pad, groundVelocity(pad));
        REQUIRE(norm(a.drag) == 0.0);
        REQUIRE(a.dynamicPressure == 0.0);
        REQUIRE(a.mach == 0.0);
    }
    SECTION("Standing still in space, the equator's 465 m/s wind blows east") {
        const Aerodynamics a = aerodynamicsAt(rocket, pad, Vec3{});
        const double wind = kOmegaEarth * kRadiusEarth;
        REQUIRE_THAT(a.mach, WithinRel(wind / 340.29, 1e-3));
        REQUIRE_THAT(a.dynamicPressure, WithinRel(0.5 * 1.225 * wind * wind, 1e-3));
        // D = q · Cd(M) · A, pushing east (the way the air moves).
        const double expected = a.dynamicPressure * rocket.dragCoefficient *
                                machDragFactor(a.mach) * frontalArea(rocket);
        REQUIRE_THAT(norm(a.drag), WithinRel(expected, 1e-12));
        REQUIRE_THAT(dot(normalized(a.drag), normalized(groundVelocity(pad))),
                     WithinAbs(1.0, 1e-12));
    }
    SECTION("A rocket with Cd = 0 feels nothing") {
        rocket.dragCoefficient = 0.0;
        REQUIRE_FALSE(hasDrag(rocket));
        REQUIRE(norm(aerodynamicsAt(rocket, pad, Vec3{}).drag) == 0.0);
    }
    SECTION("Above the atmosphere there is no drag") {
        const Vec3 high = positionFromLatLon(0.0, 0.0, kRadiusEarth + 1100e3);
        REQUIRE(norm(aerodynamicsAt(rocket, high, Vec3{}).drag) == 0.0);
    }
}

TEST_CASE("A falling object settles at its terminal velocity", "[aerodynamics]") {
    // Dropped from 3 km above the north pole (no wind there: the ground
    // doesn't move at the pole). It speeds up until drag balances weight:
    //     m·g = ½ ρ v² Cd A   →   v_t = √(2 m g / (ρ Cd A))
    // With 100 kg, Cd 0.5 and A 0.2 m², about 127 m/s at sea level, well
    // below Mach 0.8, so Cd stays at 0.5.
    const double mass = 100.0;
    const double cd = 0.5;
    const double area = 0.2;
    const Mission m = unpoweredMission(mass, cd, area);
    const Vec3 start = positionFromLatLon(degToRad(90.0), 0.0, kRadiusEarth + 3000.0);
    const SimResult r = coastFrom(m, {start, Vec3{}, mass},
                                  {.name = "Fall", .end = StepEnd::Duration, .value = 600.0});

    REQUIRE(r.endReason == EndReason::Impact);
    const double g = kMuEarth / (kRadiusEarth * kRadiusEarth);
    const double terminal = std::sqrt(2.0 * mass * g / (1.225 * cd * area));
    // It lags a little behind as the air thickens on the way down.
    REQUIRE_THAT(r.samples.back().speed(), WithinRel(terminal, 0.02));
    REQUIRE(r.samples.back().speed() > terminal);
}

TEST_CASE("Drag slowly lowers a low orbit", "[aerodynamics]") {
    // A satellite of 1000 kg, Cd 2.2 (typical for a box in thin air) and 1 m²
    // in a circular polar orbit. Each revolution the semi-major axis shrinks by
    //     Δa = −2π (Cd·A/m) ρ a²
    // (from the energy drag removes: its work per orbit is D · 2πa, and the
    // orbit's energy is −μ/2a).
    const Mission m = unpoweredMission(1000.0, 2.2, 1.0);
    const double ballistic = 2.2 * 1.0 / 1000.0;
    const FlightStep oneOrbit{.name = "Coast", .end = StepEnd::Revolutions, .value = 1.0};

    const auto decayPerOrbit = [&](double altitude) {
        const double a = kRadiusEarth + altitude;
        const Vec3 r = positionFromLatLon(0.0, 0.0, a);
        const Vec3 v{0.0, 0.0, circularSpeed(a)}; // due north: a polar orbit
        const SimResult result = coastFrom(m, {r, v, 1000.0}, oneOrbit);
        REQUIRE(result.endReason == EndReason::ProgramDone);
        const State& end = result.samples.back().state;
        return elementsFromState(end.r, end.v).semiMajorAxis - a;
    };

    SECTION("At 150 km: over a kilometre per orbit, as the formula says") {
        const double a = kRadiusEarth + 150e3;
        const double expected = -2.0 * kPi * ballistic * atmosphereAt(150e3).density * a * a;
        REQUIRE(expected < -1000.0);
        // A little more than the formula: the orbit sinks into denser air.
        const double decay = decayPerOrbit(150e3);
        REQUIRE_THAT(decay, WithinRel(expected, 0.06));
        REQUIRE(decay < expected);
    }
    SECTION("At 600 km: about 10 cm") { REQUIRE_THAT(decayPerOrbit(600e3), WithinAbs(-0.1, 0.05)); }
}

TEST_CASE("Drag costs the default rocket some delta-v and gives it a max-Q", "[simulation]") {
    const SimResult r = simulate(defaultMission());
    REQUIRE(r.outcome == Outcome::Orbit);
    REQUIRE(r.maxDynamicPressure.has_value());

    // Max-Q comes about a minute after liftoff, around 10 km up, just past
    // the sound barrier. This rocket pushes harder than most (liftoff T/W
    // 1.9 against a typical 1.3), so its max-Q is higher than the usual 30-35 kPa.
    const Sample& maxQ = *r.maxDynamicPressure;
    const Aerodynamics aero = aerodynamicsAt(r, maxQ);
    REQUIRE(aero.dynamicPressure > 30e3);
    REQUIRE(aero.dynamicPressure < 100e3);
    REQUIRE(maxQ.t > 30.0);
    REQUIRE(maxQ.t < 90.0);
    REQUIRE(maxQ.altitude() > 5e3);
    REQUIRE(maxQ.altitude() < 20e3);
    REQUIRE(aero.mach > 1.0);
    // The q graph peaks there too.
    for (const Sample& s : r.samples) {
        if (dot(s.state.r, s.state.v) > 0.0) {
            REQUIRE(aerodynamicsAt(r, s).dynamicPressure <= aero.dynamicPressure);
        }
    }

    // Real launchers lose 100-200 m/s to drag.
    REQUIRE(r.dragLoss > 50.0);
    REQUIRE(r.dragLoss < 300.0);

    // Without air the same rocket has propellant to spare.
    Mission vacuum = defaultMission();
    vacuum.rocket.dragCoefficient = 0.0;
    const SimResult v = simulate(vacuum);
    REQUIRE(v.outcome == Outcome::Orbit);
    REQUIRE_FALSE(v.maxDynamicPressure.has_value());
    REQUIRE(v.dragLoss == 0.0);
    REQUIRE(v.propellantLeft > r.propellantLeft);
}

TEST_CASE("A fatter rocket loses more to drag", "[simulation]") {
    Mission thin = defaultMission();
    thin.rocket.diameter = 1.5;
    Mission fat = defaultMission();
    fat.rocket.diameter = 4.0;
    const SimResult a = simulate(thin);
    const SimResult b = simulate(fat);
    REQUIRE(b.dragLoss > 2.0 * a.dragLoss);
}

TEST_CASE("Engines give less thrust in thick air", "[atmosphere][engine]") {
    const Mission mission = defaultMission();
    const Stage& stage = mission.rocket.stages[0];
    // F = F_vac · (1 − L · p/p0): the full loss at sea level, none in a vacuum,
    // and in between in proportion to the pressure.
    REQUIRE(thrustAt(stage, 0.0) == stage.thrust);
    REQUIRE_THAT(seaLevelThrust(stage),
                 WithinRel(stage.thrust * (1.0 - stage.seaLevelLoss), 1e-12));
    REQUIRE_THAT(thrustAt(stage, kSeaLevelPressure / 2.0),
                 WithinRel(stage.thrust * (1.0 - stage.seaLevelLoss / 2.0), 1e-12));
    REQUIRE_THAT(seaLevelIsp(stage), WithinRel(stage.isp * (1.0 - stage.seaLevelLoss), 1e-12));
    // The default rocket lifts off with the prototype's 1.2 MN.
    REQUIRE_THAT(seaLevelThrust(stage), WithinRel(1.2e6, 0.01));
    REQUIRE_THAT(
        liftoffThrustToWeight(defaultMission().rocket),
        WithinRel(seaLevelThrust(stage) / (64000.0 * kMuEarth / (kRadiusEarth * kRadiusEarth)),
                  1e-12));
}

TEST_CASE("Delta-v used counts what the engine really delivered", "[atmosphere][engine]") {
    // Straight up from the pole with no gravity and no drag: every bit of the
    // engine's push becomes speed. With a 12 % sea-level loss, the burn
    // delivers less than the vacuum rocket equation says, by the
    // back-pressure loss.
    Mission m = defaultMission();
    m.rocket.dragCoefficient = 0.0;
    m.site.latitude = degToRad(90.0);
    m.guidance.pitchStartTime = 1e9;
    m.plan = FlightPlan::BurnToDepletion;
    SimOptions noGravity;
    noGravity.mu = 0.0;
    noGravity.thirdBodyGravity = false;
    const SimResult r = simulate(m, noGravity);

    const double finalSpeed = r.events.front().sample.speed(); // at burnout
    REQUIRE(r.pressureLoss > 10.0);
    REQUIRE_THAT(r.deltaVUsed + r.pressureLoss, WithinRel(idealDeltaV(m.rocket), 1e-9));
    REQUIRE_THAT(r.deltaVUsed, WithinRel(finalSpeed, 0.002));

    // Without the loss, none of it.
    m.rocket.stages[0].seaLevelLoss = 0.0;
    const SimResult vacuum = simulate(m, noGravity);
    REQUIRE(vacuum.pressureLoss == 0.0);
    REQUIRE(vacuum.events.front().sample.speed() > finalSpeed);
}

TEST_CASE("A flight to orbit loses some tens of m/s to back-pressure", "[atmosphere][engine]") {
    const SimResult r = simulate(defaultMission());
    REQUIRE(r.outcome == Outcome::Orbit);
    // Real launchers: a few tens to a hundred m/s.
    REQUIRE(r.pressureLoss > 20.0);
    REQUIRE(r.pressureLoss < 200.0);
}
