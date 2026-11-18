// SPDX-License-Identifier: GPL-3.0-or-later
// Multi-stage rockets: the rocket equation per stage, separation, dropped
// stages, and holding altitude on a weak upper stage.

#include "orbit/core/angles.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/guidance.hpp"
#include "orbit/core/mission_parameters.hpp"
#include "orbit/core/simulation.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// The default rocket cut in two: the same 64 t, with the same structure and
/// propellant split between two stages with the same engine type.
Mission splitRocket() {
    Mission m = defaultMission();
    m.rocket.stageCount = 2;
    m.rocket.stages[0] = {
        .dryMass = 3000.0, .propellantMass = 48000.0, .thrust = 1.2e6, .isp = 340.0};
    m.rocket.stages[1] = {
        .dryMass = 1000.0, .propellantMass = 12000.0, .thrust = 150e3, .isp = 340.0};
    return m;
}

std::vector<const FlightEvent*> eventsOf(const SimResult& r, EventType type) {
    std::vector<const FlightEvent*> found;
    for (const FlightEvent& e : r.events) {
        if (e.type == type) found.push_back(&e);
    }
    return found;
}

} // namespace

TEST_CASE("Each stage's delta-v counts what it carries", "[staging]") {
    const Rocket rocket = splitRocket().rocket;
    REQUIRE_THAT(liftoffMass(rocket), WithinAbs(64000.0, 1e-9));
    REQUIRE_THAT(massAbove(rocket, 0), WithinAbs(13000.0, 1e-9)); // stage 2, full
    REQUIRE_THAT(massAbove(rocket, 1), WithinAbs(0.0, 1e-9));     // no payload

    const double ve = 340.0 * kG0;
    REQUIRE_THAT(stageDeltaV(rocket, 0), WithinRel(ve * std::log(64.0 / 16.0), 1e-12));
    REQUIRE_THAT(stageDeltaV(rocket, 1), WithinRel(ve * std::log(13.0 / 1.0), 1e-12));

    // The same masses in one stage give far less: the empty first-stage
    // tanks ride all the way.
    const Rocket single = defaultMission().rocket;
    REQUIRE_THAT(idealDeltaV(single), WithinRel(ve * std::log(64.0 / 4.0), 1e-12));
    REQUIRE(idealDeltaV(rocket) > idealDeltaV(single) + 3500.0);

    // Payload rides on top of everything.
    Rocket loaded = rocket;
    loaded.payloadMass = 2000.0;
    REQUIRE_THAT(massAbove(loaded, 1), WithinAbs(2000.0, 1e-9));
    REQUIRE(idealDeltaV(loaded) < idealDeltaV(rocket));
    REQUIRE_THAT(thrustToWeightAtIgnition(loaded, 1),
                 WithinRel(150e3 / (15000.0 * kMuEarth / (kRadiusEarth * kRadiusEarth)), 1e-12));
}

TEST_CASE("Stages beyond the stage count are ignored", "[staging]") {
    Mission a = defaultMission(); // one stage
    Mission b = a;
    b.rocket.stages[1].propellantMass = 99e3;
    b.rocket.stages[2].thrust = 1.0;
    REQUIRE(liftoffMass(a.rocket) == liftoffMass(b.rocket));
    const SimResult ra = simulate(a);
    const SimResult rb = simulate(b);
    REQUIRE(ra.duration == rb.duration);
    REQUIRE(rb.spentStages.empty());
    REQUIRE(eventsOf(rb, EventType::StageSeparation).empty());
}

TEST_CASE("A two-stage burn matches the rocket equation, stage by stage", "[staging]") {
    // No gravity, no air, no ground speed (from the pole), straight up.
    Mission m = splitRocket();
    m.rocket.payloadMass = 500.0;
    m.rocket.dragCoefficient = 0.0;
    m.site.latitude = degToRad(90.0);
    m.guidance.pitchStartTime = 1e9;
    m.plan = FlightPlan::BurnToDepletion;
    SimOptions vacuum;
    vacuum.mu = 0.0;
    vacuum.thirdBodyGravity = false;
    const SimResult r = simulate(m, vacuum);

    const auto separations = eventsOf(r, EventType::StageSeparation);
    REQUIRE(separations.size() == 1);
    REQUIRE(separations[0]->description == "Ascent: stage 1 separation");
    const Sample& atSeparation = separations[0]->sample;
    REQUIRE_THAT(atSeparation.t, WithinRel(burnTime(m.rocket.stages[0]), 1e-12));
    REQUIRE_THAT(atSeparation.speed(), WithinRel(stageDeltaV(m.rocket, 0), 1e-9));
    // Just before the drop: stage 1 empty, everything else on top.
    REQUIRE_THAT(atSeparation.state.m, WithinRel(3000.0 + 13000.0 + 500.0, 1e-12));

    // Right after it, a sample with the empty stage gone.
    const auto after = std::ranges::find_if(r.samples, [&](const Sample& s) {
        return s.t == atSeparation.t && s.state.m < atSeparation.state.m;
    });
    REQUIRE(after != r.samples.end());
    REQUIRE_THAT(after->state.m, WithinRel(13500.0, 1e-12));

    // Then the second stage burns until it is empty, and that ends the burn.
    const auto burnouts = eventsOf(r, EventType::Burnout);
    REQUIRE(burnouts.size() == 1);
    REQUIRE_THAT(burnouts[0]->sample.t,
                 WithinRel(burnTime(m.rocket.stages[0]) + burnTime(m.rocket.stages[1]), 1e-12));
    REQUIRE_THAT(burnouts[0]->sample.speed(), WithinRel(idealDeltaV(m.rocket), 1e-9));
    REQUIRE_THAT(r.deltaVUsed, WithinRel(idealDeltaV(m.rocket), 1e-12));
    REQUIRE(r.propellantLeft == 0.0);
    // The payload and the last, empty stage are what's left.
    REQUIRE_THAT(r.samples.back().state.m, WithinRel(1500.0, 1e-12));
}

