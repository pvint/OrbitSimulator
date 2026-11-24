// SPDX-License-Identifier: GPL-3.0-or-later
// A natural body's flight: it coasts under gravity, nothing else.
//
// The body is placed at its epoch (natural_body.hpp), so the simulation
// runs in two passes from there: backwards in time over the span before the
// epoch, then forwards over the span after it. The integrator doesn't mind
// which way time runs: a negative step size integrates backwards, because
// gravity, like all of Newton's mechanics, works the same both ways.
// (Running the film backwards shows a body leaving along the path it came
// in on.)
//
// Along the way it watches for
//   impact            the distance from the Earth's centre falls to its radius
//   closest approach  r·v changes sign from − to +: the distance stops
//                     falling and starts rising (a periapsis)
//   capture/release   the energy relative to the Earth, ε = v²/2 − μ/r,
//                     changes sign. Below zero the body can't get away from
//                     the Earth on its own: it is bound, a temporary moon.
//                     (The Sun's tide can still pull it free again, which is
//                     how "mini-moons" come and go.)
//   the Moon          its sphere of influence, closest approach and surface
// Each is a function g(t, state) whose sign change is pinned down by
// bisection on the integrator's dense output, as for the rocket.
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/gravity.hpp"
#include "orbit/core/simulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace orbit {

namespace {

/// Things to watch for. Each g is negative before and positive after the
/// event, in the direction of time given in the comment.
enum class Trigger {
    // These end the pass. g > 0 is the forbidden side, whichever way time
    // runs: going backwards, an "impact" means the body rose from the ground.
    EarthImpact,   ///< R − r
    MoonImpact,    ///< R_moon − d
    DistanceLimit, ///< r − limit
    // These are events, defined forward in time.
    EarthPeriapsis,  ///< r·v
    EarthCapture,    ///< −ε
    EarthRelease,    ///< ε
    EnterMoonSphere, ///< SOI − d
    LeaveMoonSphere, ///< d − SOI
};

constexpr std::array kTriggers{Trigger::EarthImpact,     Trigger::MoonImpact,
                               Trigger::DistanceLimit,   Trigger::EarthPeriapsis,
                               Trigger::EarthCapture,    Trigger::EarthRelease,
                               Trigger::EnterMoonSphere, Trigger::LeaveMoonSphere};

bool endsPass(Trigger trigger) {
    return trigger == Trigger::EarthImpact || trigger == Trigger::MoonImpact ||
           trigger == Trigger::DistanceLimit;
}

/// One pass, forwards or backwards from the epoch. Times here are τ, seconds
/// from the epoch (negative before it).
class BodyPass {
public:
    BodyPass(const Mission& mission, const SimOptions& options, double direction)
        : m_body(&mission.body), m_opt(&options), m_direction(direction),
          m_stepper([this](double t, const State& y) { return derivative(t, y); },
                    options.tolerances) {}

    struct Output {
        std::vector<Sample> samples; ///< in the order flown (backwards: latest first)
        std::vector<FlightEvent> events;
        EndReason endReason = EndReason::SpanEnd;
    };

    /// Fly from `start` at τ0 until τ_end (earlier than τ0 going backwards).
    Output run(const State& start, double tau0, double tauEnd);

private:
    const NaturalBody* m_body;
    const SimOptions* m_opt;
    double m_direction;  ///< +1 forwards in time, −1 backwards
    double m_tau0 = 0.0; ///< where the pass started
    DormandPrince45 m_stepper;
    Output m_out;
    double m_tau = 0.0;
    State m_y;

