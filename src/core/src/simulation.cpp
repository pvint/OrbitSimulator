// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/simulation.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/earth.hpp"
#include "orbit/core/ephemeris.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>

namespace orbit {

double Sample::altitude() const { return norm(state.r) - kRadiusEarth; }

double Sample::speed() const { return norm(state.v); }

Mission defaultMission() {
    Mission m;
    m.rocket = {.dryMass = 4000.0, .propellantMass = 60000.0, .thrust = 1.2e6, .isp = 340.0};
    m.site = {.latitude = degToRad(28.5), .longitude = degToRad(-80.6)}; // Cape Canaveral
    m.guidance = {.azimuth = degToRad(90.0),
                  .pitchStartTime = 10.0,
                  .kickAngle = degToRad(25.0),
                  .kickHoldTime = 10.0};
    m.plan = FlightPlan::Orbit;
    m.targetAltitude = 200e3;
    m.launchEpoch = epochFromCalendar({1969, 7, 16, 13, 32, 0.0});
    return m;
}

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// Things that can happen in the middle of a step. Each has a function g of
/// time and state that goes from negative to positive at the moment it
/// happens, so it can be pinned down by bisection.
enum class Trigger {
    None,
    EarthImpact,
    MoonImpact,
    EnterMoonSphere,
    LeaveMoonSphere,
    EscapeDistance,
    StepEnd, ///< the current step's end condition
};

/// Angle in (−π, π].
double wrapPi(double angle) {
    const double wrapped = wrapTwoPi(angle);
    return wrapped > kPi ? wrapped - kTwoPi : wrapped;
}

/// One flight. Keeps the state of the run together so each part of the loop
/// can be its own small method.
class FlightSimulator {
public:
    FlightSimulator(const Mission& mission, FlightProgram program, const SimOptions& options)
        : m_mission(mission), m_program(std::move(program)), m_opt(options),
          m_mdot(massFlowRate(mission.rocket)),
          m_stepper([this](double t, const State& y) { return derivative(t, y); },
                    options.tolerances) {}

    SimResult run();

private:
    Mission m_mission;
    FlightProgram m_program;
    SimOptions m_opt;
    double m_mdot;
    DormandPrince45 m_stepper;

    // Read by derivative(); set before each integration step.
    bool m_engineOn = false;
    Steering m_steering = Steering::Prograde;
    GuidancePhase m_phase = GuidancePhase::Vertical;
    Vec3 m_launchPlaneNormal;

    // Progress through the program.
    std::size_t m_stepIndex = 0;
    double m_stepEndTime = kInfinity; ///< time limit of the current step
    bool m_propellantGone = false;
    bool m_done = false;

    double m_t = 0.0;
    State m_y;
    double m_nextOutputTime = 0.0;
    bool m_insideMoonSphere = false;
    SimResult m_result;

    const FlightStep& step() const { return m_program[m_stepIndex]; }

    // Physics
    Vec3 moonAt(double t) const { return moonPosition(m_mission.launchEpoch + t); }
    State derivative(double t, const State& y) const;
    Vec3 steeringDirection(double t, const State& y) const;

    // Steps
    void beginStep(std::size_t index);
    void endStep(bool burnedOut);
    bool endConditionHolds(double t, const State& y) const;