TEST_CASE("A stage that can't fire is dropped straight away", "[staging]") {
    Mission m = splitRocket();
    m.rocket.stageCount = 3;
    m.rocket.stages[2] = m.rocket.stages[1];
    m.rocket.stages[1].propellantMass = 0.0; // an empty middle stage
    const SimResult r = simulate(m);

    const auto separations = eventsOf(r, EventType::StageSeparation);
    REQUIRE(separations.size() == 2);
    REQUIRE(separations[0]->sample.t == separations[1]->sample.t);
    REQUIRE(separations[1]->description == "Ascent: stage 2 separation");
    REQUIRE(r.spentStages.size() == 2);
    REQUIRE(r.spentStages[1].stage == 1);
}

TEST_CASE("Cut in two, the default rocket reaches orbit with propellant to spare", "[staging]") {
    const SimResult single = simulate(defaultMission());
    const SimResult split = simulate(splitRocket());
    REQUIRE(split.outcome == Outcome::Orbit);
    REQUIRE(split.propellantLeft > 3.0 * single.propellantLeft);

    SECTION("The first stage falls back downrange") {
        REQUIRE(split.spentStages.size() == 1);
        const SpentStage& stage1 = split.spentStages.front();
        REQUIRE(stage1.stage == 0);
        REQUIRE(stage1.impactTime.has_value());
        // About a thousand kilometres out: it was dropped at 4 km/s, 75 km up.
        REQUIRE(stage1.impactDistance > 300e3);
        REQUIRE(stage1.impactDistance < 3000e3);

        // It starts where and when the rocket dropped it, with only its own
        // mass, and lands on the ground.
        const FlightEvent* separation = eventsOf(split, EventType::StageSeparation).front();
        const Sample& first = stage1.samples.front();
        REQUIRE(first.t == separation->sample.t);
        REQUIRE(norm(first.state.r - separation->sample.state.r) < 1e-6);
        REQUIRE(first.state.m == 3000.0);
        REQUIRE(*stage1.impactTime > first.t);
        REQUIRE_THAT(stage1.samples.back().altitude(), WithinAbs(0.0, 1e-3));
        REQUIRE(std::ranges::is_sorted(stage1.samples, {}, &Sample::t));
    }
}

TEST_CASE("A weak upper stage holds its altitude while circularizing", "[staging][guidance]") {
    // The second stage pushes only a little more than its own weight, so
    // circularizing takes three minutes. Without gravity compensation it sank
    // to a 150 km orbit; tilting up to cover gravity keeps it near 200 km.
    const SimResult r = simulate(splitRocket());
    REQUIRE(r.outcome == Outcome::Orbit);
    REQUIRE(r.orbit.periapsisAltitude > 190e3);
    REQUIRE(r.orbit.apoapsisAltitude < 202e3);

    SECTION("The compensation tilts the engine up, and only below circular speed") {
        const Vec3 r0{kRadiusEarth + 200e3, 0.0, 0.0};
        const Vec3 normal{0.0, 0.0, 1.0};
        const Vec3 slow{0.0, 4000.0, 0.0};
        const Vec3 plain = thrustDirection(GuidancePhase::Circularize, r0, slow, {}, 0.0, normal);
        const Vec3 weak =
            thrustDirection(GuidancePhase::Circularize, r0, slow, {}, 0.0, normal, kMuEarth, 10.0);
        REQUIRE_THAT(plain.x, WithinAbs(0.0, 1e-12)); // at apoapsis: horizontal
        REQUIRE(weak.x > 0.3);                        // a weak engine tilts well up
        const Vec3 strong =
            thrustDirection(GuidancePhase::Circularize, r0, slow, {}, 0.0, normal, kMuEarth, 300.0);
        REQUIRE(strong.x > 0.0);
        REQUIRE(strong.x < weak.x);
    }
}

