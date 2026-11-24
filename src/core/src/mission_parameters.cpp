// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/mission_parameters.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/rocket.hpp"

#include <algorithm>
#include <array>
#include <cmath>

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

double bodyEpochHourOfDay(const Mission& mission) {
    const CalendarTime t = calendarFromEpoch(mission.body.epoch);
    const double midnight = epochFromCalendar({t.year, t.month, t.day, 0, 0, 0.0});
    return (mission.body.epoch - midnight) / 3600.0;
}

void setBodyEpochHourOfDay(Mission& mission, double hours) {
    const CalendarTime t = calendarFromEpoch(mission.body.epoch);
    mission.body.epoch = epochFromCalendar({t.year, t.month, t.day, 0, 0, 0.0}) + hours * 3600.0;
}

namespace {

// Groups of a natural body's parameters, and which way of giving it each
// belongs to.
constexpr std::string_view kElementsGroup = "Orbit at closest approach";
constexpr std::string_view kStateGroup = "Position and velocity";

/// A natural body's parameter: no slider of its own is derived, all go in
/// scenario files (except the epoch's hour, saved within "epoch-utc").
constexpr ParameterInfo bodyParameter(std::string_view key, std::string_view label,
                                      std::string_view unit, std::string_view group,
                                      std::string_view help, double min, double max, double step,
                                      double (*get)(const Mission&), void (*set)(Mission&, double),
                                      bool inScenario = true) {
    return {key,
            label,
            unit,
            group,
            help,
            min,
            max,
            step,
            get,
            set,
            false,
            inScenario,
            Vehicle::NaturalBody};
}

// The state vector's components in km and km/s, one getter/setter per axis.
template <int Axis>
double& component(Vec3& v) {
    if constexpr (Axis == 0) {
        return v.x;
    } else if constexpr (Axis == 1) {
        return v.y;
    } else {
        return v.z;
    }
}
template <int Axis>
double bodyPosition(const Mission& m) {
    Vec3 r = m.body.position;
    return component<Axis>(r) / 1000.0;
}
template <int Axis>
void setBodyPosition(Mission& m, double v) {
    component<Axis>(m.body.position) = v * 1000.0;
}
template <int Axis>
double bodyVelocity(const Mission& m) {
    Vec3 v = m.body.velocity;
    return component<Axis>(v) / 1000.0;
}
template <int Axis>
void setBodyVelocity(Mission& m, double v) {
    component<Axis>(m.body.velocity) = v * 1000.0;
}

// Getters and setters for stage K's numbers, in display units (t, kN).
template <std::size_t K>
double stageDryMass(const Mission& m) {
    return m.rocket.stages[K].dryMass / 1000.0;
}
template <std::size_t K>
void setStageDryMass(Mission& m, double v) {
    m.rocket.stages[K].dryMass = v * 1000.0;
}
template <std::size_t K>
double stagePropellant(const Mission& m) {
    return m.rocket.stages[K].propellantMass / 1000.0;
}
template <std::size_t K>
void setStagePropellant(Mission& m, double v) {
    m.rocket.stages[K].propellantMass = v * 1000.0;
}
template <std::size_t K>
double stageThrust(const Mission& m) {
    return m.rocket.stages[K].thrust / 1000.0;
}
template <std::size_t K>
void setStageThrust(Mission& m, double v) {
    m.rocket.stages[K].thrust = v * 1000.0;
}
template <std::size_t K>
double stageIsp(const Mission& m) {
    return m.rocket.stages[K].isp;
}
template <std::size_t K>
void setStageIsp(Mission& m, double v) {
    m.rocket.stages[K].isp = v;
}
template <std::size_t K>
double stageSeaLevelLoss(const Mission& m) {
    return m.rocket.stages[K].seaLevelLoss * 100.0;
}
template <std::size_t K>
void setStageSeaLevelLoss(Mission& m, double v) {
    m.rocket.stages[K].seaLevelLoss = v / 100.0;
}

// Each row: key, label, unit, group, help, min, max, step, get, set.
// The lambdas convert between display units and the physics' units.
constexpr std::array kParameters{
    ParameterInfo{"stages", "Stages", "", "Rocket",
                  "How many stages are stacked. Each is dropped when its propellant runs out, "
                  "and the one above fires",
                  1.0, kMaxStages, 1.0,
                  [](const Mission& m) { return static_cast<double>(m.rocket.stageCount); },
                  [](Mission& m, double v) {
                      m.rocket.stageCount =
                          std::clamp(static_cast<int>(std::lround(v)), 1, kMaxStages);
                  }},
    ParameterInfo{"payload", "Payload", "t", "Rocket",
                  "Carried on top of the last stage, all the way to the end", 0.0, 50.0, 0.1,
                  [](const Mission& m) { return m.rocket.payloadMass / 1000.0; },
                  [](Mission& m, double v) { m.rocket.payloadMass = v * 1000.0; }},

    // Stage 1 keeps the keys from before staging, so older files still mean
    // the same thing.
    ParameterInfo{"stage-coast", "Coast between stages", "s", "Rocket",
                  "Time between dropping an empty stage and lighting the next, so they drift "
                  "apart first. The rocket falls a little meanwhile",
                  0.0, 30.0, 0.5, [](const Mission& m) { return m.rocket.separationCoast; },
                  [](Mission& m, double v) { m.rocket.separationCoast = v; }},
    ParameterInfo{"dry-mass", "Dry mass", "t", "Stage 1",
                  "Tanks, structure and engine, without propellant", 0.5, 50.0, 0.1,
                  stageDryMass<0>, setStageDryMass<0>},
    ParameterInfo{"propellant", "Propellant", "t", "Stage 1", "Propellant mass at liftoff", 0.0,
                  200.0, 0.5, stagePropellant<0>, setStagePropellant<0>},
    ParameterInfo{"thrust", "Thrust (vacuum)", "kN", "Stage 1",
                  "Engine thrust in a vacuum; a little less low down, where the air presses on "
                  "the nozzle",
                  50.0, 5000.0, 10.0, stageThrust<0>, setStageThrust<0>},
    ParameterInfo{"isp", "Isp (vacuum)", "s", "Stage 1",
                  "Specific impulse in a vacuum, the engine's efficiency: exhaust velocity = "
                  "Isp · g0",
                  150.0, 500.0, 1.0, stageIsp<0>, setStageIsp<0>},
    ParameterInfo{"sea-level-loss", "Sea-level loss", "%", "Stage 1",
                  "Thrust lost at sea level, where the air presses on the nozzle; less as the "
                  "air thins. Merlin 1D: 9 %, Space Shuttle main engine: 19 %",
                  0.0, 40.0, 0.5, stageSeaLevelLoss<0>, setStageSeaLevelLoss<0>},
    ParameterInfo{"burn-time", "Burn time", "s", "Stage 1",
                  "Sets the propellant so the engine can run this long at full thrust", 10.0,
                  1000.0, 1.0, [](const Mission& m) { return burnTime(m.rocket.stages[0]); },
                  [](Mission& m, double v) {
                      m.rocket.stages[0].propellantMass =
                          propellantForBurnTime(m.rocket.stages[0], v);
                  },
                  true},

    ParameterInfo{"stage-2-dry-mass", "Dry mass", "t", "Stage 2",
                  "Tanks, structure and engine, without propellant", 0.1, 20.0, 0.1,
                  stageDryMass<1>, setStageDryMass<1>},
    ParameterInfo{"stage-2-propellant", "Propellant", "t", "Stage 2", "Propellant mass when full",
                  0.0, 100.0, 0.1, stagePropellant<1>, setStagePropellant<1>},
    ParameterInfo{"stage-2-thrust", "Thrust (vacuum)", "kN", "Stage 2",
                  "Engine thrust in a vacuum; a little less low down, where the air presses on "
                  "the nozzle",
                  5.0, 2000.0, 5.0, stageThrust<1>, setStageThrust<1>},
    ParameterInfo{"stage-2-isp", "Isp (vacuum)", "s", "Stage 2",
                  "Specific impulse in a vacuum, the engine's efficiency: exhaust velocity = "
                  "Isp · g0",
                  150.0, 500.0, 1.0, stageIsp<1>, setStageIsp<1>},
    ParameterInfo{"stage-2-sea-level-loss", "Sea-level loss", "%", "Stage 2",
                  "Thrust lost at sea level, where the air presses on the nozzle; less as the "
                  "air thins. Merlin 1D: 9 %, Space Shuttle main engine: 19 %",
                  0.0, 40.0, 0.5, stageSeaLevelLoss<1>, setStageSeaLevelLoss<1>},

    ParameterInfo{"stage-3-dry-mass", "Dry mass", "t", "Stage 3",
                  "Tanks, structure and engine, without propellant", 0.1, 20.0, 0.1,
                  stageDryMass<2>, setStageDryMass<2>},
    ParameterInfo{"stage-3-propellant", "Propellant", "t", "Stage 3", "Propellant mass when full",
                  0.0, 100.0, 0.1, stagePropellant<2>, setStagePropellant<2>},
    ParameterInfo{"stage-3-thrust", "Thrust (vacuum)", "kN", "Stage 3",
                  "Engine thrust in a vacuum; a little less low down, where the air presses on "
                  "the nozzle",
                  5.0, 2000.0, 5.0, stageThrust<2>, setStageThrust<2>},
    ParameterInfo{"stage-3-isp", "Isp (vacuum)", "s", "Stage 3",
                  "Specific impulse in a vacuum, the engine's efficiency: exhaust velocity = "
                  "Isp · g0",
                  150.0, 500.0, 1.0, stageIsp<2>, setStageIsp<2>},
    ParameterInfo{"stage-3-sea-level-loss", "Sea-level loss", "%", "Stage 3",
                  "Thrust lost at sea level, where the air presses on the nozzle; less as the "
                  "air thins. Merlin 1D: 9 %, Space Shuttle main engine: 19 %",
                  0.0, 40.0, 0.5, stageSeaLevelLoss<2>, setStageSeaLevelLoss<2>},

    ParameterInfo{"drag-coefficient", "Drag coefficient", "", "Aerodynamics",
                  "Cd below the speed of sound: how streamlined the rocket is. It nearly "
                  "doubles around Mach 1. 0 = no drag, as in a vacuum",
                  0.0, 1.5, 0.01, [](const Mission& m) { return m.rocket.dragCoefficient; },
                  [](Mission& m, double v) { m.rocket.dragCoefficient = v; }},
    ParameterInfo{"diameter", "Diameter", "m", "Aerodynamics",
                  "Rocket body diameter. Drag grows with the frontal area, π·d²/4", 0.5, 10.0, 0.1,
                  [](const Mission& m) { return m.rocket.diameter; },
                  [](Mission& m, double v) { m.rocket.diameter = v; }},

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
                  "Clockwise from north: 90° = east, 270° = west", 0.0, 360.0, 0.1,
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
                  "Altitude of the circular orbit (the parking orbit, for trips to the Moon)", 80.0,
                  2000.0, 5.0, [](const Mission& m) { return m.targetAltitude / 1000.0; },
                  [](Mission& m, double v) { m.targetAltitude = v * 1000.0; }},

