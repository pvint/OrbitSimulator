// SPDX-License-Identifier: GPL-3.0-or-later
// Flies one rocket from launch until it crashes, escapes or completes its orbits.
//
// FRAME
// Earth-centred inertial (see earth.hpp). The launch site sits on the
// rotating Earth at the given latitude and longitude, so at liftoff the
// rocket already moves with the ground: ω × r, up to 465 m/s eastward.
//
// FORCES
//   gravity  a = −μ r / |r|³              (point-mass Earth)
//   thrust   a = F/m along the guidance direction, while the engine runs
//   mass     dm/dt = −F / (Isp·g0)
// No drag and no staging yet.
//
// FLIGHT SEQUENCE
// With autoCircularize off, the engine burns until the propellant runs out,
// following the pitch program (guidance.hpp), and then the rocket coasts.
// With it on:
//   ASCENT             pitch program, until the apoapsis reaches the target altitude
//   COAST_TO_APOAPSIS  engine off, climbing towards the high point
//   CIRCULARIZE        burn towards circular-orbit velocity (guidance.hpp)
//   COAST              engine off
// The engine also stops if the propellant runs out partway through.
//
// The simulation ends on impact, after `orbitsAfterCutoff` revolutions of
// coasting, when an escaping rocket passes `escapeDistance`, or at `maxTime`.
#pragma once

#include "orbit/core/constants.hpp"
#include "orbit/core/guidance.hpp"
#include "orbit/core/integrator.hpp"
#include "orbit/core/orbit.hpp"
#include "orbit/core/rocket.hpp"
#include "orbit/core/state.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace orbit {

struct LaunchSite {
    double latitude = 0.0;  ///< radians, north positive
    double longitude = 0.0; ///< radians, east positive
};

/// Everything that defines one flight.
struct Mission {
    Rocket rocket;
    LaunchSite site;
    AscentGuidance guidance;
    bool autoCircularize = true;
    double targetAltitude = 200e3; ///< m, used when autoCircularize is on
    /// Earth's rotation angle at liftoff (radians). Sets where the launch site
    /// is in inertial space. It will matter once the Moon is in the sim.
    double earthRotationAngleAtLaunch = 0.0;
};

/// The default rocket and flight from the prototype. It reaches a roughly
/// 190 × 200 km orbit from Cape Canaveral's latitude.
Mission defaultMission();

/// Numerical settings. The defaults suit everything up to lunar distances.
struct SimOptions {
    double mu = kMuEarth;
    Tolerances tolerances;
    double initialStep = 0.1;        ///< s
    double maxBurnStep = 2.0;        ///< s: keeps steering smooth during burns
    double maxCoastStep = 300.0;     ///< s
    double burnOutputInterval = 1.0; ///< s between output samples during burns
    /// While coasting, output samples are spaced by this fraction of √(r³/μ)
    /// (a circular period / 2π), so each orbit gets about 1250 points.
    double coastOutputFraction = 0.005;
    double maxCoastOutputInterval = 600.0; ///< s
    double orbitsAfterCutoff = 1.0;
    double escapeDistance = 10.0 * kRadiusEarth; ///< m
    double maxTime = 3.0 * 86400.0;              ///< s
    int maxSteps = 1'000'000;
};

enum class Outcome {
    Crashed,    ///< hit the ground without reaching space (100 km)
    Suborbital, ///< reached space, but the orbit dips underground
    Orbit,      ///< the whole orbit stays above the surface
    Escape,     ///< energy ≥ 0: never comes back
};

enum class EventType {
    Cutoff,               ///< engine cut because apoapsis reached the target
    CircularizationStart, ///< engine relit at apoapsis
    CircularizationEnd,   ///< orbit is circular, engine off
    Burnout,              ///< propellant ran out
};

enum class EndReason { Impact, OrbitsComplete, EscapeDistance, MaxTime, MaxSteps };

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
    Sample sample;
};

struct SimResult {
    Mission mission;
    std::vector<Sample> samples;
    std::vector<FlightEvent> events;
    /// Where the engine last shut down: the start of the final coast.
    std::optional<Sample> cutoff;
    /// The orbit the rocket coasts on after `cutoff` (or at the end, if the
    /// engine never shut down).
    OrbitalElements orbit;
    Outcome outcome = Outcome::Crashed;
    EndReason endReason = EndReason::MaxTime;
    double maxAltitude = 0.0;    ///< m, highest sample
    double deltaVUsed = 0.0;     ///< m/s, from the propellant actually burnt
    double propellantLeft = 0.0; ///< kg
    double duration = 0.0;       ///< s of simulated time
};

SimResult simulate(const Mission& mission, const SimOptions& options = {});

/// The rocket's state at time t, interpolated linearly between samples
/// (clamped to the start and end of the flight). Samples are close enough
/// together that this is accurate to a few metres. Used for playback.
State stateAt(const SimResult& result, double t);

std::string_view toString(Outcome outcome);
std::string_view toString(EventType type);
std::string_view toString(EndReason reason);

} // namespace orbit