TEST_CASE("Stage parameters have names that tell them apart", "[staging][parameters]") {
    REQUIRE(parameterTitle(*findParameter("stage-2-thrust")) == "Stage 2 thrust");
    REQUIRE(parameterTitle(*findParameter("thrust")) == "Stage 1 thrust");
    REQUIRE(parameterTitle(*findParameter("isp")) == "Stage 1 specific impulse");
    REQUIRE(parameterTitle(*findParameter("kick-angle")) == "Kick angle");

    Mission m = defaultMission();
    findParameter("stages")->set(m, 2.4);
    REQUIRE(m.rocket.stageCount == 2);
    findParameter("stages")->set(m, 7.0);
    REQUIRE(m.rocket.stageCount == kMaxStages);
    findParameter("stage-3-propellant")->set(m, 1.5);
    REQUIRE(m.rocket.stages[2].propellantMass == 1500.0);
}

TEST_CASE("Guided ascent hits the target orbit where the open-loop plan can't", "[guidance]") {
    // The two-stage rocket with 2 t of payload: heavy, so the 20° kick flies
    // flat. The classic plan reaches its apoapsis target only after passing
    // the top of its path, and circularizes far too low.
    Mission m = splitRocket();
    m.rocket.payloadMass = 2000.0;
    const SimResult classic = simulate(m);
    REQUIRE(classic.orbit.periapsisAltitude < 150e3);

    m.plan = FlightPlan::GuidedOrbit;
    const SimResult guided = simulate(m);
    REQUIRE(guided.outcome == Outcome::Orbit);
    REQUIRE_THAT(guided.orbit.periapsisAltitude, WithinAbs(200e3, 1e3));
    REQUIRE_THAT(guided.orbit.apoapsisAltitude, WithinAbs(200e3, 1e3));

    SECTION("whatever the kick") {
        for (const double kick : {12.0, 16.0, 20.0, 24.0}) {
            CAPTURE(kick);
            m.guidance.kickAngle = degToRad(kick);
            const SimResult r = simulate(m);
            REQUIRE_THAT(r.orbit.periapsisAltitude, WithinAbs(200e3, 1e3));
            REQUIRE_THAT(r.orbit.apoapsisAltitude, WithinAbs(200e3, 1e3));
        }
    }
    SECTION("and it carries more") {
        m.rocket.payloadMass = 3000.0;
        REQUIRE(simulate(m).outcome == Outcome::Orbit);
        m.plan = FlightPlan::Orbit;
        REQUIRE(simulate(m).outcome != Outcome::Orbit);
    }
}

TEST_CASE("A powerful single stage flies guided too, coasting up at the end", "[guidance]") {
    Mission m = defaultMission();
    m.plan = FlightPlan::GuidedOrbit;
    const SimResult r = simulate(m);
    REQUIRE(r.outcome == Outcome::Orbit);
    REQUIRE_THAT(r.orbit.periapsisAltitude, WithinAbs(200e3, 1e3));
    REQUIRE_THAT(r.orbit.apoapsisAltitude, WithinAbs(200e3, 1e3));
    // It gains speed faster than it climbs, so the apoapsis can't be held:
    // the guided burn stops and the classic coast and circularization finish.
    REQUIRE(std::ranges::any_of(r.events, [](const FlightEvent& e) {
        return e.description == "Circularization: engine on";
    }));
    // Climbing steeper than the open-loop plan costs a little more.
    const double classic = simulate(defaultMission()).deltaVUsed;
    REQUIRE(r.deltaVUsed > classic);
    REQUIRE(r.deltaVUsed < classic + 300.0);
}

TEST_CASE("Orbit insertion steering stays within its pitch limits", "[guidance]") {
    const Vec3 r{kRadiusEarth + 100e3, 0.0, 0.0};
    const Vec3 fallback{0.0, 1.0, 0.0};
    const Vec3 slowClimb{500.0, 3000.0, 0.0};
    const Vec3 d =
        orbitInsertionDirection(r, slowClimb, kRadiusEarth + 200e3, 20.0, 60.0, fallback);
    REQUIRE_THAT(norm(d), WithinAbs(1.0, 1e-12));
    const double pitch = std::asin(d.x);
    REQUIRE(pitch >= degToRad(-30.0) - 1e-9);
    REQUIRE(pitch <= degToRad(80.0) + 1e-9);
    // Far too fast for this height: the apoapsis is above the target, so
    // it noses down as far as it may.
    const Vec3 tooFast{0.0, 8500.0, 0.0};
    const Vec3 down =
        orbitInsertionDirection(r, tooFast, kRadiusEarth + 200e3, 20.0, 60.0, fallback);
    REQUIRE_THAT(std::asin(down.x), WithinAbs(degToRad(-30.0), 1e-9));
}