    ParameterInfo{"tli-apoapsis", "TLI apoapsis", "km", "Lunar transfer",
                  "Trans-lunar injection raises the apoapsis to this altitude, a little "
                  "beyond the Moon (about 380,000 km away)",
                  200000.0, 1000000.0, 1000.0,
                  [](const Mission& m) { return m.lunar.tliApoapsisAltitude / 1000.0; },
                  [](Mission& m, double v) { m.lunar.tliApoapsisAltitude = v * 1000.0; }},
    ParameterInfo{"lead-angle-adjust", "Lead angle adjust", "°", "Lunar transfer",
                  "Added to the computed angle the Moon should be ahead at injection. "
                  "Sweep it to see timing decide between hitting, orbiting and missing",
                  -45.0, 45.0, 0.1,
                  [](const Mission& m) { return radToDeg(m.lunar.leadAngleAdjust); },
                  [](Mission& m, double v) { m.lunar.leadAngleAdjust = degToRad(v); }},
    ParameterInfo{"tli-delay", "TLI start delay", "s", "Lunar transfer",
                  "Start the trans-lunar injection burn this many seconds later than planned "
                  "(negative: earlier). Sweep it to see how timing decides where the rocket "
                  "meets the Moon: about 15 s per degree of lead angle",
                  -600.0, 600.0, 1.0, [](const Mission& m) { return m.lunar.injectionDelay; },
                  [](Mission& m, double v) { m.lunar.injectionDelay = v; }},
    ParameterInfo{"capture-apoapsis", "Capture apoapsis", "km", "Lunar transfer",
                  "Lunar orbit insertion brakes until the orbit round the Moon reaches "
                  "no higher than this above its surface",
                  100.0, 60000.0, 100.0,
                  [](const Mission& m) { return m.lunar.captureApoapsisAltitude / 1000.0; },
                  [](Mission& m, double v) { m.lunar.captureApoapsisAltitude = v * 1000.0; }},

