// SPDX-License-Identifier: GPL-3.0-or-later
// Natural bodies around the Sun: two-body checks, moving a body between the
// Earth and the Sun, and real comets and asteroids against JPL Horizons.

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/natural_body.hpp"
#include "orbit/core/scenario.hpp"
#include "orbit/core/simulation.hpp"
#include "orbit/core/solar_system.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <filesystem>
#include <optional>

using namespace orbit;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kDay = 86400.0;
constexpr double kYear = 365.25 * kDay;

Mission sunMission(double q, double e, double inclinationDeg = 0.0) {
    Mission m = defaultMission();
    m.vehicle = Vehicle::NaturalBody;
    m.body.centre = BodyCentre::Sun;
    m.body.given = BodyGiven::Elements;
    m.body.periapsisDistance = q;
    m.body.eccentricity = e;
    m.body.inclination = degToRad(inclinationDeg);
    m.body.raan = 0.0;
    m.body.argumentOfPeriapsis = 0.0;
    m.body.epoch = epochFromCalendar({2030, 1, 1, 0, 0, 0.0});
    m.body.timeBefore = 0.0;
    m.body.timeAfter = kYear;
    return m;
}

Scenario example(const char* name) {
    return loadScenarioFile(std::filesystem::path(ORBIT_SCENARIO_DIR) / name).scenario;
}

/// When an event of this type first happens (as an epoch).
std::optional<double> firstEvent(const SimResult& r, EventType type) {
    const auto it = std::ranges::find(r.events, type, &FlightEvent::type);
    if (it == r.events.end()) return std::nullopt;
    return r.startEpoch + it->sample.t;
}

const SolarSummary::Approach& approach(const SolarSummary& s, Planet planet) {
    return s.approaches.at(static_cast<std::size_t>(planet));
}

} // namespace

TEST_CASE("With the Sun alone, an orbit is Kepler's ellipse", "[helio]") {
    // q = 0.6 au, e = 0.4: a = 1 au, so a period of one year, and the
    // aphelion at a(1 + e) = 1.4 au half a year after the perihelion.
    Mission m = sunMission(0.6 * kAstronomicalUnit, 0.4);
    m.body.timeAfter = 1.5 * kYear;
    SimOptions options;
    options.thirdBodyGravity = false;
    const SimResult r = simulate(m, options);

    REQUIRE(r.solar);
    CHECK(r.frame == ResultFrame::SunEclipticJ2000);
    CHECK(r.outcome == Outcome::Orbit);
    const double year = orbitalPeriod(kAstronomicalUnit, kMuSun);
    const std::optional<double> aphelion = firstEvent(r, EventType::Aphelion);
    REQUIRE(aphelion);
    CHECK_THAT(*aphelion - m.body.epoch, WithinAbs(year / 2.0, 1.0));
    const auto at = std::ranges::find(r.events, EventType::Aphelion, &FlightEvent::type);
    CHECK_THAT(norm(at->sample.state.r), WithinRel(1.4 * kAstronomicalUnit, 1e-8));
    // The perihelion at the start, then the next one a year later.
    CHECK(std::ranges::count(r.events, EventType::Perihelion, &FlightEvent::type) == 2);
    // (C++23 has find_last; this is C++20.)
    double lastPerihelion = 0.0;
    for (const FlightEvent& e : r.events) {
        if (e.type == EventType::Perihelion) lastPerihelion = e.sample.t;
    }
    CHECK_THAT(lastPerihelion, WithinAbs(year, 1.0));
    CHECK_THAT(r.solar->finalOrbit.semiMajorAxis, WithinRel(kAstronomicalUnit, 1e-8));
}

TEST_CASE("A body moved from the Earth to the Sun keeps its path", "[helio]") {
    NaturalBody body = apophisFlyby();
    const PositionVelocity before = bodyStateJ2000(body);
    setBodyCentre(body, BodyCentre::Sun);
    CHECK(body.centre == BodyCentre::Sun);
    CHECK(body.given == BodyGiven::StateVector);
    // About 1 au from the Sun, moving at about 30 km/s, like the Earth.
    CHECK_THAT(norm(body.position), WithinRel(kAstronomicalUnit, 0.02));
    CHECK_THAT(norm(body.velocity), WithinRel(30e3, 0.05));
    CHECK(body.timeBefore >= kYear);

    setBodyCentre(body, BodyCentre::Earth);
    CHECK(norm(body.position - before.r) < 1e-3);
    CHECK(norm(body.velocity - before.v) < 1e-9);
    CHECK(body.timeBefore <= 30.0 * kDay);
}

TEST_CASE("Apophis's 2029 flyby moves it outside the Earth's orbit", "[helio]") {
    // Apophis's geocentric state at closest approach, moved to the Sun and
    // flown a year either way, with every planet pulling.
    Mission m = defaultMission();
    m.vehicle = Vehicle::NaturalBody;
    m.body = apophisFlyby();
    setBodyCentre(m.body, BodyCentre::Sun);
    const SimResult r = simulate(m);
    REQUIRE(r.solar);

    // It still passes the Earth at 38,011 km.
    CHECK_THAT(approach(*r.solar, Planet::Earth).distance, WithinAbs(38011e3, 50e3));
    // JPL's orbit (Horizons, osculating, ecliptic J2000): before, on
    // 1 January 2028, a = 0.92237 au, e = 0.19119, i = 3.3412°; after, on
    // 29 June 2030, a = 1.10298 au, e = 0.18904, i = 2.2210°. From inside the
    // Earth's orbit (an Aten asteroid) to outside it (an Apollo).
    const OrbitalElements& in = r.solar->initialOrbit;
    const OrbitalElements& out = r.solar->finalOrbit;
    CHECK_THAT(in.semiMajorAxis / kAstronomicalUnit, WithinAbs(0.92237, 0.001));
    CHECK_THAT(in.eccentricity, WithinAbs(0.19119, 0.001));
    CHECK_THAT(radToDeg(in.inclination), WithinAbs(3.3412, 0.01));
    CHECK_THAT(out.semiMajorAxis / kAstronomicalUnit, WithinAbs(1.10298, 0.001));
    CHECK_THAT(out.eccentricity, WithinAbs(0.18904, 0.001));
    CHECK_THAT(radToDeg(out.inclination), WithinAbs(2.2210, 0.01));
}

