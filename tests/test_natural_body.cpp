// SPDX-License-Identifier: GPL-3.0-or-later
// Natural bodies: elements for any conic, Kepler's equation, precession, and
// bodies coasting past the Earth (checked against JPL Horizons for Apophis).

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/earth.hpp"
#include "orbit/core/integrator.hpp"
#include "orbit/core/natural_body.hpp"
#include "orbit/core/orbit.hpp"
#include "orbit/core/scenario.hpp"
#include "orbit/core/simulation.hpp"
#include "orbit/core/sweep.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <filesystem>
#include <optional>
#include <ranges>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

Mission bodyMission(const NaturalBody& body) {
    Mission m = defaultMission();
    m.vehicle = Vehicle::NaturalBody;
    m.body = body;
    return m;
}

/// Coast under the Earth's gravity alone for `duration` s (either sign).
State coastTwoBody(State y, double duration) {
    DormandPrince45 stepper(
        [](double, const State& s) {
            const double r = norm(s.r);
            return State{s.v, (-kMuEarth / (r * r * r)) * s.r, 0.0};
        },
        Tolerances{});
    double t = 0.0;
    double h = duration > 0.0 ? 1.0 : -1.0;
    while (std::abs(t) < std::abs(duration)) {
        const double hStep = duration > 0.0 ? std::min(h, duration - t) : std::max(h, duration - t);
        const auto result = stepper.step(t, y, hStep);
        h = result.nextStep;
        if (!result.accepted) continue;
        y = result.y;
        t += hStep;
    }
    return y;
}

/// Apophis's geocentric position from JPL Horizons (ICRF, km) at 21:45 UTC
/// on 11–15 April 2029.
struct HorizonsPoint {
    int day = 0;
    Vec3 r;
};
constexpr std::array kApophisHorizons{
    HorizonsPoint{11, {-774576.368, -471512.354, -515832.238}},
    HorizonsPoint{12, {-409400.704, -224981.357, -253325.393}},
    HorizonsPoint{13, {-19129.052, 27228.908, 18371.955}},
    HorizonsPoint{14, {487829.824, 113610.155, 177848.285}},
    HorizonsPoint{15, {973384.073, 189041.409, 323738.724}},
};

} // namespace

TEST_CASE("stateFromPeriapsis agrees with stateFromElements for an ellipse", "[body]") {
    const double a = 10'000e3;
    const double e = 0.3;
    for (const double nu : {0.0, 1.0, 2.5, 4.0}) {
        const PositionVelocity viaA =
            stateFromElements(a, e, degToRad(30.0), degToRad(40.0), degToRad(50.0), nu);
        const PositionVelocity viaQ = stateFromPeriapsis(a * (1.0 - e), e, degToRad(30.0),
                                                         degToRad(40.0), degToRad(50.0), nu);
        CHECK(norm(viaA.r - viaQ.r) < 1e-6);
        CHECK(norm(viaA.v - viaQ.v) < 1e-9);
    }
}

TEST_CASE("elements of any conic survive a round trip through a state vector", "[body]") {
    const double e = GENERATE(0.0, 0.5, 1.0, 1.5, 4.25);
    const double q = 38'000e3;
    const double i = degToRad(140.6);
    const double raan = degToRad(167.4);
    const double w = degToRad(49.7);
    // A true anomaly every conic reaches (a hyperbola only goes so far).
    const double nu = degToRad(-60.0);
    const PositionVelocity s = stateFromPeriapsis(q, e, i, raan, w, nu);
    const OrbitalElements el = elementsFromState(s.r, s.v);

    CHECK_THAT(el.periapsis, WithinRel(q, 1e-9));
    CHECK_THAT(el.eccentricity, WithinAbs(e, 1e-9));
    CHECK_THAT(el.inclination, WithinAbs(i, 1e-9));
    CHECK_THAT(el.raan, WithinAbs(raan, 1e-9));
    if (e > 0.0) {
        CHECK_THAT(el.argumentOfPeriapsis, WithinAbs(w, 1e-9));
        CHECK_THAT(el.trueAnomaly, WithinAbs(wrapTwoPi(nu), 1e-9));
    }
    // Energy: the vis-viva equation, v² = μ(2/r − 1/a), so ε = −μ/2a, and
    // for a parabola exactly zero.
    const double expectedEnergy = e == 1.0 ? 0.0 : -kMuEarth * (1.0 - e) / (2.0 * q);
    CHECK_THAT(el.energy, WithinAbs(expectedEnergy, 1e-6));
}

