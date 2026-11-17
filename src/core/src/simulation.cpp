// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/simulation.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/earth.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
    m.autoCircularize = true;
    m.targetAltitude = 200e3;
    return m;
}

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

enum class FlightMode { Ascent, CoastToApoapsis, Circularize, Coast };

bool engineRunsIn(FlightMode mode) {
    return mode == FlightMode::Ascent || mode == FlightMode::Circularize;
}

/// Things that end a step early when they happen. Each has a function g of the
/// state that goes from negative to positive at the moment it happens.
enum class Trigger { None, Impact, ApoapsisAtTarget, Apoapsis, EscapeDistance };

/// One flight. Keeps the state of the run together so each part of the loop
/// can be its own small method.
class FlightSimulator {
public:
    FlightSimulator(const Mission& mission, const SimOptions& options)
        : m_mission(mission), m_opt(options), m_mdot(massFlowRate(mission.rocket)),
          m_stepper([this](double t, const State& y) { return derivative(t, y); },
                    options.tolerances) {}

    SimResult run();

private:
    Mission m_mission;
    SimOptions m_opt;
    double m_mdot;
    DormandPrince45 m_stepper;

    // Read by derivative(); set before each step.
    bool m_engineOn = false;
    Vec3 m_launchPlaneNormal;
    GuidancePhase m_phase = GuidancePhase::Vertical;

    FlightMode m_mode = FlightMode::Coast;
    double m_t = 0.0;
    State m_y;
    double m_nextOutputTime = 0.0;
    double m_coastEndTime = kInfinity;
    SimResult m_result;

    State derivative(double t, const State& y) const;
    double gTrigger(Trigger trigger, const State& y) const;
    std::vector<Trigger> activeTriggers() const;
    double findCrossing(Trigger trigger, double t0, double t1) const;
    double outputInterval(const State& y) const;
    void emitOutputs(double tEnd);
    void record(EventType type);
    void enterCoast();
    void finish(EndReason reason);
};

/// Equations of motion: the rate of change of {r, v, m}.
State FlightSimulator::derivative(double /*t*/, const State& y) const {
    const double r = norm(y.r);
    Vec3 acceleration = (-m_opt.mu / (r * r * r)) * y.r;
    double massRate = 0.0;
    if (m_engineOn) {
        const Vec3 relativeVel = y.v - groundVelocity(y.r);
        const Vec3 dir =
            thrustDirection(m_phase, y.r, y.v, relativeVel, m_mission.guidance.kickAngle,
                            m_launchPlaneNormal, m_opt.mu);
        acceleration += (m_mission.rocket.thrust / y.m) * dir;
        massRate = -m_mdot;
    }
    return {y.v, acceleration, massRate};
}

double FlightSimulator::gTrigger(Trigger trigger, const State& y) const {
    switch (trigger) {
    case Trigger::Impact:
        return kRadiusEarth - norm(y.r);
    case Trigger::ApoapsisAtTarget:
        return elementsFromState(y.r, y.v, m_opt.mu).apoapsis -
               (kRadiusEarth + m_mission.targetAltitude);
    case Trigger::Apoapsis:
        return -dot(y.r, y.v); // climbing → falling
    case Trigger::EscapeDistance:
        return norm(y.r) - m_opt.escapeDistance;
    case Trigger::None:
        break;
    }
    return -1.0;
}

std::vector<Trigger> FlightSimulator::activeTriggers() const {
    std::vector<Trigger> triggers{Trigger::Impact};
    if (m_mode == FlightMode::Ascent && m_mission.autoCircularize) {
        triggers.push_back(Trigger::ApoapsisAtTarget);
    }
    if (m_mode == FlightMode::CoastToApoapsis) triggers.push_back(Trigger::Apoapsis);
    if (m_mode == FlightMode::Coast) triggers.push_back(Trigger::EscapeDistance);
    return triggers;
}