    // --- Natural bodies (natural_body.hpp) ---------------------------------
    bodyParameter(
        "periapsis-distance", "Periapsis distance", "km", kElementsGroup,
        "Closest approach, from the Earth's centre, at the epoch. The Earth's radius is "
        "6,371 km: closer than that is an impact",
        1.0, 2000000.0, 1.0, [](const Mission& m) { return m.body.periapsisDistance / 1000.0; },
        [](Mission& m, double v) { m.body.periapsisDistance = v * 1000.0; }),
    bodyParameter(
        "eccentricity", "Eccentricity", "", kElementsGroup,
        "0 = a circle, below 1 = an ellipse (bound to the Earth), above 1 = a "
        "hyperbola: it comes from far away and leaves again",
        0.0, 30.0, 0.001, [](const Mission& m) { return m.body.eccentricity; },
        [](Mission& m, double v) { m.body.eccentricity = v; }),
    bodyParameter(
        "inclination", "Inclination", "°", kElementsGroup,
        "Tilt of the orbit against the equator (of J2000). Over 90° = moving "
        "westward, against the Earth's turning",
        0.0, 180.0, 0.01, [](const Mission& m) { return radToDeg(m.body.inclination); },
        [](Mission& m, double v) { m.body.inclination = degToRad(v); }),
    bodyParameter(
        "raan", "Ascending node (Ω)", "°", kElementsGroup,
        "Where the orbit crosses the equator going north, measured eastward from "
        "the vernal equinox (right ascension)",
        0.0, 360.0, 0.01, [](const Mission& m) { return radToDeg(m.body.raan); },
        [](Mission& m, double v) { m.body.raan = degToRad(v); }),
    bodyParameter(
        "argument-of-periapsis", "Argument of periapsis (ω)", "°", kElementsGroup,
        "Where the closest point lies, measured from the ascending node in the "
        "direction of motion",
        0.0, 360.0, 0.01, [](const Mission& m) { return radToDeg(m.body.argumentOfPeriapsis); },
        [](Mission& m, double v) { m.body.argumentOfPeriapsis = degToRad(v); }),