TEST_CASE("time since periapsis follows Kepler's equation", "[body]") {
    SECTION("half an ellipse takes half a period") {
        const double a = 20'000e3;
        const double e = 0.6;
        const double t = timeSincePeriapsis(a * (1.0 - e), e, kPi - 1e-12);
        CHECK_THAT(t, WithinRel(orbitalPeriod(a) / 2.0, 1e-9));
    }
    SECTION("before periapsis, the time is negative, and symmetric") {
        const double t1 = timeSincePeriapsis(40'000e3, 3.0, 0.5);
        const double t2 = timeSincePeriapsis(40'000e3, 3.0, -0.5);
        CHECK(t1 > 0.0);
        CHECK_THAT(t2, WithinRel(-t1, 1e-12));
    }
    SECTION("matches a numerical coast from periapsis on every conic") {
        const double e = GENERATE(0.2, 0.999, 1.0, 1.001, 4.25);
        const double q = 38'000e3;
        const PositionVelocity p = stateFromPeriapsis(q, e, 0.3, 0.2, 0.1, 0.0);
        const double duration = 20'000.0;
        const State end = coastTwoBody({p.r, p.v, 0.0}, duration);
        const OrbitalElements el = elementsFromState(end.r, end.v);
        CHECK_THAT(timeSincePeriapsis(q, e, el.trueAnomaly), WithinAbs(duration, 1e-3));
    }
    SECTION("the parabola joins the ellipse and the hyperbola smoothly") {
        const double q = 38'000e3;
        const double nu = 1.5;
        const double parabola = timeSincePeriapsis(q, 1.0, nu);
        CHECK_THAT(timeSincePeriapsis(q, 1.0 - 1e-6, nu), WithinRel(parabola, 1e-5));
        CHECK_THAT(timeSincePeriapsis(q, 1.0 + 1e-6, nu), WithinRel(parabola, 1e-5));
    }
}

TEST_CASE("precession turns J2000 coordinates into those of date", "[body]") {
    // Meeus, "Astronomical Algorithms", example 21.b: θ Persei. Its J2000
    // place, moved by its proper motion to 2028 November 13.19 TD (RA 2h 44m
    // 12.975s, Dec +49° 13' 39.90"), precesses to RA 2h 46m 11.331s,
    // Dec +49° 20' 54.54".
    const double ra0 = degToRad(41.0540625);
    const double dec0 = degToRad(49.22775);
    const Vec3 j2000{std::cos(dec0) * std::cos(ra0), std::cos(dec0) * std::sin(ra0),
                     std::sin(dec0)};
    const double epoch = (2462088.69 - 2451545.0) * 86400.0;
    const Vec3 ofDate = precessionFromJ2000(j2000, epoch);
    const double ra = wrapTwoPi(std::atan2(ofDate.y, ofDate.x));
    const double dec = std::asin(ofDate.z);
    CHECK_THAT(radToDeg(ra), WithinAbs(41.5472125, 2e-6));
    CHECK_THAT(radToDeg(dec), WithinAbs(49.3484833, 2e-6));

    // And back again.
    CHECK(norm(precessionToJ2000(ofDate, epoch) - j2000) < 1e-14);
    // No precession at J2000 itself.
    CHECK(norm(precessionFromJ2000(j2000, 0.0) - j2000) < 1e-15);
}

TEST_CASE("switching how a body is given keeps its path", "[body]") {
    NaturalBody body = apophisFlyby();
    const NaturalBody original = body;

    setBodyGiven(body, BodyGiven::StateVector);
    CHECK(body.given == BodyGiven::StateVector);
    CHECK(body.epoch == original.epoch);
    CHECK(norm(body.position - bodyStateJ2000(original).r) < 1e-6);

    // Move the state 6 hours along the (two-body) path, and on to elements:
    // the epoch should come back to the periapsis.
    const State later = coastTwoBody({body.position, body.velocity, 0.0}, 6.0 * 3600.0);
    body.position = later.r;
    body.velocity = later.v;
    body.epoch += 6.0 * 3600.0;
    setBodyGiven(body, BodyGiven::Elements);
    CHECK_THAT(body.epoch, WithinAbs(original.epoch, 1e-3));
    CHECK_THAT(body.periapsisDistance, WithinRel(original.periapsisDistance, 1e-9));
    CHECK_THAT(body.eccentricity, WithinAbs(original.eccentricity, 1e-9));
    CHECK_THAT(body.argumentOfPeriapsis, WithinAbs(original.argumentOfPeriapsis, 1e-9));
    // The span still covers the same stretch of time: 2 days before the old
    // epoch was 2 days + 6 h before the periapsis.
    CHECK_THAT(body.timeBefore, WithinAbs(original.timeBefore - 6.0 * 3600.0, 1e-3));
    CHECK_THAT(body.timeAfter, WithinAbs(original.timeAfter + 6.0 * 3600.0, 1e-3));
}

TEST_CASE("with the Earth alone, a flyby is a symmetric hyperbola", "[body]") {
    const Mission mission = bodyMission(apophisFlyby());
    SimOptions options;
    options.thirdBodyGravity = false;
    const SimResult r = simulateNaturalBody(mission, options);

    REQUIRE(r.encounter);
    CHECK(r.outcome == Outcome::Flyby);
    CHECK(r.endReason == EndReason::SpanEnd);
    CHECK_THAT(r.duration, WithinAbs(4.0 * 86400.0, 1e-6));
    CHECK_THAT(r.encounter->epochTime, WithinAbs(2.0 * 86400.0, 1e-6));
    CHECK_THAT(r.startEpoch, WithinAbs(mission.body.epoch - 2.0 * 86400.0, 1e-6));
    CHECK(std::ranges::is_sorted(r.samples, {}, &Sample::t));

    // Closest approach: exactly the periapsis, exactly at the epoch.
    CHECK_THAT(norm(r.encounter->closest.state.r), WithinRel(38011.3e3, 1e-6));
    const auto closest =
        std::ranges::find(r.events, EventType::EarthClosestApproach, &FlightEvent::type);
    REQUIRE(closest != r.events.end());
    CHECK_THAT(closest->sample.t, WithinAbs(r.encounter->epochTime, 1e-3));
    CHECK(std::ranges::count(r.events, EventType::EarthClosestApproach, &FlightEvent::type) == 1);
    CHECK(std::ranges::count(r.events, EventType::Epoch, &FlightEvent::type) == 1);

    // Energy is conserved, so the speed far away is the same in and out,
    // and the distance a day before equals the distance a day after.
    CHECK_THAT(r.encounter->arrivalSpeed, WithinRel(r.encounter->departureSpeed, 1e-8));
    CHECK_THAT(r.encounter->arrivalSpeed, WithinRel(5841.4, 1e-3));
    const double dayBefore = norm(stateAt(r, 1.0 * 86400.0).r);
    const double dayAfter = norm(stateAt(r, 3.0 * 86400.0).r);
    CHECK_THAT(dayBefore, WithinRel(dayAfter, 1e-4));
}

TEST_CASE("Apophis's 2029 flyby matches JPL Horizons", "[body]") {
    const SimResult r = simulate(bodyMission(apophisFlyby()));
    REQUIRE(r.encounter);
    CHECK(r.outcome == Outcome::Flyby);

    // Closest approach: 38,011 km from the Earth's centre, near 21:45 UTC.
    CHECK_THAT(norm(r.encounter->closest.state.r), WithinAbs(38011e3, 30e3));
    const double closestEpoch = r.startEpoch + r.encounter->closest.t;
    CHECK_THAT(closestEpoch, WithinAbs(epochFromCalendar({2029, 4, 13, 21, 45, 0.0}), 120.0));

    // A day and two days either side: within 50 km of JPL's position (in
    // fact within 20 km), although JPL uses every planet and a precise
    // ephemeris, and ours brings the Sun and the Moon from low-precision
    // series. Near the Earth, the Earth's pull is what counts.
    for (const HorizonsPoint& point : kApophisHorizons) {
        const double epoch = epochFromCalendar({2029, 4, point.day, 21, 45, 0.0});
        const Vec3 ours = precessionToJ2000(stateAt(r, epoch - r.startEpoch).r, epoch);
        INFO("13 April + " << point.day - 13 << " days: off by "
                           << norm(ours - point.r * 1000.0) / 1000.0 << " km");
        CHECK(norm(ours - point.r * 1000.0) < 50e3);
    }
}

TEST_CASE("a body given by elements below the surface hits the ground", "[body]") {
    NaturalBody body = apophisFlyby();
    body.periapsisDistance = 3000e3;
    const SimResult r = simulate(bodyMission(body));

    CHECK(r.outcome == Outcome::Impact);
    CHECK(r.endReason == EndReason::Impact);
    CHECK_THAT(norm(r.samples.back().state.r), WithinAbs(kRadiusEarth, 1.0));
    // It hits before it would have reached the (underground) periapsis.
    CHECK(r.startEpoch + r.duration < body.epoch);
    CHECK(std::ranges::none_of(r.events,
                               [](const FlightEvent& e) { return e.type == EventType::Epoch; }));
    // Still coming in from far away: two days' worth, less the last minutes.
    CHECK(r.samples.front().altitude() > 500'000e3);
}

TEST_CASE("a moon bound to the Earth stays in orbit", "[body]") {
    NaturalBody body;
    body.given = BodyGiven::Elements;
    body.periapsisDistance = 100'000e3;
    body.eccentricity = 0.3;
    body.inclination = degToRad(20.0);
    body.epoch = epochFromCalendar({2030, 1, 1, 0, 0, 0.0});
    body.timeBefore = 0.0;
    body.timeAfter = 20.0 * 86400.0;
    const SimResult r = simulate(bodyMission(body));

    CHECK(r.outcome == Outcome::Orbit);
    REQUIRE(r.encounter);
    CHECK_THAT(r.encounter->timeBound, WithinRel(r.duration, 1e-9));
    CHECK(r.encounter->arrivalSpeed == 0.0);
    // Period: 2π √(a³/μ) with a = q / (1 − e) ≈ 142,857 km: about 6.2 days,
    // so three periapsis passes after the one it starts at.
    CHECK(std::ranges::count(r.events, EventType::EarthClosestApproach, &FlightEvent::type) == 3);
}

TEST_CASE("a natural body only offers its own parameters", "[body]") {
    Mission mission = bodyMission(apophisFlyby());
    const auto hasTarget = [&](std::string_view key) {
        return findSweepTarget(mission, key).has_value();
    };
    CHECK(hasTarget("periapsis-distance"));
    CHECK(hasTarget("epoch-hour"));
    CHECK_FALSE(hasTarget("body-x"));
    CHECK_FALSE(hasTarget("thrust"));
    CHECK_FALSE(hasTarget("kick-angle"));

    mission.body.given = BodyGiven::StateVector;
    CHECK(hasTarget("body-vx"));
    CHECK_FALSE(hasTarget("periapsis-distance"));

    mission.vehicle = Vehicle::Rocket;
    CHECK(hasTarget("thrust"));
    CHECK_FALSE(hasTarget("days-before"));

    // Sweeping the closest approach brings the flybys in closer.
    mission = bodyMission(apophisFlyby());
    const std::vector<Mission> sweep =
        sweepMissions(mission, {"periapsis-distance", 10000.0, 50000.0, 3});
    CHECK(sweep.at(0).body.periapsisDistance == 10000e3);
    CHECK(sweep.at(2).body.periapsisDistance == 50000e3);
}

TEST_CASE("natural body scenarios round-trip", "[body][scenario]") {
    Scenario scenario;
    scenario.name = "Apophis";
    scenario.mission = bodyMission(apophisFlyby());
    scenario.mission.body.timeAfter = 5.0 * 86400.0;
    const std::string json = scenarioToJson(scenario);
    CHECK(json.find("\"vehicle\": \"natural-body\"") != std::string::npos);
    CHECK(json.find("\"epoch-utc\": \"2029-04-13T21:45:04Z\"") != std::string::npos);
    CHECK(json.find("\"thrust\"") == std::string::npos); // no rocket in a body's file

    const LoadedScenario loaded = scenarioFromJson(json);
    CHECK(loaded.warnings.empty());
    const Mission& m = loaded.scenario.mission;
    CHECK(m.vehicle == Vehicle::NaturalBody);
    CHECK(m.body.given == BodyGiven::Elements);
    CHECK(m.body.epoch == scenario.mission.body.epoch);
    CHECK_THAT(m.body.eccentricity, WithinAbs(4.2539, 1e-12));
    CHECK_THAT(m.body.timeAfter, WithinAbs(5.0 * 86400.0, 1e-6));

    SECTION("given as a state vector") {
        Scenario s2 = scenario;
        setBodyGiven(s2.mission.body, BodyGiven::StateVector);
        const LoadedScenario back = scenarioFromJson(scenarioToJson(s2));
        CHECK(back.scenario.mission.body.given == BodyGiven::StateVector);
        CHECK(norm(back.scenario.mission.body.position - s2.mission.body.position) < 1.0);
        CHECK(norm(back.scenario.mission.body.velocity - s2.mission.body.velocity) < 1e-6);
    }
    SECTION("a rocket's file says so") {
        Scenario rocket;
        CHECK(scenarioToJson(rocket).find("\"vehicle\": \"rocket\"") != std::string::npos);
        CHECK(scenarioToJson(rocket).find("periapsis-distance") == std::string::npos);
    }
    SECTION("bad values are refused") {
        CHECK_THROWS_AS(
            scenarioFromJson(
                R"({"format": "orbitsim-scenario", "version": 6, "mission": {"vehicle": "comet"}})"),
            ScenarioError);
    }
}

// --- The example scenarios say what they show; check they do ---------------

namespace {

Scenario example(const char* name) {
    return loadScenarioFile(std::filesystem::path(ORBIT_SCENARIO_DIR) / name).scenario;
}

/// When an event first happens, as an epoch.
std::optional<double> eventEpoch(const SimResult& r, EventType type) {
    const auto it = std::ranges::find(r.events, type, &FlightEvent::type);
    if (it == r.events.end()) return std::nullopt;
    return r.startEpoch + it->sample.t;
}

} // namespace

TEST_CASE("2024 PT5 is a mini-moon from 29 September to 25 November 2024", "[body][example]") {
    const SimResult r = simulate(example("minimoon-2024-pt5.json").mission);
    CHECK(r.outcome == Outcome::TemporaryCapture);
    // JPL: the energy relative to the Earth turns negative between 28 and 30
    // September, and positive again between 25 and 27 November.
    const std::optional<double> captured = eventEpoch(r, EventType::EarthCapture);
    const std::optional<double> released = eventEpoch(r, EventType::EarthRelease);
    REQUIRE(captured);
    REQUIRE(released);
    CHECK_THAT(*captured, WithinAbs(epochFromCalendar({2024, 9, 29, 12, 0, 0.0}), 86400.0));
    CHECK_THAT(*released, WithinAbs(epochFromCalendar({2024, 11, 26, 0, 0, 0.0}), 86400.0));
    // 567,149 km from the Earth on 9 August (JPL, 0h UTC), about the closest.
    REQUIRE(r.encounter);
    CHECK_THAT(norm(r.encounter->closest.state.r), WithinRel(567'000e3, 0.002));
}

TEST_CASE("The Chelyabinsk impactor passes 30 km where the real one burst", "[body][example]") {
    const SimResult r = simulate(example("chelyabinsk-2013.json").mission);
    CHECK(r.outcome == Outcome::Impact);
    // The first sample below 30 km, and where it is over the ground.
    const auto low =
        std::ranges::find_if(r.samples, [](const Sample& s) { return s.altitude() < 30e3; });
    REQUIRE(low != r.samples.end());
    const double epoch = r.startEpoch + low->t;
    const GeoPoint ground = groundPoint(low->state.r, earthRotationAngleAt(epoch));
    CHECK_THAT(epoch, WithinAbs(epochFromCalendar({2013, 2, 15, 3, 20, 33.0}), 2.0));
    CHECK_THAT(radToDeg(ground.latitude), WithinAbs(54.9, 0.15));
    CHECK_THAT(radToDeg(ground.longitude), WithinAbs(61.2, 0.15));
    // About 15 km/s far from the Earth (published: 15.1 km/s).
    REQUIRE(r.encounter);
    CHECK_THAT(r.encounter->arrivalSpeed, WithinAbs(15.2e3, 300.0));
}

TEST_CASE("Gravitational focusing: Apophis aimed to miss can still hit", "[body][example]") {
    const Scenario s = example("apophis-aim-sweep.json");
    REQUIRE(s.sweep);
    std::vector<SimResult> results;
    for (const Mission& m : sweepMissions(s.mission, *s.sweep)) results.push_back(simulate(m));
    const auto closest = [](const SimResult& r) { return norm(r.encounter->closest.state.r); };

    // The first is the real Apophis, aimed to miss by about 48,000 km and
    // pulled in to 38,000.
    CHECK_THAT(closest(results.front()), WithinAbs(38011e3, 30e3));
    CHECK_THAT(results.front().encounter->impactParameter, WithinRel(48'000e3, 0.02));

    // Gravity pulls every path in: the closest approach is nearer than the
    // aim, by the hyperbola's formula q = (μ/v∞²)(√(1 + (b v∞²/μ)²) − 1).
    // (Within 5 %: the Sun and Moon pull too, and b is measured a million
    // km out, not infinitely far.)
    for (const SimResult& r : results) {
        const EncounterSummary& e = *r.encounter;
        const double k = kMuEarth / (e.arrivalSpeed * e.arrivalSpeed);
        const double q = k * (std::sqrt(1.0 + std::pow(e.impactParameter / k, 2.0)) - 1.0);
        CHECK(e.impactParameter > closest(r));
        if (r.outcome != Outcome::Impact) CHECK_THAT(closest(r), WithinRel(q, 0.05));
    }

    // The last three hit, though they were all aimed to miss the Earth:
    // anything aimed within R √(1 + v_esc²/v∞²) ≈ 2.2 R is caught.
    std::vector<Outcome> outcomes;
    outcomes.reserve(results.size());
    for (const SimResult& r : results) outcomes.push_back(r.outcome);
    CHECK(std::ranges::count(outcomes, Outcome::Impact) == 3);
    for (std::size_t i = outcomes.size() - 3; i < outcomes.size(); ++i) {
        CHECK(outcomes[i] == Outcome::Impact);
        CHECK(results[i].encounter->impactParameter > kRadiusEarth);
    }
}
