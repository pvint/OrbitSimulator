// SPDX-License-Identifier: GPL-3.0-or-later
// Sweeps and scenario files.

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/mission_parameters.hpp"
#include "orbit/core/scenario.hpp"
#include "orbit/core/sweep.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <stdexcept>

using namespace orbit;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

TEST_CASE("Sweep values are evenly spaced and include both ends", "[sweep]") {
    REQUIRE(sweepValues({"kick-angle", 10.0, 30.0, 5}) ==
            std::vector{10.0, 15.0, 20.0, 25.0, 30.0});
    REQUIRE(sweepValues({"kick-angle", 10.0, 30.0, 1}) == std::vector{10.0});
    // Downwards works too.
    REQUIRE(sweepValues({"latitude", 60.0, 0.0, 3}) == std::vector{60.0, 30.0, 0.0});
}

TEST_CASE("Sweep missions change only the swept parameter", "[sweep]") {
    const Mission base = defaultMission();
    const auto missions = sweepMissions(base, {"kick-angle", 10.0, 30.0, 3});
    REQUIRE(missions.size() == 3);
    REQUIRE_THAT(radToDeg(missions[1].guidance.kickAngle), WithinAbs(20.0, 1e-9));
    REQUIRE(missions[1].rocket.thrust == base.rocket.thrust);
    REQUIRE(missions[1].site.latitude == base.site.latitude);

    REQUIRE_THROWS_AS(sweepMissions(base, {"warp-factor", 1.0, 9.0, 3}), std::invalid_argument);
}

TEST_CASE("A scenario survives a round trip through JSON", "[scenario]") {
    Scenario s;
    s.name = "Test";
    s.description = "A scenario with \"quotes\" and ° signs";
    findParameter("kick-angle")->set(s.mission, 31.25);
    findParameter("latitude")->set(s.mission, -12.5);
    s.mission.plan = FlightPlan::LunarOrbit;
    s.sweep = SweepSpec{"thrust", 800.0, 2000.0, 7};

    const LoadedScenario loaded = scenarioFromJson(scenarioToJson(s));
    REQUIRE(loaded.warnings.empty());
    const Scenario& back = loaded.scenario;
    REQUIRE(back.name == s.name);
    REQUIRE(back.description == s.description);
    REQUIRE(back.mission.plan == FlightPlan::LunarOrbit);
    for (const ParameterInfo& p : missionParameters()) {
        CAPTURE(p.key);
        REQUIRE_THAT(p.get(back.mission), WithinAbs(p.get(s.mission), 1e-9));
    }
    REQUIRE(back.sweep.has_value());
    REQUIRE(back.sweep->parameter == "thrust");
    REQUIRE(back.sweep->steps == 7);
}

TEST_CASE("A minimal scenario fills in defaults, in display units", "[scenario]") {
    const LoadedScenario loaded = scenarioFromJson(R"({
        "format": "orbitsim-scenario", "version": 1,
        "mission": { "kick-angle": 30, "propellant": 50 }
    })");
    const Mission& m = loaded.scenario.mission;
    REQUIRE_THAT(radToDeg(m.guidance.kickAngle), WithinAbs(30.0, 1e-12));
    REQUIRE_THAT(m.rocket.propellantMass, WithinAbs(50000.0, 1e-9)); // tonnes → kg
    REQUIRE(m.rocket.thrust == defaultMission().rocket.thrust);
    REQUIRE_FALSE(loaded.scenario.sweep.has_value());
    REQUIRE(loaded.warnings.empty());
}

TEST_CASE("Unknown keys are warnings, not errors", "[scenario]") {
    const LoadedScenario loaded = scenarioFromJson(R"({
        "format": "orbitsim-scenario", "version": 1,
        "mission": { "warp-factor": 9 },
        "camera": {}
    })");
    REQUIRE(loaded.warnings.size() == 2);
    REQUIRE_THAT(loaded.warnings[0], ContainsSubstring("warp-factor"));
    REQUIRE_THAT(loaded.warnings[1], ContainsSubstring("camera"));
}

