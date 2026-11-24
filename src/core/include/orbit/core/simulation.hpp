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
//   thrust   a = F/m along the steering direction, while the engine runs;
//            F = F_vac − p·A_exit is lower in thick air (rocket.hpp)
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
#include "orbit/core/natural_body.hpp"
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
    /// Start the injection burn this many seconds later than planned
    /// (negative: earlier). The same idea as leadAngleAdjust, in time: about
    /// 15 s per degree in a low parking orbit.
    double injectionDelay = 0.0;
    /// Lunar orbit insertion brakes until the orbit round the Moon has its
    /// apoapsis no higher than this (m above the Moon's surface).
    double captureApoapsisAltitude = 10e6;
};

/// Everything that defines one flight.
struct Mission {
    /// What flies. A natural body uses only `body`; everything else
    /// describes the rocket and its launch.
    Vehicle vehicle = Vehicle::Rocket;
    NaturalBody body = apophisFlyby();

    Rocket rocket;
    LaunchSite site;
    AscentGuidance guidance;
    FlightPlan plan = FlightPlan::Orbit;
    /// The steps to fly when plan is Custom (the maneuver editor's program).
    /// Other plans build theirs from the settings (flightProgramFor).
    FlightProgram program;
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
    /// A natural body's flight stops beyond this distance from the Earth (m),
    /// about seven times the reach of the Earth's gravity against the Sun's
    /// (its Hill sphere). Further out, the path is better seen around the Sun.
    double bodyDistanceLimit = 1e10;
    /// s: the most time between a natural body's samples. Far from the Earth
    /// its path is nearly straight, so it can be longer than a rocket's.
    double maxBodyOutputInterval = 3600.0;
    double maxTime = 30.0 * 86400.0; ///< s
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
    // Natural bodies (they also use Orbit, Escape and LunarImpact)
    Impact,           ///< hit the Earth
    Flyby,            ///< passed by, never bound to the Earth
    TemporaryCapture, ///< bound to the Earth for part of the time: a "mini-moon"
};

enum class EventType {
    EngineStart,         ///< a burn step begins
    EngineStop,          ///< a burn step ends as planned
    Burnout,             ///< the propellant ran out (in the last stage that can fire)
    StageSeparation,     ///< a stage ran dry and was dropped; the next one ignites
    EnterMoonSphere,     ///< entered the Moon's sphere of influence
    LeaveMoonSphere,     ///< left it again
    MoonClosestApproach, ///< closest point to the Moon
    // Natural bodies
    Epoch,                ///< where the given elements or state hold
    EarthClosestApproach, ///< a periapsis: the closest point to the Earth on this pass
    EarthCapture,         ///< energy relative to the Earth turns negative: bound
    EarthRelease,         ///< and turns positive again: free
};

enum class EndReason {
    Impact,         ///< hit the Earth
    MoonImpact,     ///< hit the Moon
    ProgramDone,    ///< the flight program's last step ended
    EscapeDistance, ///< escaping, and far away
    MaxTime,
    MaxSteps,
    SpanEnd,       ///< a natural body: the end of its time span
    DistanceLimit, ///< a natural body: beyond SimOptions::bodyDistanceLimit
};

/// The rocket's state at one moment, as stored for drawing and graphs.
struct Sample {
    double t = 0.0; ///< s since liftoff
    State state;
    bool burning = false; ///< is the engine firing from this sample onwards?
    int stage = 0;        ///< the stage firing (or next to fire): 0 = the first

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

/// A natural body's passage by the Earth.
struct EncounterSummary {
    /// The closest point to the Earth's centre over the whole time span.
    Sample closest;
    /// Time (as in the samples) of the epoch, where the body was placed.
    double epochTime = 0.0;
    /// The orbit around the Earth at the start and at the end of the span:
    /// a hyperbola (e > 1) far from the Earth means a flyby, an ellipse a
    /// capture. With the Sun and Moon pulling too, these are snapshots.
    OrbitalElements initialOrbit;
    OrbitalElements finalOrbit;
    /// Speed far from the Earth on the way in and out, v∞ = √(2ε), if
    /// unbound (the hyperbolic excess speed; 0 if bound).
    double arrivalSpeed = 0.0;
    double departureSpeed = 0.0;
    /// The impact parameter b (m): how far from the Earth's centre the body
    /// would pass if the Earth's gravity didn't bend its path, b = h / v∞
    /// (h, the angular momentum per kg, is r·v_sideways, and far away all
    /// of v is v∞). Gravity pulls the path in, so the closest approach is
    /// nearer than b: q = (μ/v∞²)(√(1 + (b v∞²/μ)²) − 1). And a body hits
    /// the Earth if b < R √(1 + v_esc²/v∞²), more than the Earth's radius
    /// R: gravitational focusing. Infinite if it arrived bound.
    double impactParameter = std::numeric_limits<double>::infinity();
    /// s spent bound to the Earth (negative energy) within the span.
    double timeBound = 0.0;
};

struct SimResult {
    Mission mission;
    /// The epoch at t = 0 (s since J2000): the launch, or the start of a
    /// natural body's time span.
    double startEpoch = 0.0;
    /// A natural body's encounter with the Earth.
    std::optional<EncounterSummary> encounter;
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
    /// m/s: the speed the engine added, ∫ F/m dt. The rocket equation with
    /// the vacuum Isp, less the back-pressure loss.
    double deltaVUsed = 0.0;
    /// m/s: speed lost because the engine gives less thrust in thick air,
    /// ∫ (F_vac − F)/m dt (summed over the samples, so to a few m/s).
    double pressureLoss = 0.0;
    /// m/s: speed taken away by drag on the way up, ∫ D/m dt while climbing
    /// (summed over the samples, so accurate to a few per cent).
    double dragLoss = 0.0;
    /// The sample with the highest dynamic pressure q = ½ρv² while climbing
    /// ("max-Q"), if the rocket felt any drag.
    std::optional<Sample> maxDynamicPressure;
    double propellantLeft = 0.0; ///< kg
    double duration = 0.0;       ///< s of simulated time
};

/// Fly the mission's flight plan, or let its natural body coast.
SimResult simulate(const Mission& mission, const SimOptions& options = {});

/// A natural body's flight (natural_body_flight.cpp): from the epoch
/// backwards over the time span before it, then forwards over the span
/// after. Samples run from t = 0 at the start of the span.
SimResult simulateNaturalBody(const Mission& mission, const SimOptions& options = {});

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
