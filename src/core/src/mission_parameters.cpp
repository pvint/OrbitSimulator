// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/mission_parameters.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/rocket.hpp"

#include <array>

namespace orbit {

double launchHourOfDay(const Mission& mission) {
    const CalendarTime t = calendarFromEpoch(mission.launchEpoch);
    const double midnight = epochFromCalendar({t.year, t.month, t.day, 0, 0, 0.0});
    return (mission.launchEpoch - midnight) / 3600.0;
}

void setLaunchHourOfDay(Mission& mission, double hours) {
    const CalendarTime t = calendarFromEpoch(mission.launchEpoch);
    mission.launchEpoch = epochFromCalendar({t.year, t.month, t.day, 0, 0, 0.0}) + hours * 3600.0;
}

namespace {

// Each row: key, label, unit, group, help, min, max, step, get, set.
// The lambdas convert between display units and the physics' units.
constexpr std::array kParameters{
    ParameterInfo{"dry-mass", "Dry mass", "t", "Rocket", "Rocket mass without propellant", 0.5,
                  50.0, 0.1, [](const Mission& m) { return m.rocket.dryMass / 1000.0; },
                  [](Mission& m, double v) { m.rocket.dryMass = v * 1000.0; }},
    ParameterInfo{"propellant", "Propellant", "t", "Rocket", "Propellant mass at liftoff", 0.0,
                  200.0, 0.5, [](const Mission& m) { return m.rocket.propellantMass / 1000.0; },
                  [](Mission& m, double v) { m.rocket.propellantMass = v * 1000.0; }},
    ParameterInfo{"thrust", "Thrust", "kN", "Rocket", "Engine thrust (constant)", 50.0, 5000.0,
                  10.0, [](const Mission& m) { return m.rocket.thrust / 1000.0; },
                  [](Mission& m, double v) { m.rocket.thrust = v * 1000.0; }},
    ParameterInfo{"isp", "Specific impulse", "s", "Rocket",
                  "Engine efficiency: exhaust velocity = Isp · g0", 150.0, 500.0, 1.0,
                  [](const Mission& m) { return m.rocket.isp; },
                  [](Mission& m, double v) { m.rocket.isp = v; }},

    ParameterInfo{
        "burn-time", "Burn time", "s", "Rocket",
        "Sets the propellant so the engine can run this long at full thrust", 10.0, 1000.0, 1.0,
        [](const Mission& m) { return burnTime(m.rocket); },
        [](Mission& m, double v) { m.rocket.propellantMass = propellantForBurnTime(m.rocket, v); },
        true},

    ParameterInfo{"latitude", "Latitude", "°", "Launch site", "North positive", -90.0, 90.0, 0.5,
                  [](const Mission& m) { return radToDeg(m.site.latitude); },
                  [](Mission& m, double v) { m.site.latitude = degToRad(v); }},
    ParameterInfo{"longitude", "Longitude", "°", "Launch site", "East positive", -180.0, 180.0, 0.5,
                  [](const Mission& m) { return radToDeg(m.site.longitude); },
                  [](Mission& m, double v) { m.site.longitude = degToRad(v); }},
    ParameterInfo{"launch-hour", "Launch time", "h UTC", "Launch site",
                  "Time of day of liftoff (UTC, decimal hours). Sets where the Earth has "
                  "turned to, and so the orbit's orientation in space",
                  0.0, 23.95, 0.05, [](const Mission& m) { return launchHourOfDay(m); },
                  [](Mission& m, double v) { setLaunchHourOfDay(m, v); }, false, false},
    ParameterInfo{"azimuth", "Direction (azimuth)", "°", "Launch site",
                  "Clockwise from north: 90° = east, 270° = west", 0.0, 360.0, 1.0,
                  [](const Mission& m) { return radToDeg(m.guidance.azimuth); },
                  [](Mission& m, double v) { m.guidance.azimuth = degToRad(v); }},

    ParameterInfo{"pitch-start", "Pitch start", "s", "Pitch program",
                  "When the rocket starts to tilt over", 0.0, 60.0, 0.5,
                  [](const Mission& m) { return m.guidance.pitchStartTime; },
                  [](Mission& m, double v) { m.guidance.pitchStartTime = v; }},
    ParameterInfo{"kick-angle", "Kick angle", "°", "Pitch program",
                  "How far it tilts from vertical before the gravity turn", 0.0, 60.0, 0.25,
                  [](const Mission& m) { return radToDeg(m.guidance.kickAngle); },
                  [](Mission& m, double v) { m.guidance.kickAngle = degToRad(v); }},
    ParameterInfo{"kick-hold", "Kick hold", "s", "Pitch program", "How long the kick angle is held",
                  0.0, 60.0, 0.5, [](const Mission& m) { return m.guidance.kickHoldTime; },
                  [](Mission& m, double v) { m.guidance.kickHoldTime = v; }},

    ParameterInfo{"target-altitude", "Target altitude", "km", "Orbit insertion",
                  "Auto-circularize: cut the engine when apoapsis reaches this", 80.0, 2000.0, 5.0,
                  [](const Mission& m) { return m.targetAltitude / 1000.0; },
                  [](Mission& m, double v) { m.targetAltitude = v * 1000.0; }},
};

} // namespace

std::span<const ParameterInfo> missionParameters() { return kParameters; }

const ParameterInfo* findParameter(std::string_view key) {
    for (const ParameterInfo& p : kParameters) {
        if (p.key == key) return &p;
    }
    return nullptr;
}

} // namespace orbit