TEST_CASE("Halley's comet passes the Earth in April 1986", "[helio][example]") {
    const SimResult r = simulate(example("halley-1986.json").mission);
    REQUIRE(r.solar);
    CHECK(r.outcome == Outcome::Orbit);
    // JPL: 0.4172 au on 10 April 1986, 22h UTC.
    const SolarSummary::Approach& earth = approach(*r.solar, Planet::Earth);
    CHECK_THAT(earth.distance / kAstronomicalUnit, WithinAbs(0.4172, 0.002));
    CHECK_THAT(r.startEpoch + earth.t,
               WithinAbs(epochFromCalendar({1986, 4, 10, 22, 0, 0.0}), 6.0 * 3600.0));
    // A 76-year orbit (JPL: a = 17.94 au, period 76.0 years in 1986).
    CHECK_THAT(r.solar->finalOrbit.period / kYear, WithinAbs(76.0, 1.0));
    // It goes round backwards: inclination over 90°.
    CHECK(radToDeg(r.solar->finalOrbit.inclination) > 160.0);
    // Out beyond Neptune's orbit at aphelion.
    CHECK(r.solar->finalOrbit.apoapsis > 30.0 * kAstronomicalUnit);
}

TEST_CASE("ʻOumuamua leaves the solar system", "[helio][example]") {
    const SimResult r = simulate(example("oumuamua-2017.json").mission);
    REQUIRE(r.solar);
    CHECK(r.outcome == Outcome::Escape);
    CHECK(r.solar->finalOrbit.eccentricity > 1.0);
    // JPL: closest to the Earth on 14 October 2017 at 0.1618 au.
    const SolarSummary::Approach& earth = approach(*r.solar, Planet::Earth);
    CHECK_THAT(earth.distance / kAstronomicalUnit, WithinAbs(0.1618, 0.003));
    CHECK_THAT(r.startEpoch + earth.t,
               WithinAbs(epochFromCalendar({2017, 10, 14, 14, 0, 0.0}), 12.0 * 3600.0));
    // Far from the Sun it still moves at about 26 km/s: v∞ = √(2ε).
    const double vInf = std::sqrt(2.0 * r.solar->finalOrbit.energy);
    CHECK_THAT(vInf, WithinAbs(26.3e3, 500.0));
}

TEST_CASE("Ceres goes round in 4.6 years", "[helio][example]") {
    const SimResult r = simulate(example("ceres-2024.json").mission);
    REQUIRE(r.solar);
    CHECK(r.outcome == Outcome::Orbit);
    CHECK_THAT(r.solar->finalOrbit.semiMajorAxis / kAstronomicalUnit, WithinAbs(2.7672, 0.003));
    CHECK_THAT(r.solar->finalOrbit.period / kYear, WithinAbs(4.60, 0.02));
}

TEST_CASE("Bodies can hit the Sun or a planet", "[helio]") {
    SECTION("a sungrazer that grazes too low") {
        // Perihelion 0.002 au = 300,000 km from the centre: inside the Sun.
        const SimResult r = simulate(sunMission(0.002 * kAstronomicalUnit, 0.999));
        CHECK(r.outcome == Outcome::Impact);
        REQUIRE(r.solar);
        CHECK(r.solar->impacted == "the Sun");
        CHECK_THAT(norm(r.samples.back().state.r), WithinRel(kRadiusSun, 1e-6));
    }
    SECTION("a body aimed at Jupiter") {
        // 2 million km from Jupiter, falling straight at it at 10 km/s.
        Mission m = sunMission(kAstronomicalUnit, 0.0);
        const double epoch = m.body.epoch;
        const Vec3 jupiter = planetPosition(Planet::Jupiter, epoch);
        const Vec3 out = normalized(jupiter);
        m.body.given = BodyGiven::StateVector;
        m.body.position = jupiter + 2.0e9 * out;
        m.body.velocity = planetVelocity(Planet::Jupiter, epoch) - 10e3 * out;
        m.body.timeAfter = 30.0 * kDay;
        const SimResult r = simulate(m);
        CHECK(r.outcome == Outcome::Impact);
        REQUIRE(r.solar);
        CHECK(r.solar->impacted == "Jupiter");
        // Falling 2 million km, speeding up to over 60 km/s near Jupiter
        // (its escape speed): a little under two days.
        CHECK(r.duration < 2.5 * kDay);
    }
}

TEST_CASE("Sun-centred scenarios round-trip", "[helio][scenario]") {
    Scenario s;
    s.name = "Halley";
    s.mission = example("halley-1986.json").mission;
    const std::string json = scenarioToJson(s);
    CHECK(json.find("\"body-centre\": \"sun\"") != std::string::npos);
    CHECK(json.find("\"perihelion-distance\": 0.5871034") != std::string::npos);
    CHECK(json.find("\"periapsis-distance\"") == std::string::npos);
    const Mission back = scenarioFromJson(json).scenario.mission;
    CHECK(back.body.centre == BodyCentre::Sun);
    CHECK(back.body.periapsisDistance == s.mission.body.periapsisDistance);
}