TEST_CASE("Broken or foreign files are refused with a clear message", "[scenario]") {
    const auto failsWith = [](std::string_view text, const char* expected) {
        CAPTURE(text);
        REQUIRE_THROWS_WITH(scenarioFromJson(text), ContainsSubstring(expected));
    };
    failsWith("{ not json", "not valid JSON");
    failsWith(R"([1, 2, 3])", "JSON object");
    failsWith(R"({"format": "something-else", "version": 1})", "not an Orbit Simulator scenario");
    failsWith(R"({"format": "orbitsim-scenario"})", "version");
    failsWith(R"({"format": "orbitsim-scenario", "version": 99})", "newer version");
    failsWith(R"({"format": "orbitsim-scenario", "version": 1, "mission": {"thrust": "lots"}})",
              "must be a number");
    failsWith(R"({"format": "orbitsim-scenario", "version": 1,
                  "sweep": {"parameter": "thrust", "from": 1, "to": 2, "steps": 2.5}})",
              "whole number");
    failsWith(R"({"format": "orbitsim-scenario", "version": 1,
                  "sweep": {"parameter": "nope", "from": 1, "to": 2, "steps": 2}})",
              "not a known parameter");
}

TEST_CASE("Every bundled example scenario loads cleanly and flies", "[scenario]") {
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(ORBIT_SCENARIO_DIR)) {
        if (entry.path().extension() != ".json") continue;
        CAPTURE(entry.path().filename().string());
        const LoadedScenario loaded = loadScenarioFile(entry.path());
        REQUIRE(loaded.warnings.empty());
        REQUIRE_FALSE(loaded.scenario.name.empty());
        REQUIRE_FALSE(loaded.scenario.description.empty());
        // Every flight in it at least lifts off (some crash on purpose: the
        // propellant sweep's heaviest rocket tips over at a few hundred metres).
        const auto missions = loaded.scenario.sweep
                                  ? sweepMissions(loaded.scenario.mission, *loaded.scenario.sweep)
                                  : std::vector{loaded.scenario.mission};
        for (const Mission& m : missions) REQUIRE(simulate(m).maxAltitude > 100.0);
        ++count;
    }
    REQUIRE(count >= 5);
}

TEST_CASE("Saved values are tidy, not full of conversion noise", "[scenario]") {
    Scenario s; // latitude 28.5° goes through radians and back
    const std::string json = scenarioToJson(s);
    REQUIRE_THAT(json, ContainsSubstring("\"latitude\": 28.5,"));
}

TEST_CASE("The launch moment is saved as UTC text and read back", "[scenario]") {
    Scenario s;
    s.mission.launchEpoch = *parseIsoUtc("2024-03-15T06:45:00Z");
    const std::string json = scenarioToJson(s);
    REQUIRE_THAT(json, ContainsSubstring(R"("launch-utc": "2024-03-15T06:45:00Z")"));
    REQUIRE_THAT(json, !ContainsSubstring("launch-hour")); // stored within launch-utc
    REQUIRE(scenarioFromJson(json).scenario.mission.launchEpoch == s.mission.launchEpoch);

    // launch-hour in a file adjusts the time of day, keeping the date.
    const LoadedScenario loaded = scenarioFromJson(R"({
        "format": "orbitsim-scenario", "version": 1,
        "mission": { "launch-utc": "2024-03-15T06:45:00Z", "launch-hour": 18.5 } })");
    REQUIRE(formatIsoUtc(loaded.scenario.mission.launchEpoch) == "2024-03-15T18:30:00Z");

    REQUIRE_THROWS_WITH(scenarioFromJson(R"({"format": "orbitsim-scenario", "version": 1,
                                             "mission": {"launch-utc": "soon"}})"),
                        ContainsSubstring("launch-utc"));
}

TEST_CASE("Older files with auto-circularize still load", "[scenario]") {
    const auto plan = [](const char* json) { return scenarioFromJson(json).scenario.mission.plan; };
    REQUIRE(plan(R"({"format": "orbitsim-scenario", "version": 1,
                    "mission": {"auto-circularize": false}})") == FlightPlan::BurnToDepletion);
    REQUIRE(plan(R"({"format": "orbitsim-scenario", "version": 1,
                    "mission": {"auto-circularize": true}})") == FlightPlan::Orbit);
    REQUIRE_THROWS_WITH(scenarioFromJson(R"({"format": "orbitsim-scenario", "version": 1,
                                            "mission": {"plan": "to-mars"}})"),
                        ContainsSubstring("mission.plan"));
}
