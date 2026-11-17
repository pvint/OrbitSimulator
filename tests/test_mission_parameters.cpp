// SPDX-License-Identifier: GPL-3.0-or-later
// The parameter table that the GUI and CLI are built from.

#include "orbit/core/mission_parameters.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <set>
#include <string_view>

using namespace orbit;
using Catch::Matchers::WithinAbs;

TEST_CASE("Every parameter writes and reads back the same display value", "[parameters]") {
    for (const ParameterInfo& p : missionParameters()) {
        CAPTURE(p.key);
        Mission m = defaultMission();
        const double value = p.min + 0.37 * (p.max - p.min);
        p.set(m, value);
        REQUIRE_THAT(p.get(m), WithinAbs(value, 1e-9 * (1.0 + std::abs(value))));
    }
}

TEST_CASE("Setting one parameter leaves the others alone", "[parameters]") {
    for (const ParameterInfo& changed : missionParameters()) {
        if (changed.derived) continue; // derived ones change others on purpose
        CAPTURE(changed.key);
        Mission m = defaultMission();
        changed.set(m, changed.min);
        for (const ParameterInfo& other : missionParameters()) {
            if (other.key == changed.key || other.derived) continue;
            CAPTURE(other.key);
            REQUIRE(other.get(m) == other.get(defaultMission()));
        }
    }
}

TEST_CASE("Defaults lie within the slider ranges", "[parameters]") {
    const Mission m = defaultMission();
    for (const ParameterInfo& p : missionParameters()) {
        CAPTURE(p.key, p.get(m));
        REQUIRE(p.min < p.max);
        REQUIRE(p.step > 0.0);
        REQUIRE(p.get(m) >= p.min);
        REQUIRE(p.get(m) <= p.max);
    }
}

TEST_CASE("Keys are unique and can be looked up", "[parameters]") {
    std::set<std::string_view> keys;
    for (const ParameterInfo& p : missionParameters()) {
        REQUIRE(keys.insert(p.key).second);
        REQUIRE(findParameter(p.key) == &p);
    }
    REQUIRE(findParameter("no-such-parameter") == nullptr);
    // Spot-check a unit conversion: 25° of kick is stored in radians.
    Mission m = defaultMission();
    findParameter("kick-angle")->set(m, 25.0);
    REQUIRE_THAT(m.guidance.kickAngle, WithinAbs(0.436332313, 1e-9));
}

TEST_CASE("Burn time is derived: setting it sets the propellant", "[parameters]") {
    const ParameterInfo* p = findParameter("burn-time");
    REQUIRE(p != nullptr);
    REQUIRE(p->derived);
    Mission m = defaultMission();
    p->set(m, 100.0);
    REQUIRE_THAT(burnTime(m.rocket), WithinAbs(100.0, 1e-9));
    REQUIRE_THAT(m.rocket.propellantMass, WithinAbs(100.0 * massFlowRate(m.rocket), 1e-6));
}
