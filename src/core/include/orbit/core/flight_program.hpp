// SPDX-License-Identifier: GPL-3.0-or-later
// Flight programs: a mission as a list of steps.
//
// Each step either burns the engine or coasts, steers in some way, and ends
// when a condition is met: "burn prograde until the apoapsis reaches the
// Moon", "coast until closest approach to the Moon". The simulation runs the
// steps in order.
//
// A mission's *flight plan* (orbit, lunar flyby, ...) builds its program from
// the mission's settings (flightProgramFor). The Custom plan uses a program
// the user wrote (the maneuver editor); later, scripting will too.
//
// RULES
// - A stage running dry doesn't end a burn: it is dropped and the next one
//   carries on. Running out in the last stage that can fire ends any burn;
//   later burn steps are skipped, but later coasts still happen.
// - If a step's end condition already holds when it starts (the rocket is
//   already past apoapsis, say), it ends at once.
// - A step that takes longer than its timeout ends anyway, and the program
//   moves on. (A transfer that misses the Moon never reaches "closest
//   approach".)
#pragma once

#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace orbit {

struct Mission;

enum class Steering {
    PitchProgram,   ///< the ascent: vertical, kick, gravity turn (guidance.hpp)
    Prograde,       ///< along the velocity
    Retrograde,     ///< against the velocity
    Circularize,    ///< towards circular-orbit velocity (guidance.hpp)
    MoonRetrograde, ///< against the velocity relative to the Moon (to be captured)
    Normal,         ///< along the orbit's angular momentum r × v: tilts the orbit's plane
    Antinormal,     ///< the opposite way
    RadialOut,      ///< straight up, away from the Earth's centre
    RadialIn,       ///< straight down
    OrbitInsertion, ///< closed-loop ascent into a circular orbit at the step's value (guidance.hpp)
};

enum class StepEnd {
    PropellantExhausted, ///< burn until the tank is empty
    ApoapsisAltitude,    ///< until the apoapsis passes `value` m above the Earth (up or down)
    PeriapsisAltitude,   ///< until the periapsis passes `value` m above the Earth (up or down)
    Altitude,            ///< until the rocket's altitude passes `value` m (up or down)
    Apoapsis,            ///< until the rocket reaches apoapsis (stops climbing)
    Periapsis,           ///< until the rocket reaches periapsis (stops falling)
    Circular,            ///< until the orbit is circular (with Circularize steering)
    OrbitAt,             ///< until in a circular orbit; OrbitInsertion aims for `value` m up
    MoonAhead,           ///< until the Moon, `lookAhead` s from now, is `value` radians ahead
    MoonClosestApproach, ///< until the closest point to the Moon (inside its sphere of influence)
    MoonCapture,         ///< until bound to the Moon with apoapsis ≤ `value` m above it
                         ///< (or ≤ 1.2 × the current distance, if that is higher)
    Revolutions,         ///< coast `value` revolutions of the orbit at the step's start
    Duration,            ///< for `value` seconds
};

struct FlightStep {
    std::string name; ///< for people, e.g. "Trans-lunar injection"
    bool engineOn = false;
    Steering steering = Steering::Prograde;
    StepEnd end = StepEnd::Duration;
    double value = 0.0;
    double timeout = std::numeric_limits<double>::infinity(); ///< s
    /// For MoonAhead: use where the Moon will be this many seconds later
    /// (the time the rocket takes to get there), not where it is now.
    double lookAhead = 0.0;

    friend bool operator==(const FlightStep&, const FlightStep&) = default;
};

using FlightProgram = std::vector<FlightStep>;

/// The kinds of flight a Mission can describe.
enum class FlightPlan {
    Orbit,           ///< ascent, coast to apoapsis, circularize at the target altitude
    GuidedOrbit,     ///< gravity turn through the air, then closed-loop guidance into orbit
    BurnToDepletion, ///< ascent until the propellant runs out (a pure gravity turn)
    LunarFlyby,      ///< parking orbit, trans-lunar injection, past the Moon
    LunarOrbit,      ///< as a flyby, then brake into orbit around the Moon
    Custom,          ///< the mission's own program (Mission::program)
};

/// Stable names for scenario files and the CLI ("orbit", "lunar-flyby", ...).
std::string_view planKey(FlightPlan plan);
bool planFromKey(std::string_view key, FlightPlan& plan);
std::string_view planLabel(FlightPlan plan); ///< for people: "Lunar flyby"
inline constexpr std::array kAllPlans{FlightPlan::Orbit,           FlightPlan::GuidedOrbit,
                                      FlightPlan::BurnToDepletion, FlightPlan::LunarFlyby,
                                      FlightPlan::LunarOrbit,      FlightPlan::Custom};

// Names for steering modes and end conditions, for files and for people.
// Step values are stored in SI units (m, radians, s) but shown and saved in
// display units (km, degrees, s): see stepValueToDisplay().
inline constexpr std::array kAllSteerings{
    Steering::PitchProgram, Steering::Prograde,       Steering::Retrograde,
    Steering::Circularize,  Steering::MoonRetrograde, Steering::Normal,
    Steering::Antinormal,   Steering::RadialOut,      Steering::RadialIn};
inline constexpr std::array kAllStepEnds{StepEnd::PropellantExhausted,
                                         StepEnd::ApoapsisAltitude,
                                         StepEnd::PeriapsisAltitude,
                                         StepEnd::Altitude,
                                         StepEnd::Apoapsis,
                                         StepEnd::Periapsis,
                                         StepEnd::Circular,
                                         StepEnd::MoonAhead,
                                         StepEnd::MoonClosestApproach,
                                         StepEnd::MoonCapture,
                                         StepEnd::Revolutions,
                                         StepEnd::Duration};

std::string_view steeringKey(Steering steering);   ///< "prograde"
std::string_view steeringLabel(Steering steering); ///< "Prograde (along the velocity)"
bool steeringFromKey(std::string_view key, Steering& steering);
std::string_view stepEndKey(StepEnd end);   ///< "apoapsis-altitude"
std::string_view stepEndLabel(StepEnd end); ///< "Apoapsis altitude reaches"
bool stepEndFromKey(std::string_view key, StepEnd& end);
/// Does this end condition use the step's value? (Not "at apoapsis", say.)
bool stepEndHasValue(StepEnd end);
std::string_view stepEndUnit(StepEnd end); ///< display unit of the value: "km", "°", "s", ...
double stepValueToDisplay(StepEnd end, double value);
double stepValueFromDisplay(StepEnd end, double display);

/// The step list for a mission's flight plan.
FlightProgram flightProgramFor(const Mission& mission);

/// For lunar plans: how far ahead of the rocket the Moon is when the
/// trans-lunar injection burn starts (radians), so that they meet: the
/// classic "lead angle", about 110–130°. Includes the manual adjustment.
double leadAngle(const Mission& mission);

/// For lunar plans: how far round the transfer ellipse the rocket travels
/// before reaching the Moon's distance (radians, ν). The injection burn starts
/// when the Moon's position *at arrival* is this far ahead. Includes the
/// manual adjustment.
double lunarTravelAngle(const Mission& mission);

/// For lunar plans: how long (s) from the injection burn until the rocket
/// reaches the Moon's distance, on the transfer ellipse.
double lunarTransferTime(const Mission& mission);

} // namespace orbit