    Vec3 moonAt(double tau) const { return moonPosition(m_body->epoch + tau); }
    State derivative(double tau, const State& y) const {
        return {y.v, gravityAcceleration(y.r, m_body->epoch + tau, m_opt->thirdBodyGravity), 0.0};
    }
    double g(Trigger trigger, double tau, const State& y) const;
    double outputInterval(double tau, const State& y) const;
    double findCrossing(Trigger trigger, double tau0, double tau1) const;
    void record(EventType type, std::string description);
};

double BodyPass::g(Trigger trigger, double tau, const State& y) const {
    const auto energy = [&] { return normSquared(y.v) / 2.0 - kMuEarth / norm(y.r); };
    switch (trigger) {
    case Trigger::EarthImpact:
        return kRadiusEarth - norm(y.r);
    case Trigger::MoonImpact:
        return kRadiusMoon - norm(y.r - moonAt(tau));
    case Trigger::DistanceLimit:
        return norm(y.r) - m_opt->bodyDistanceLimit;
    case Trigger::EarthPeriapsis:
        return dot(y.r, y.v);
    case Trigger::EarthCapture:
        return -energy();
    case Trigger::EarthRelease:
        return energy();
    case Trigger::EnterMoonSphere:
        return kMoonSphereOfInfluence - norm(y.r - moonAt(tau));
    case Trigger::LeaveMoonSphere:
        return norm(y.r - moonAt(tau)) - kMoonSphereOfInfluence;
    }
    return -1.0;
}

/// Samples a fixed fraction of the local orbital period apart (around the
/// Earth or the Moon, whichever is nearer in that sense), as for the rocket.
double BodyPass::outputInterval(double tau, const State& y) const {
    const double r = norm(y.r);
    double interval = m_opt->coastOutputFraction * std::sqrt(r * r * r / kMuEarth);
    if (m_opt->thirdBodyGravity) {
        const double d = norm(y.r - moonAt(tau));
        interval = std::min(interval, m_opt->coastOutputFraction * std::sqrt(d * d * d / kMuMoon));
    }
    return std::clamp(interval, m_opt->burnOutputInterval, m_opt->maxBodyOutputInterval);
}

/// Where in [τ0, τ1] (either order) the trigger's g changes sign, by
/// bisection on the dense output. Returns the first moment on the far side.
double BodyPass::findCrossing(Trigger trigger, double tau0, double tau1) const {
    const bool startPositive = g(trigger, tau0, m_stepper.interpolate(tau0)) >= 0.0;
    double near = tau0;
    double far = tau1;
    for (int i = 0; i < 80 && std::abs(far - near) > 1e-6; ++i) {
        const double mid = 0.5 * (near + far);
        if ((g(trigger, mid, m_stepper.interpolate(mid)) >= 0.0) == startPositive) {
            near = mid;
        } else {
            far = mid;
        }
    }
    return far;
}

void BodyPass::record(EventType type, std::string description) {
    const Sample sample{m_tau, m_y, false, 0};
    m_out.samples.push_back(sample);
    m_out.events.push_back({type, std::move(description), sample});
}

BodyPass::Output BodyPass::run(const State& start, double tau0, double tauEnd) {
    m_y = start;
    m_tau = tau0;
    m_tau0 = tau0;
    m_out.samples.push_back({tau0, m_y, false, 0});
    // Is a before b, in the direction of travel?
    const auto before = [&](double a, double b) { return m_direction * (a - b) < 0.0; };
    if (!before(tau0, tauEnd)) return std::move(m_out);

    double h = m_direction * m_opt->initialStep;
    double nextOutput = tau0 + m_direction * outputInterval(tau0, m_y);

    for (int stepCount = 0;; ++stepCount) {
        if (stepCount >= m_opt->maxSteps) {
            m_out.endReason = EndReason::MaxSteps;
            break;
        }
        if (!before(m_tau, tauEnd)) {
            m_out.endReason = EndReason::SpanEnd;
            break;
        }
        double hStep = m_direction * std::min(std::abs(h), m_opt->maxCoastStep);
        if (before(tauEnd, m_tau + hStep)) hStep = tauEnd - m_tau;

        const auto result = m_stepper.step(m_tau, m_y, hStep);
        h = result.nextStep;
        if (!result.accepted) continue;
        double tauStop = m_tau + hStep;

        // The earliest trigger (in the direction of travel) within the step.
        std::optional<Trigger> fired;
        for (const Trigger trigger : kTriggers) {
            if (!m_opt->thirdBodyGravity &&
                (trigger == Trigger::MoonImpact || trigger == Trigger::EnterMoonSphere ||
                 trigger == Trigger::LeaveMoonSphere)) {
                continue;
            }
            const double g0 = g(trigger, m_tau, m_y);
            const double g1 = g(trigger, m_tau + hStep, result.y);
            // Events are defined forwards in time; going backwards, the end
            // of the step is the earlier moment.
            const bool forwards = endsPass(trigger) || m_direction > 0.0;
            const double gEarly = forwards ? g0 : g1;
            const double gLate = forwards ? g1 : g0;
            if (gEarly >= 0.0 || gLate < 0.0) continue;
            const double crossing = findCrossing(trigger, m_tau, m_tau + hStep);
            // A body placed at periapsis starts with r·v = 0, give or take
            // rounding, which differs between compilers. That periapsis is
            // recorded once, at the epoch (simulateNaturalBody), not here.
            if (trigger == Trigger::EarthPeriapsis && std::abs(crossing - m_tau0) < 1e-3) {
                continue;
            }
            if (!fired || before(crossing, tauStop)) {
                tauStop = crossing;
                fired = trigger;
            }
        }
        const State yStop = fired ? m_stepper.interpolate(tauStop) : result.y;

        while (before(nextOutput, tauStop)) {
            const State y = m_stepper.interpolate(nextOutput);
            m_out.samples.push_back({nextOutput, y, false, 0});
            nextOutput += m_direction * outputInterval(nextOutput, y);
        }
        m_tau = tauStop;
        m_y = yStop;
        if (!fired) continue;

        switch (*fired) {
        case Trigger::EarthImpact:
            // Exactly on the surface (the interpolated point is within mm).
            m_y.r = kRadiusEarth * normalized(m_y.r);
            m_out.samples.push_back({m_tau, m_y, false, 0});
            m_out.endReason = EndReason::Impact;
            return std::move(m_out);
        case Trigger::MoonImpact:
            m_out.samples.push_back({m_tau, m_y, false, 0});
            m_out.endReason = EndReason::MoonImpact;
            return std::move(m_out);
        case Trigger::DistanceLimit:
            m_out.samples.push_back({m_tau, m_y, false, 0});
            m_out.endReason = EndReason::DistanceLimit;
            return std::move(m_out);
        case Trigger::EarthPeriapsis:
            record(EventType::EarthClosestApproach, "Closest approach to the Earth");
            break;
        case Trigger::EarthCapture:
            record(EventType::EarthCapture, "Captured: bound to the Earth");
            break;
        case Trigger::EarthRelease:
            record(EventType::EarthRelease, "Released: no longer bound to the Earth");
            break;
        case Trigger::EnterMoonSphere:
            record(EventType::EnterMoonSphere, "Entered the Moon's sphere of influence");
            break;
        case Trigger::LeaveMoonSphere:
            record(EventType::LeaveMoonSphere, "Left the Moon's sphere of influence");
            break;
        }
    }
    if (m_out.samples.back().t < m_tau || m_out.samples.back().t > m_tau) {
        m_out.samples.push_back({m_tau, m_y, false, 0});
    }
    return std::move(m_out);
}

double bodyEnergy(const State& y) { return normSquared(y.v) / 2.0 - kMuEarth / norm(y.r); }

/// Speed left over far from the Earth, v∞ = √(2ε), for an unbound orbit.
double excessSpeed(const State& y) {
    const double energy = bodyEnergy(y);
    return energy > 0.0 ? std::sqrt(2.0 * energy) : 0.0;
}

/// Elements whose periapsis lies underground describe a body that hits the
/// ground on its way in, before it gets there. Where that happens: the
/// moment τ (s from the epoch, negative) and the state, from the two-body
/// orbit (the conic equation r = p / (1 + e cos ν) solved for r = R, on the
/// way in, ν < 0). Nothing if the whole orbit is underground.
struct GroundCrossing {
    double tau;
    State state;
};
std::optional<GroundCrossing> incomingGroundCrossing(const NaturalBody& body) {
    const double q = body.periapsisDistance;
    const double e = body.eccentricity;
    const double p = q * (1.0 + e);
    const double cosNu = e > 0.0 ? (p / kRadiusEarth - 1.0) / e : 2.0;
    if (cosNu < -1.0 || cosNu > 1.0) return std::nullopt; // never reaches the surface
    const double nu = -std::acos(cosNu);
    NaturalBody atSurface = body;
    const PositionVelocity j2000 =
        stateFromPeriapsis(q, e, body.inclination, body.raan, body.argumentOfPeriapsis, nu);
    const double tau = timeSincePeriapsis(q, e, nu);
    atSurface.epoch = body.epoch + tau;
    atSurface.given = BodyGiven::StateVector;
    atSurface.position = j2000.r;
    atSurface.velocity = j2000.v;
    const PositionVelocity state = bodyStateAtEpoch(atSurface);
    return GroundCrossing{tau, {state.r, state.v, 0.0}};
}

} // namespace

