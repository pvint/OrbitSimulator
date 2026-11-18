// SPDX-License-Identifier: GPL-3.0-or-later
// Flies one rocket through its flight program, from launch until it crashes,
// escapes, or the program ends.
//
// FRAME
// Earth-centred inertial (see earth.hpp). The launch site sits on the
// rotating Earth at the given latitude and longitude, so at liftoff the
// rocket already moves with the ground: ω × r, up to 465 m/s eastward.
//
// FORCES
//   Earth    a = −μ r / |r|³              (point mass)
//   Moon,    a = μ_b ((r_b − r)/|r_b − r|³ − r_b/|r_b|³)   for body b at r_b.
//   Sun      The second term is there because the frame's origin, the Earth,
//            is itself pulled towards the body; only the *difference* in pull
//            moves the rocket relative to the Earth.
//   thrust   a = F/m along the steering direction, while the engine runs
//   drag     a = −½ρ|v_air|·Cd·A·v_air / m, below 1000 km (aerodynamics.hpp)
//   mass     dm/dt = −F / (Isp·g0)
// F and Isp are those of the stage that is firing (rocket.hpp). When its
// propellant runs out it is dropped (its dry mass leaves the rocket) and the
// next stage takes over at once. Dropped stages are then flown on their own.
//
// FLIGHT PROGRAM
// The mission's flight plan becomes a list of burn and coast steps
// (flight_program.hpp), run in order. For a plain orbit:
//   Ascent             pitch program, until the apoapsis reaches the target altitude
//   Coast to apoapsis
//   Circularization    burn towards circular-orbit velocity (guidance.hpp)
//   Coast              one revolution
// The simulation ends when the program does, on impact (Earth or Moon), when
// an escaping rocket passes `escapeDistance`, or at `maxTime`.
#pragma once

#include "orbit/core/aerodynamics.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/flight_program.hpp"
#include "orbit/core/guidance.hpp"
#include "orbit/core/integrator.hpp"
#include "orbit/core/orbit.hpp"
#include "orbit/core/rocket.hpp"
#include "orbit/core/state.hpp"

#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace orbit {

struct LaunchSite {
    double latitude = 0.0;  ///< radians, north positive
    double longitude = 0.0; ///< radians, east positive
};

/// Settings for the lunar flight plans.
struct LunarTransfer {
    /// The trans-lunar injection burn raises the apoapsis to this altitude
    /// (m), a little beyond the Moon so the rocket meets it on the way out.
    double tliApoapsisAltitude = 450e6;
    /// Added to the automatically computed lead angle (radians). Sweep it to
    /// see how timing decides between hitting, orbiting and missing the Moon.
    double leadAngleAdjust = 0.0;
    /// Lunar orbit insertion brakes until the orbit round the Moon has its
    /// apoapsis no higher than this (m above the Moon's surface).
    double captureApoapsisAltitude = 10e6;
};

/// Everything that defines one flight.
struct Mission {
    Rocket rocket;
    LaunchSite site;
    AscentGuidance guidance;
    FlightPlan plan = FlightPlan::Orbit;
    double targetAltitude = 200e3; ///< m: the orbit (or lunar parking orbit) altitude
    LunarTransfer lunar;
    /// Liftoff time: seconds since J2000 (astro_time.hpp). It sets where the
    /// launch site is in inertial space (the Earth's rotation angle), and
    /// where the Sun and Moon are.
    double launchEpoch = 0.0;
};

/// The default rocket and flight from the prototype. It reaches a 200 km
/// circular orbit from Cape Canaveral, launched at Apollo 11's liftoff time
/// (1969-07-16 13:32 UTC).
Mission defaultMission();

/// Numerical settings. The defaults suit everything up to lunar distances.
struct SimOptions {
    double mu = kMuEarth;
    /// Include the Moon's and Sun's gravity. Off for idealised tests.
    bool thirdBodyGravity = true;
    Tolerances tolerances;
    double initialStep = 0.1;    ///< s
    double maxBurnStep = 2.0;    ///< s: keeps steering smooth during burns
    double maxCoastStep = 300.0; ///< s
    /// s between output samples during burns, and while coasting below
    /// 100 km with drag (so the dynamic pressure graph stays smooth)
    double burnOutputInterval = 1.0;
    /// While coasting, output samples are spaced by this fraction of √(r³/μ)
    /// (a circular period / 2π) around the Earth, or around the Moon when
    /// that is shorter, so each orbit gets about 1250 points.
    double coastOutputFraction = 0.005;
    double maxCoastOutputInterval = 600.0; ///< s
    double escapeDistance = 1.5e9;         ///< m (about four times the Moon's distance)
    double maxTime = 30.0 * 86400.0;       ///< s
    int maxSteps = 2'000'000;
    /// Start from this state instead of the launch pad (tests, and later
    /// scenarios that begin in orbit). Time 0 is still the launch epoch.
    std::optional<State> initialState;
};