    // Triggers and output
    double gTrigger(Trigger trigger, double t, const State& y) const;
    std::vector<Trigger> activeTriggers() const;
    double findCrossing(Trigger trigger, double t0, double t1) const;
    double outputInterval(double t, const State& y) const;
    void emitOutputs(double tEnd);
    void pushSample(double t, const State& y, bool burning);
    void record(EventType type, std::string description);
    void finish(EndReason reason);
    void summarise();
};

// --- Physics -------------------------------------------------------------

/// Equations of motion: the rate of change of {r, v, m}.
State FlightSimulator::derivative(double t, const State& y) const {
    const double r = norm(y.r);
    Vec3 acceleration = (-m_opt.mu / (r * r * r)) * y.r;

    if (m_opt.thirdBodyGravity) {
        // A body at rb pulls the rocket towards it, and the Earth too; the
        // difference is what moves the rocket relative to the Earth.
        const auto pull = [&](const Vec3& rb, double mu) {
            const Vec3 toBody = rb - y.r;
            const double d = norm(toBody);
            const double rbNorm = norm(rb);
            return mu * (toBody / (d * d * d) - rb / (rbNorm * rbNorm * rbNorm));
        };
        const double epoch = m_mission.launchEpoch + t;
        acceleration += pull(moonPosition(epoch), kMuMoon);
        acceleration += pull(sunPosition(epoch), kMuSun);
    }

    double massRate = 0.0;
    if (m_engineOn) {
        acceleration += (m_mission.rocket.thrust / y.m) * steeringDirection(t, y);
        massRate = -m_mdot;
    }
    return {y.v, acceleration, massRate};
}

Vec3 FlightSimulator::steeringDirection(double t, const State& y) const {
    switch (m_steering) {
    case Steering::PitchProgram: {
        const Vec3 relativeVel = y.v - groundVelocity(y.r);
        return thrustDirection(m_phase, y.r, y.v, relativeVel, m_mission.guidance.kickAngle,
                               m_launchPlaneNormal, m_opt.mu);
    }
    case Steering::Circularize:
        return thrustDirection(GuidancePhase::Circularize, y.r, y.v, Vec3{},
                               m_mission.guidance.kickAngle, m_launchPlaneNormal, m_opt.mu);
    case Steering::Prograde:
        return normalized(y.v);
    case Steering::Retrograde:
        return -normalized(y.v);
    case Steering::MoonRetrograde:
        return -normalized(y.v - moonVelocity(m_mission.launchEpoch + t));
    }
    return normalized(y.v);
}

// --- Steps ----------------------------------------------------------------

/// Start step `index`, skipping steps that can't or needn't run: burns once
/// the propellant is gone, and steps whose end condition already holds.
void FlightSimulator::beginStep(std::size_t index) {
    for (m_stepIndex = index; m_stepIndex < m_program.size(); ++m_stepIndex) {
        const FlightStep& s = step();
        if (s.engineOn && m_propellantGone) continue;
        // Braking into lunar orbit only makes sense near the Moon.
        if (s.steering == Steering::MoonRetrograde && !m_insideMoonSphere) continue;
        if (s.end != StepEnd::Duration && s.end != StepEnd::Revolutions &&
            s.end != StepEnd::PropellantExhausted && endConditionHolds(m_t, m_y)) {
            continue;
        }

        m_engineOn = s.engineOn;
        m_steering = s.steering;
        m_stepEndTime = m_t + s.timeout;
        if (s.end == StepEnd::Duration) m_stepEndTime = std::min(m_stepEndTime, m_t + s.value);
        if (s.end == StepEnd::Revolutions) {
            const OrbitalElements el = elementsFromState(m_y.r, m_y.v, m_opt.mu);
            if (el.bound) m_stepEndTime = std::min(m_stepEndTime, m_t + s.value * el.period);
        }
        // Liftoff isn't an event; every later engine start is.
        if (s.engineOn && m_t > 0.0) record(EventType::EngineStart, s.name + ": engine on");
        // The output spacing depends on whether the engine runs.
        m_nextOutputTime = m_t + outputInterval(m_t, m_y);
        return;
    }
    // Ran off the end of the program.
    m_engineOn = false;
    finish(EndReason::ProgramDone);
}

/// The current step is over: note it and start the next one.
void FlightSimulator::endStep(bool burnedOut) {
    const FlightStep& s = step();
    if (burnedOut) {
        m_propellantGone = true;
        m_engineOn = false;
        record(EventType::Burnout, s.name + ": propellant exhausted");
        m_result.cutoff = m_result.samples.back();
    } else if (s.engineOn) {
        m_engineOn = false;
        record(EventType::EngineStop, s.name + ": engine off");
        m_result.cutoff = m_result.samples.back();
    }
    beginStep(m_stepIndex + 1);
}

bool FlightSimulator::endConditionHolds(double t, const State& y) const {
    switch (step().end) {
    case StepEnd::Apoapsis:
        return dot(y.r, y.v) <= 0.0; // not climbing: at or past apoapsis
    case StepEnd::Circular:
        return norm(circularizationDeltaV(y.r, y.v, downrangeDirection(y.r, m_launchPlaneNormal),
                                          m_opt.mu)) < 0.01;
    case StepEnd::ApoapsisAltitude:
    case StepEnd::MoonCapture:
        return gTrigger(Trigger::StepEnd, t, y) >= 0.0;
    case StepEnd::MoonAhead:
    case StepEnd::MoonClosestApproach:
    case StepEnd::PropellantExhausted:
    case StepEnd::Revolutions:
    case StepEnd::Duration:
        break;
    }
    return false;
}

// --- Triggers -------------------------------------------------------------

double FlightSimulator::gTrigger(Trigger trigger, double t, const State& y) const {
    switch (trigger) {
    case Trigger::EarthImpact:
        return kRadiusEarth - norm(y.r);
    case Trigger::MoonImpact:
        return kRadiusMoon - norm(y.r - moonAt(t));
    case Trigger::EnterMoonSphere:
        return kMoonSphereOfInfluence - norm(y.r - moonAt(t));
    case Trigger::LeaveMoonSphere:
        return norm(y.r - moonAt(t)) - kMoonSphereOfInfluence;
    case Trigger::EscapeDistance:
        return norm(y.r) - m_opt.escapeDistance;
    case Trigger::StepEnd:
        break;
    case Trigger::None:
        return -1.0;
    }

    const double value = step().value;
    switch (step().end) {
    case StepEnd::ApoapsisAltitude:
        return elementsFromState(y.r, y.v, m_opt.mu).apoapsis - (kRadiusEarth + value);
    case StepEnd::Apoapsis:
        return -dot(y.r, y.v); // climbing → falling
    case StepEnd::MoonAhead: {
        // Angle from the rocket to the Moon, measured forwards in the orbit
        // plane. It shrinks as the faster rocket catches up; the step ends
        // when it shrinks through `value`.
        const Vec3 h = normalized(cross(y.r, y.v));
        Vec3 moon = moonAt(t + step().lookAhead);
        moon -= dot(moon, h) * h;
        const double ahead = std::atan2(dot(h, cross(y.r, moon)), dot(y.r, moon));
        return -wrapPi(ahead - value);
    }
    case StepEnd::MoonClosestApproach: {
        const Vec3 rRel = y.r - moonAt(t);
        if (norm(rRel) > kMoonSphereOfInfluence) return -1.0;
        const Vec3 vRel = y.v - moonVelocity(m_mission.launchEpoch + t);
        return dot(rRel, vRel) / norm(rRel); // approaching → receding
    }
    case StepEnd::MoonCapture: {
        const Vec3 rRel = y.r - moonAt(t);
        const Vec3 vRel = y.v - moonVelocity(m_mission.launchEpoch + t);
        const OrbitalElements el = elementsFromState(rRel, vRel, kMuMoon, kRadiusMoon);
        // The apoapsis can't be lower than where the rocket already is: if the
        // target is below that, settle for an orbit whose high point is within
        // 20 % of the current distance.
        const double target = std::max(kRadiusMoon + value, 1.2 * norm(rRel));
        return target - el.apoapsis; // −∞ while not captured
    }
    case StepEnd::PropellantExhausted:
    case StepEnd::Circular:
    case StepEnd::Revolutions:
    case StepEnd::Duration:
        break;
    }
    return -1.0;
}

std::vector<Trigger> FlightSimulator::activeTriggers() const {
    std::vector<Trigger> triggers{Trigger::EarthImpact};
    if (m_opt.thirdBodyGravity) {
        triggers.push_back(Trigger::MoonImpact);
        triggers.push_back(m_insideMoonSphere ? Trigger::LeaveMoonSphere
                                              : Trigger::EnterMoonSphere);
    }
    if (!m_engineOn) triggers.push_back(Trigger::EscapeDistance);
    switch (step().end) {
    case StepEnd::ApoapsisAltitude:
    case StepEnd::Apoapsis:
    case StepEnd::MoonAhead:
    case StepEnd::MoonClosestApproach:
    case StepEnd::MoonCapture:
        triggers.push_back(Trigger::StepEnd);
        break;
    case StepEnd::PropellantExhausted:
    case StepEnd::Circular:
    case StepEnd::Revolutions:
    case StepEnd::Duration:
        break;
    }
    return triggers;
}

/// Time in [t0, t1] where g crosses zero, by bisection on the dense output of
/// the step just taken. Accurate to about a microsecond.
double FlightSimulator::findCrossing(Trigger trigger, double t0, double t1) const {
    double lo = t0;
    double hi = t1;
    for (int i = 0; i < 60 && hi - lo > 1e-7; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (gTrigger(trigger, mid, m_stepper.interpolate(mid)) > 0.0) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return hi;
}

// --- Output ---------------------------------------------------------------

/// Output samples are spaced to keep curves smooth: every second during
/// burns, and a fixed fraction of the local orbital period while coasting,
/// around the Earth or the Moon, whichever is nearer in that sense.
double FlightSimulator::outputInterval(double t, const State& y) const {
    if (m_engineOn) return m_opt.burnOutputInterval;
    const double r = norm(y.r);
    double interval = m_opt.coastOutputFraction * std::sqrt(r * r * r / m_opt.mu);
    if (m_opt.thirdBodyGravity) {
        const double d = norm(y.r - moonAt(t));
        interval = std::min(interval, m_opt.coastOutputFraction * std::sqrt(d * d * d / kMuMoon));
    }
    return std::clamp(interval, m_opt.burnOutputInterval, m_opt.maxCoastOutputInterval);
}

void FlightSimulator::pushSample(double t, const State& y, bool burning) {
    m_result.samples.push_back({t, y, burning});
    if (m_opt.thirdBodyGravity) {
        const double altitude = norm(y.r - moonAt(t)) - kRadiusMoon;
        if (altitude < m_result.lunar.closestApproachAltitude) {
            m_result.lunar.closestApproachAltitude = altitude;
            m_result.lunar.closestApproachTime = t;
        }
    }
}

/// Store samples at the regular output times that fall before tEnd, using
/// the dense output of the step just taken.
void FlightSimulator::emitOutputs(double tEnd) {
    while (m_nextOutputTime < tEnd) {
        const State y = m_stepper.interpolate(m_nextOutputTime);
        pushSample(m_nextOutputTime, y, m_engineOn);
        m_nextOutputTime += outputInterval(m_nextOutputTime, y);
    }
}

/// Store the current moment as an event (and as a sample).
void FlightSimulator::record(EventType type, std::string description) {
    pushSample(m_t, m_y, m_engineOn);
    m_result.events.push_back({type, std::move(description), m_result.samples.back()});
}

void FlightSimulator::finish(EndReason reason) {
    m_done = true;
    m_result.endReason = reason;
    if (m_result.samples.empty() || m_result.samples.back().t < m_t) pushSample(m_t, m_y, false);
}

// --- The main loop -----------------------------------------------------------

SimResult FlightSimulator::run() {
    const Rocket& rocket = m_mission.rocket;
    const double m0 = rocket.dryMass + rocket.propellantMass;
    m_propellantGone = rocket.propellantMass <= 0.0 || rocket.thrust <= 0.0;

    // On the pad: at the launch site, moving with the ground (unless told
    // to start somewhere else).
    const Vec3 padEcef =
        positionFromLatLon(m_mission.site.latitude, m_mission.site.longitude, kRadiusEarth);
    const Vec3 pad = rotateZ(padEcef, earthRotationAngleAt(m_mission.launchEpoch));
    m_y = m_opt.initialState.value_or(State{pad, groundVelocity(pad), m0});
    m_launchPlaneNormal = launchPlaneNormal(pad, m_mission.guidance.azimuth);
    m_result.mission = m_mission;
    m_result.program = m_program;
    m_insideMoonSphere =
        m_opt.thirdBodyGravity && norm(m_y.r - moonAt(0.0)) < kMoonSphereOfInfluence;
    m_result.lunar.enteredSphereOfInfluence = m_insideMoonSphere;
    pushSample(0.0, m_y, false);

    beginStep(0);
    m_result.samples.front().burning = m_engineOn;
    if (m_engineOn && m_result.cutoff) m_result.cutoff.reset();
    if (!m_engineOn) m_result.cutoff = m_result.samples.front();

    const std::array pitchChanges{m_mission.guidance.pitchStartTime,
                                  m_mission.guidance.pitchStartTime +
                                      m_mission.guidance.kickHoldTime};

    double h = m_opt.initialStep;
    for (int stepCount = 0; !m_done; ++stepCount) {
        if (stepCount >= m_opt.maxSteps) {
            finish(EndReason::MaxSteps);
            break;
        }
        if (m_t >= m_opt.maxTime) {
            finish(EndReason::MaxTime);
            break;
        }

        // --- Choose the step size --------------------------------------
        // Never step across a moment where the equations of motion change
        // abruptly (pitch program changes, burnout, a step's time limit), so
        // each integration step sees smooth physics.
        double hStep = std::min(h, m_engineOn ? m_opt.maxBurnStep : m_opt.maxCoastStep);
        double tLimit = std::min(m_opt.maxTime, m_stepEndTime);
        if (m_engineOn) tLimit = std::min(tLimit, m_t + (m_y.m - rocket.dryMass) / m_mdot);
        if (m_steering == Steering::PitchProgram && m_engineOn) {
            for (const double tc : pitchChanges) {
                if (tc > m_t + 1e-9) tLimit = std::min(tLimit, tc);
            }
        }
        hStep = std::min(hStep, tLimit - m_t);

        // Some burns aim at a velocity: circularizing (towards circular-orbit
        // velocity) and braking (towards zero speed relative to the Earth or
        // Moon). If the remaining Δv can be delivered within this step, make
        // it the burn's last step: past that point the steering direction
        // would flip round, which no integrator can follow.
        bool lastStepOfBurn = false;
        if (m_engineOn) {
            double remaining = kInfinity;
            if (step().end == StepEnd::Circular) {
                const Vec3 fallback = downrangeDirection(m_y.r, m_launchPlaneNormal);
                remaining = norm(circularizationDeltaV(m_y.r, m_y.v, fallback, m_opt.mu));
            } else if (m_steering == Steering::Retrograde) {
                remaining = norm(m_y.v);
            } else if (m_steering == Steering::MoonRetrograde) {
                remaining = norm(m_y.v - moonVelocity(m_mission.launchEpoch + m_t));
            }
            const double timeToFinish = remaining / (rocket.thrust / m_y.m);
            if (timeToFinish <= hStep) {
                hStep = timeToFinish;
                lastStepOfBurn = true;
            }
        }

        m_phase = pitchProgramPhase(m_t + hStep / 2.0, m_mission.guidance);

        // --- Take the step -----------------------------------------------
        const auto result = m_stepper.step(m_t, m_y, hStep);
        h = result.nextStep;
        if (!result.accepted) continue;
        double tEnd = m_t + hStep;

        // --- Did something happen during the step? -----------------------
        // Find the earliest trigger whose g changed sign, and cut the step there.
        Trigger fired = Trigger::None;
        for (const Trigger trigger : activeTriggers()) {
            const double g0 = gTrigger(trigger, m_t, m_y);
            const double g1 = gTrigger(trigger, m_t + hStep, result.y);
            // Impact uses g0 ≤ 0 so that a rocket sinking off the pad (g = 0
            // at the start) still counts.
            const bool crossed =
                trigger == Trigger::EarthImpact ? (g0 <= 0.0 && g1 > 0.0) : (g0 < 0.0 && g1 >= 0.0);
            if (!crossed) continue;
            const double tc = findCrossing(trigger, m_t, m_t + hStep);
            if (fired == Trigger::None || tc < tEnd) {
                tEnd = tc;
                fired = trigger;
            }
        }
        const State yEnd = fired == Trigger::None ? result.y : m_stepper.interpolate(tEnd);
        if (fired != Trigger::None) lastStepOfBurn = false; // cut short

        emitOutputs(tEnd);
        m_t = tEnd;
        m_y = yEnd;

        // --- Respond to what happened ------------------------------------
        switch (fired) {
        case Trigger::EarthImpact:
            // Land exactly on the surface (the interpolated point is within
            // a few mm of it).
            m_y.r = kRadiusEarth * normalized(m_y.r);
            pushSample(m_t, m_y, false);
            m_result.endReason = EndReason::Impact;
            m_done = true;
            continue;
        case Trigger::MoonImpact:
            pushSample(m_t, m_y, false);
            m_result.endReason = EndReason::MoonImpact;
            m_done = true;
            continue;
        case Trigger::EnterMoonSphere:
            m_insideMoonSphere = true;
            m_result.lunar.enteredSphereOfInfluence = true;
            record(EventType::EnterMoonSphere, "Entered the Moon's sphere of influence");
            continue;
        case Trigger::LeaveMoonSphere:
            m_insideMoonSphere = false;
            record(EventType::LeaveMoonSphere, "Left the Moon's sphere of influence");
            continue;
        case Trigger::EscapeDistance:
            if (elementsFromState(m_y.r, m_y.v, m_opt.mu).energy >= 0.0) {
                finish(EndReason::EscapeDistance);
            }
            continue;
        case Trigger::StepEnd:
            if (step().end == StepEnd::MoonClosestApproach) {
                record(EventType::MoonClosestApproach, "Closest approach to the Moon");
            }
            endStep(false);
            continue;
        case Trigger::None:
            break;
        }

        if (m_engineOn && m_y.m - rocket.dryMass <= 1e-9 * m0) {
            m_y.m = rocket.dryMass;
            endStep(true);
        } else if (lastStepOfBurn || m_t >= m_stepEndTime - 1e-9) {
            endStep(false);
        }
    }

    summarise();
    return std::move(m_result);
}

void FlightSimulator::summarise() {
    const Rocket& rocket = m_mission.rocket;
    const double m0 = rocket.dryMass + rocket.propellantMass;
    m_result.duration = m_t;
    m_result.propellantLeft = m_y.m - rocket.dryMass;
    m_result.deltaVUsed = exhaustVelocity(rocket.isp) * std::log(m0 / m_y.m);
    for (const Sample& s : m_result.samples) {
        m_result.maxAltitude = std::max(m_result.maxAltitude, s.altitude());
    }

    // The orbit around the Earth: from the last engine shutdown, unless the
    // Moon has bent the path since, in which case from the end.
    const bool useFinalState = !m_result.cutoff || m_result.lunar.enteredSphereOfInfluence;
    const State& coast = useFinalState ? m_y : m_result.cutoff->state;
    m_result.orbit = elementsFromState(coast.r, coast.v, m_opt.mu);

    if (m_opt.thirdBodyGravity && m_insideMoonSphere) {
        const double epoch = m_mission.launchEpoch + m_t;
        m_result.lunar.finalOrbit = elementsFromState(
            m_y.r - moonPosition(epoch), m_y.v - moonVelocity(epoch), kMuMoon, kRadiusMoon);
    }

    const bool impacted = m_result.endReason == EndReason::Impact;
    const auto& moonOrbit = m_result.lunar.finalOrbit;
    if (m_result.endReason == EndReason::MoonImpact) {
        m_result.outcome = Outcome::LunarImpact;
    } else if (moonOrbit && moonOrbit->bound && moonOrbit->periapsis > kRadiusMoon) {
        m_result.outcome = Outcome::LunarOrbit;
    } else if (!impacted && !m_result.orbit.bound) {
        m_result.outcome = Outcome::Escape;
    } else if (!impacted && m_result.orbit.periapsis > kRadiusEarth) {
        m_result.outcome = Outcome::Orbit;
    } else {
        m_result.outcome =
            m_result.maxAltitude >= kKarmanLine ? Outcome::Suborbital : Outcome::Crashed;
    }
}

} // namespace

SimResult simulate(const Mission& mission, const SimOptions& options) {
    return simulateProgram(mission, flightProgramFor(mission), options);
}

SimResult simulateProgram(const Mission& mission, const FlightProgram& program,
                          const SimOptions& options) {
    return FlightSimulator(mission, program, options).run();
}

State stateAt(const SimResult& result, double t) {
    const auto& samples = result.samples;
    if (samples.empty()) return {};
    if (t <= samples.front().t) return samples.front().state;
    if (t >= samples.back().t) return samples.back().state;
    // First sample later than t; the one before it is at or before t.
    const auto next = std::ranges::upper_bound(samples, t, {}, &Sample::t);
    const Sample& a = *std::prev(next);
    const Sample& b = *next;
    const double span = b.t - a.t;
    const double f = span > 0.0 ? (t - a.t) / span : 0.0;
    return a.state + f * (b.state - a.state);
}

std::string_view toString(Outcome outcome) {
    switch (outcome) {
    case Outcome::Crashed:
        return "crashed";
    case Outcome::Suborbital:
        return "suborbital";
    case Outcome::Orbit:
        return "orbit";
    case Outcome::Escape:
        return "escape";
    case Outcome::LunarImpact:
        return "lunar impact";
    case Outcome::LunarOrbit:
        return "lunar orbit";
    }
    return "?";
}

std::string_view toString(EventType type) {
    switch (type) {
    case EventType::EngineStart:
        return "engine start";
    case EventType::EngineStop:
        return "engine stop";
    case EventType::Burnout:
        return "propellant exhausted";
    case EventType::EnterMoonSphere:
        return "entered the Moon's sphere of influence";
    case EventType::LeaveMoonSphere:
        return "left the Moon's sphere of influence";
    case EventType::MoonClosestApproach:
        return "closest approach to the Moon";
    }
    return "?";
}

std::string_view toString(EndReason reason) {
    switch (reason) {
    case EndReason::Impact:
        return "impact";
    case EndReason::MoonImpact:
        return "lunar impact";
    case EndReason::ProgramDone:
        return "flight program complete";
    case EndReason::EscapeDistance:
        return "escape distance reached";
    case EndReason::MaxTime:
        return "time limit";
    case EndReason::MaxSteps:
        return "step limit";
    }
    return "?";
}

} // namespace orbit