SimResult simulateNaturalBody(const Mission& mission, const SimOptions& options) {
    const NaturalBody& body = mission.body;
    const PositionVelocity placed = bodyStateAtEpoch(body);
    State start{placed.r, placed.v, 0.0};
    double tauStart = 0.0;

    // Given underground: start at the impact, and only look back from there.
    const bool underground = norm(start.r) < kRadiusEarth;
    if (underground) {
        const std::optional<GroundCrossing> crossing =
            body.given == BodyGiven::Elements ? incomingGroundCrossing(body) : std::nullopt;
        if (crossing) {
            tauStart = crossing->tau;
            start = crossing->state;
        }
        start.r = kRadiusEarth * normalized(start.r); // exactly on the surface
    }

    BodyPass::Output back = BodyPass(mission, options, -1.0).run(start, tauStart, -body.timeBefore);
    BodyPass::Output ahead = BodyPass(mission, options, 1.0)
                                 .run(start, tauStart, underground ? tauStart : body.timeAfter);
    if (underground) ahead.endReason = EndReason::Impact;

    // Put the two passes end to end, from the earliest moment, with t = 0
    // at the start: t = τ + (how far back the first pass got).
    const double offset = -back.samples.back().t;
    SimResult result;
    result.mission = mission;
    result.startEpoch = body.epoch - offset;
    std::ranges::reverse(back.samples);
    std::ranges::reverse(back.events);
    result.samples = std::move(back.samples);
    result.samples.pop_back(); // the epoch, which the second pass starts with
    result.samples.insert(result.samples.end(), ahead.samples.begin(), ahead.samples.end());
    for (Sample& s : result.samples) s.t += offset;

    // The epoch itself is an event: where the body was placed (unless it
    // hit the ground before getting there).
    const bool elements = body.given == BodyGiven::Elements;
    if (!underground) {
        back.events.push_back({EventType::Epoch,
                               elements ? "Epoch: the orbital elements hold here"
                                        : "Epoch: the position and velocity hold here",
                               {0.0, start, false, 0}});
        // Placed at its closest approach (always, for elements): that is an
        // event too. The passes skip it, as rounding can put it either side.
        const double radial = dot(start.r, start.v) / (norm(start.r) * norm(start.v));
        if (std::abs(radial) < 1e-9) {
            back.events.push_back({EventType::EarthClosestApproach,
                                   "Closest approach to the Earth",
                                   {0.0, start, false, 0}});
        }
    }
    for (auto* events : {&back.events, &ahead.events}) {
        for (FlightEvent& e : *events) {
            e.sample.t += offset;
            result.events.push_back(std::move(e));
        }
    }
    // Events and samples at the same moment: keep them in time order.
    std::ranges::stable_sort(result.events, {}, [](const FlightEvent& e) { return e.sample.t; });
    std::ranges::stable_sort(result.samples, {}, &Sample::t);

    result.endReason = ahead.endReason;
    result.duration = result.samples.back().t;

    // The encounter, and the Moon.
    EncounterSummary encounter;
    encounter.epochTime = offset;
    encounter.closest = result.samples.front();
    double boundTime = 0.0;
    bool alwaysBound = true;
    for (std::size_t i = 0; i < result.samples.size(); ++i) {
        const Sample& s = result.samples[i];
        result.maxAltitude = std::max(result.maxAltitude, s.altitude());
        if (norm(s.state.r) < norm(encounter.closest.state.r)) encounter.closest = s;
        if (options.thirdBodyGravity) {
            const double moonAltitude =
                norm(s.state.r - moonPosition(result.startEpoch + s.t)) - kRadiusMoon;
            if (moonAltitude < result.lunar.closestApproachAltitude) {
                result.lunar.closestApproachAltitude = moonAltitude;
                result.lunar.closestApproachTime = s.t;
            }
        }
        alwaysBound = alwaysBound && bodyEnergy(s.state) < 0.0;
        // Time bound to the Earth: whole intervals between bound samples
        // (the capture and release events are samples too, so the edges are
        // exact).
        if (i > 0 && bodyEnergy(result.samples[i - 1].state) < 0.0 && bodyEnergy(s.state) < 0.0) {
            boundTime += s.t - result.samples[i - 1].t;
        }
    }
    for (const FlightEvent& e : result.events) {
        if (e.type == EventType::EnterMoonSphere) result.lunar.enteredSphereOfInfluence = true;
    }
    const State& first = result.samples.front().state;
    const State& last = result.samples.back().state;
    encounter.initialOrbit = elementsFromState(first.r, first.v);
    encounter.finalOrbit = elementsFromState(last.r, last.v);
    encounter.arrivalSpeed = excessSpeed(first);
    encounter.departureSpeed = excessSpeed(last);
    encounter.timeBound = boundTime;
    if (encounter.arrivalSpeed > 0.0) {
        encounter.impactParameter = norm(cross(first.r, first.v)) / encounter.arrivalSpeed;
    }
    result.orbit = encounter.finalOrbit;
    if (options.thirdBodyGravity) {
        const double epoch = result.startEpoch + result.duration;
        const Vec3 rMoon = last.r - moonPosition(epoch);
        if (norm(rMoon) < kMoonSphereOfInfluence) {
            result.lunar.finalOrbit =
                elementsFromState(rMoon, last.v - moonVelocity(epoch), kMuMoon, kRadiusMoon);
        }
    }

    // What became of it.
    const bool startedFromGround = back.endReason == EndReason::Impact;
    if (result.endReason == EndReason::Impact) {
        result.outcome = Outcome::Impact;
    } else if (result.endReason == EndReason::MoonImpact) {
        result.outcome = Outcome::LunarImpact;
    } else if (alwaysBound && !startedFromGround && encounter.finalOrbit.periapsis > kRadiusEarth) {
        result.outcome = Outcome::Orbit;
    } else if (boundTime > 0.0) {
        result.outcome = Outcome::TemporaryCapture;
    } else {
        result.outcome = Outcome::Flyby;
    }
    result.encounter = encounter;
    return result;
}

} // namespace orbit