/// Time in [t0, t1] where g crosses zero, by bisection on the dense output of
/// the step just taken. Accurate to about a microsecond.
double FlightSimulator::findCrossing(Trigger trigger, double t0, double t1) const {
    double lo = t0;
    double hi = t1;
    for (int i = 0; i < 60 && hi - lo > 1e-7; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (gTrigger(trigger, m_stepper.interpolate(mid)) > 0.0) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return hi;
}

double FlightSimulator::outputInterval(const State& y) const {
    if (engineRunsIn(m_mode)) return m_opt.burnOutputInterval;
    const double r = norm(y.r);
    const double interval = m_opt.coastOutputFraction * std::sqrt(r * r * r / m_opt.mu);
    return std::clamp(interval, m_opt.burnOutputInterval, m_opt.maxCoastOutputInterval);
}

/// Store samples at the regular output times that fall before tEnd, using
/// the dense output of the step just taken.
void FlightSimulator::emitOutputs(double tEnd) {
    while (m_nextOutputTime < tEnd) {
        const State y = m_stepper.interpolate(m_nextOutputTime);
        m_result.samples.push_back({m_nextOutputTime, y, engineRunsIn(m_mode)});
        m_nextOutputTime += outputInterval(y);
    }
}

/// Store the current moment as an event (and as a sample).
void FlightSimulator::record(EventType type) {
    const Sample sample{m_t, m_y, engineRunsIn(m_mode)};
    m_result.samples.push_back(sample);
    m_result.events.push_back({type, sample});
    if (type != EventType::CircularizationStart) m_result.cutoff = sample;
    // The output spacing depends on the mode, which has just changed.
    m_nextOutputTime = m_t + outputInterval(m_y);
}

/// Engine off for good: work out when `orbitsAfterCutoff` revolutions are done.
void FlightSimulator::enterCoast() {
    m_mode = FlightMode::Coast;
    const OrbitalElements el = elementsFromState(m_y.r, m_y.v, m_opt.mu);
    m_coastEndTime = el.bound ? m_t + m_opt.orbitsAfterCutoff * el.period : kInfinity;
}

void FlightSimulator::finish(EndReason reason) {
    m_result.endReason = reason;
    if (m_result.samples.empty() || m_result.samples.back().t < m_t) {
        m_result.samples.push_back({m_t, m_y, false});
    }
}

SimResult FlightSimulator::run() {
    const Rocket& rocket = m_mission.rocket;
    const double m0 = rocket.dryMass + rocket.propellantMass;
    const bool hasEngine = rocket.propellantMass > 0.0 && rocket.thrust > 0.0;

    // On the pad: at the launch site, moving with the ground.
    const Vec3 padEcef =
        positionFromLatLon(m_mission.site.latitude, m_mission.site.longitude, kRadiusEarth);
    m_y.r = rotateZ(padEcef, m_mission.earthRotationAngleAtLaunch);
    m_y.v = groundVelocity(m_y.r);
    m_y.m = m0;
    m_launchPlaneNormal = launchPlaneNormal(m_y.r, m_mission.guidance.azimuth);
    m_result.mission = m_mission;
    m_result.samples.push_back({0.0, m_y, hasEngine});

    m_mode = FlightMode::Ascent;
    if (!hasEngine) {
        m_result.cutoff = m_result.samples.front();
        enterCoast();
    }
    m_nextOutputTime = outputInterval(m_y);

    const std::array pitchChanges{m_mission.guidance.pitchStartTime,
                                  m_mission.guidance.pitchStartTime +
                                      m_mission.guidance.kickHoldTime};

    double h = m_opt.initialStep;
    bool done = false;
    for (int stepCount = 0; !done; ++stepCount) {
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
        // abruptly (pitch program changes, burnout, end of the coast), so
        // each step sees smooth physics.
        m_engineOn = engineRunsIn(m_mode);
        double hStep = std::min(h, m_engineOn ? m_opt.maxBurnStep : m_opt.maxCoastStep);
        double tLimit = m_opt.maxTime;
        if (m_engineOn) tLimit = std::min(tLimit, m_t + (m_y.m - rocket.dryMass) / m_mdot);
        if (m_mode == FlightMode::Ascent) {
            for (const double tc : pitchChanges) {
                if (tc > m_t + 1e-9) tLimit = std::min(tLimit, tc);
            }
        }
        if (m_mode == FlightMode::Coast) tLimit = std::min(tLimit, m_coastEndTime);
        hStep = std::min(hStep, tLimit - m_t);

        // Circularizing: if the remaining Δv can be delivered within this
        // step, make this the last step of the burn.
        bool lastCircularizationStep = false;
        if (m_mode == FlightMode::Circularize) {
            const Vec3 fallback = downrangeDirection(m_y.r, m_launchPlaneNormal);
            const double remaining = norm(circularizationDeltaV(m_y.r, m_y.v, fallback, m_opt.mu));
            const double timeToFinish = remaining / (rocket.thrust / m_y.m);
            if (timeToFinish <= hStep) {
                hStep = timeToFinish;
                lastCircularizationStep = true;
            }
        }

        m_phase = m_mode == FlightMode::Circularize
                      ? GuidancePhase::Circularize
                      : pitchProgramPhase(m_t + hStep / 2.0, m_mission.guidance);

        // --- Take the step -----------------------------------------------
        const auto step = m_stepper.step(m_t, m_y, hStep);
        if (!step.accepted) {
            h = step.nextStep;
            continue;
        }
        h = step.nextStep;
        double tEnd = m_t + hStep;
        State yEnd = step.y;

        // --- Did something happen during the step? -------------------------
        // Find the earliest trigger whose g changed sign, and cut the step there.
        Trigger fired = Trigger::None;
        for (const Trigger trigger : activeTriggers()) {
            const double g0 = gTrigger(trigger, m_y);
            const double g1 = gTrigger(trigger, step.y);
            // Impact uses g0 ≤ 0 so that a rocket sinking off the pad (g = 0
            // at the start) still counts.
            const bool crossed =
                trigger == Trigger::Impact ? (g0 <= 0.0 && g1 > 0.0) : (g0 < 0.0 && g1 >= 0.0);
            if (!crossed) continue;
            const double tc = findCrossing(trigger, m_t, m_t + hStep);
            if (tc < tEnd || fired == Trigger::None) {
                tEnd = tc;
                fired = trigger;
            }
        }
        if (fired != Trigger::None) {
            yEnd = m_stepper.interpolate(tEnd);
            lastCircularizationStep = false; // the step was cut short
        }

        emitOutputs(tEnd);
        m_t = tEnd;
        m_y = yEnd;

        // --- Respond to what happened ------------------------------------
        if (fired == Trigger::Impact) {
            // Land exactly on the surface (the interpolated point is within
            // a few mm of it).
            m_y.r = kRadiusEarth * normalized(m_y.r);
            m_result.samples.push_back({m_t, m_y, false});
            m_result.endReason = EndReason::Impact;
            done = true;
            continue;
        }

        if (m_engineOn && m_y.m - rocket.dryMass <= 1e-9 * m0 && fired == Trigger::None) {
            m_y.m = rocket.dryMass;
            m_mode = FlightMode::Coast;
            record(EventType::Burnout);
            enterCoast();
            continue;
        }

        switch (fired) {
        case Trigger::ApoapsisAtTarget:
            // Usually still climbing: coast up to apoapsis. If already past
            // it, start circularizing straight away.
            m_mode =
                dot(m_y.r, m_y.v) > 0.0 ? FlightMode::CoastToApoapsis : FlightMode::Circularize;
            record(EventType::Cutoff);
            if (m_mode == FlightMode::Circularize) record(EventType::CircularizationStart);
            continue;
        case Trigger::Apoapsis:
            m_mode = FlightMode::Circularize;
            record(EventType::CircularizationStart);
            continue;
        case Trigger::EscapeDistance:
            if (elementsFromState(m_y.r, m_y.v, m_opt.mu).energy >= 0.0) {
                finish(EndReason::EscapeDistance);
                done = true;
            }
            continue;
        case Trigger::Impact:
        case Trigger::None:
            break;
        }

        if (lastCircularizationStep) {
            m_mode = FlightMode::Coast;
            record(EventType::CircularizationEnd);
            enterCoast();
            continue;
        }

        if (m_mode == FlightMode::Coast && m_t >= m_coastEndTime - 1e-9) {
            finish(EndReason::OrbitsComplete);
            done = true;
        }
    }

    // --- Summarise -------------------------------------------------------
    const State& coastStart = m_result.cutoff ? m_result.cutoff->state : m_y;
    m_result.orbit = elementsFromState(coastStart.r, coastStart.v, m_opt.mu);
    m_result.duration = m_t;
    m_result.propellantLeft = m_y.m - rocket.dryMass;
    m_result.deltaVUsed = exhaustVelocity(rocket.isp) * std::log(m0 / m_y.m);
    for (const Sample& s : m_result.samples) {
        m_result.maxAltitude = std::max(m_result.maxAltitude, s.altitude());
    }

    const bool impacted = m_result.endReason == EndReason::Impact;
    if (!impacted && !m_result.orbit.bound) {
        m_result.outcome = Outcome::Escape;
    } else if (!impacted && m_result.orbit.periapsis > kRadiusEarth) {
        m_result.outcome = Outcome::Orbit;
    } else {
        m_result.outcome =
            m_result.maxAltitude >= kKarmanLine ? Outcome::Suborbital : Outcome::Crashed;
    }
    return std::move(m_result);
}

} // namespace

SimResult simulate(const Mission& mission, const SimOptions& options) {
    return FlightSimulator(mission, options).run();
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
    }
    return "?";
}

std::string_view toString(EventType type) {
    switch (type) {
    case EventType::Cutoff:
        return "engine cutoff (apoapsis at target)";
    case EventType::CircularizationStart:
        return "circularization burn start";
    case EventType::CircularizationEnd:
        return "circularization burn end";
    case EventType::Burnout:
        return "propellant exhausted";
    }
    return "?";
}

std::string_view toString(EndReason reason) {
    switch (reason) {
    case EndReason::Impact:
        return "impact";
    case EndReason::OrbitsComplete:
        return "orbits complete";
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