enum class Outcome {
    Crashed,     ///< hit the ground without reaching space (100 km)
    Suborbital,  ///< reached space, but the orbit dips underground
    Orbit,       ///< the whole orbit stays above the surface
    Escape,      ///< energy ≥ 0: never comes back
    LunarImpact, ///< hit the Moon
    LunarOrbit,  ///< ended in orbit around the Moon
};

enum class EventType {
    EngineStart,         ///< a burn step begins
    EngineStop,          ///< a burn step ends as planned
    Burnout,             ///< the propellant ran out (in the last stage that can fire)
    StageSeparation,     ///< a stage ran dry and was dropped; the next one ignites
    EnterMoonSphere,     ///< entered the Moon's sphere of influence
    LeaveMoonSphere,     ///< left it again
    MoonClosestApproach, ///< closest point to the Moon
};

enum class EndReason {
    Impact,         ///< hit the Earth
    MoonImpact,     ///< hit the Moon
    ProgramDone,    ///< the flight program's last step ended
    EscapeDistance, ///< escaping, and far away
    MaxTime,
    MaxSteps,
};

/// The rocket's state at one moment, as stored for drawing and graphs.
struct Sample {
    double t = 0.0; ///< s since liftoff
    State state;
    bool burning = false; ///< is the engine firing from this sample onwards?

    double altitude() const;
    double speed() const;
};

struct FlightEvent {
    EventType type = EventType::Burnout;
    std::string description; ///< e.g. "Trans-lunar injection: engine off"
    Sample sample;
};

/// A stage after it was dropped: it coasts on by itself, usually falling back
/// to the ground downrange of the launch site.
struct SpentStage {
    int stage = 0;               ///< 0 = the first stage
    std::vector<Sample> samples; ///< from separation; times since liftoff
    /// When and how far from the launch site (m, along the ground) it came
    /// down, if it did before the main flight ended.
    std::optional<double> impactTime;
    double impactDistance = 0.0;
};

/// What happened near the Moon.
struct LunarSummary {
    bool enteredSphereOfInfluence = false;
    double closestApproachAltitude =
        std::numeric_limits<double>::infinity(); ///< m above the surface
    double closestApproachTime = 0.0;            ///< s after liftoff
    /// The orbit around the Moon at the end of the flight, if the rocket was
    /// then inside the Moon's sphere of influence.
    std::optional<OrbitalElements> finalOrbit;
};

struct SimResult {
    Mission mission;
    FlightProgram program;
    std::vector<Sample> samples;
    std::vector<FlightEvent> events;
    /// Where the engine last shut down: the start of the final coast.
    std::optional<Sample> cutoff;
    /// The orbit around the Earth: after `cutoff` (or at the end, if the engine
    /// never shut down, or if the Moon has since changed the orbit).
    OrbitalElements orbit;
    LunarSummary lunar;
    /// The dropped stages, in the order they were dropped.
    std::vector<SpentStage> spentStages;
    Outcome outcome = Outcome::Crashed;
    EndReason endReason = EndReason::MaxTime;
    double maxAltitude = 0.0; ///< m, highest sample
    double deltaVUsed = 0.0;  ///< m/s, from the propellant actually burnt
    /// m/s: speed taken away by drag on the way up, ∫ D/m dt while climbing
    /// (summed over the samples, so accurate to a few per cent).
    double dragLoss = 0.0;
    /// The sample with the highest dynamic pressure q = ½ρv² while climbing
    /// ("max-Q"), if the rocket felt any drag.
    std::optional<Sample> maxDynamicPressure;
    double propellantLeft = 0.0; ///< kg
    double duration = 0.0;       ///< s of simulated time
};

/// Fly the mission's flight plan.
SimResult simulate(const Mission& mission, const SimOptions& options = {});

/// Fly any flight program (the mission supplies the rocket, launch site,
/// pitch program and launch time). This is what scripting will call.
SimResult simulateProgram(const Mission& mission, const FlightProgram& program,
                          const SimOptions& options = {});

/// The air's effect on the rocket at this sample (aerodynamics.hpp): its
/// Mach number, dynamic pressure, drag, ...
Aerodynamics aerodynamicsAt(const SimResult& result, const Sample& sample);

/// The rocket's state at time t, interpolated linearly between samples
/// (clamped to the start and end of the flight). Samples are close enough
/// together that this is accurate to a few metres. Used for playback.
State stateAt(const SimResult& result, double t);

/// The same for any samples in time order (a dropped stage's, say).
State stateAt(std::span<const Sample> samples, double t);

std::string_view toString(Outcome outcome);
std::string_view toString(EventType type);
std::string_view toString(EndReason reason);

} // namespace orbit