    bodyParameter("body-x", "x", "km", kStateGroup,
                  "Position at the epoch, towards the vernal equinox (J2000 equator, as JPL "
                  "Horizons gives it)",
                  -1e7, 1e7, 1.0, bodyPosition<0>, setBodyPosition<0>),
    bodyParameter("body-y", "y", "km", kStateGroup,
                  "Position at the epoch, 90° east of x in the equator's plane", -1e7, 1e7, 1.0,
                  bodyPosition<1>, setBodyPosition<1>),
    bodyParameter("body-z", "z", "km", kStateGroup, "Position at the epoch, towards the north pole",
                  -1e7, 1e7, 1.0, bodyPosition<2>, setBodyPosition<2>),
    bodyParameter("body-vx", "vx", "km/s", kStateGroup, "Velocity at the epoch, along x", -50.0,
                  50.0, 0.001, bodyVelocity<0>, setBodyVelocity<0>),
    bodyParameter("body-vy", "vy", "km/s", kStateGroup, "Velocity at the epoch, along y", -50.0,
                  50.0, 0.001, bodyVelocity<1>, setBodyVelocity<1>),
    bodyParameter("body-vz", "vz", "km/s", kStateGroup, "Velocity at the epoch, along z", -50.0,
                  50.0, 0.001, bodyVelocity<2>, setBodyVelocity<2>),

    bodyParameter(
        "epoch-hour", "Epoch time", "h UTC", "Epoch and time span",
        "Time of day (UTC, decimal hours) when the values above hold: for "
        "elements, the moment of closest approach. Sets where the Earth has turned "
        "to, and where the Moon is",
        0.0, 23.99, 0.01, [](const Mission& m) { return bodyEpochHourOfDay(m); },
        [](Mission& m, double v) { setBodyEpochHourOfDay(m, v); }, false),
    bodyParameter(
        "days-before", "Days before", "d", "Epoch and time span",
        "How far back from the epoch to follow the body", 0.0, 60.0, 0.1,
        [](const Mission& m) { return m.body.timeBefore / 86400.0; },
        [](Mission& m, double v) { m.body.timeBefore = v * 86400.0; }),
    bodyParameter(
        "days-after", "Days after", "d", "Epoch and time span",
        "How far on from the epoch to follow it", 0.0, 60.0, 0.1,
        [](const Mission& m) { return m.body.timeAfter / 86400.0; },
        [](Mission& m, double v) { m.body.timeAfter = v * 86400.0; }),
};

} // namespace

std::span<const ParameterInfo> missionParameters() { return kParameters; }

bool parameterInUse(const ParameterInfo& parameter, const Mission& mission) {
    if (parameter.vehicle != mission.vehicle) return false;
    if (parameter.group == kElementsGroup) return mission.body.given == BodyGiven::Elements;
    if (parameter.group == kStateGroup) return mission.body.given == BodyGiven::StateVector;
    return true;
}

std::string parameterTitle(const ParameterInfo& parameter) {
    const auto sameLabel = [&](const ParameterInfo& p) { return p.label == parameter.label; };
    if (std::ranges::count_if(kParameters, sameLabel) == 1) return std::string(parameter.label);
    // "Stage 2" and "Thrust (vacuum)" → "Stage 2: Thrust (vacuum)"
    return std::string(parameter.group) + ": " + std::string(parameter.label);
}

const ParameterInfo* findParameter(std::string_view key) {
    for (const ParameterInfo& p : kParameters) {
        if (p.key == key) return &p;
    }
    return nullptr;
}

} // namespace orbit
