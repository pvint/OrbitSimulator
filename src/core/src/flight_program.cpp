// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/flight_program.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/constants.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/simulation.hpp"

#include <algorithm>
#include <cmath>

namespace orbit {

namespace {

/// The sidereal month: the Moon's orbital period relative to the stars.
constexpr double kSiderealMonth = 27.321661 * kSecondsPerDay;

/// Time from the burn to reaching the Moon's distance is a few days; the Moon's
/// distance is taken at roughly the arrival time.
constexpr double kTypicalTransferTime = 3.5 * kSecondsPerDay;

/// The guided ascent takes over from the pitch program here (m): above most
/// of the air, where turning the rocket away from its direction of motion no
/// longer matters.
constexpr double kGuidanceStartAltitude = 30e3;

} // namespace

std::string_view planKey(FlightPlan plan) {
    switch (plan) {
    case FlightPlan::Custom:
        return "custom";
    case FlightPlan::GuidedOrbit:
        return "guided-orbit";
    case FlightPlan::Orbit:
        return "orbit";
    case FlightPlan::BurnToDepletion:
        return "burn-to-depletion";
    case FlightPlan::LunarFlyby:
        return "lunar-flyby";
    case FlightPlan::LunarOrbit:
        return "lunar-orbit";
    }
    return "orbit";
}

bool planFromKey(std::string_view key, FlightPlan& plan) {
    for (const FlightPlan p : kAllPlans) {
        if (planKey(p) == key) {
            plan = p;
            return true;
        }
    }
    return false;
}

std::string_view planLabel(FlightPlan plan) {
    switch (plan) {
    case FlightPlan::Orbit:
        return "Orbit (circularize at target)";
    case FlightPlan::BurnToDepletion:
        return "Burn until empty";
    case FlightPlan::LunarFlyby:
        return "Lunar flyby";
    case FlightPlan::LunarOrbit:
        return "Lunar orbit";
    case FlightPlan::Custom:
        return "Custom program";
    case FlightPlan::GuidedOrbit:
        return "Orbit (guided ascent)";
    }
    return {};
}

std::string_view steeringKey(Steering steering) {
    switch (steering) {
    case Steering::PitchProgram:
        return "pitch-program";
    case Steering::Prograde:
        return "prograde";
    case Steering::Retrograde:
        return "retrograde";
    case Steering::Circularize:
        return "circularize";
    case Steering::MoonRetrograde:
        return "moon-retrograde";
    case Steering::Normal:
        return "normal";
    case Steering::Antinormal:
        return "antinormal";
    case Steering::RadialOut:
        return "radial-out";
    case Steering::RadialIn:
        return "radial-in";
    case Steering::OrbitInsertion:
        return "orbit-insertion";
    }
    return "prograde";
}

std::string_view steeringLabel(Steering steering) {
    switch (steering) {
    case Steering::PitchProgram:
        return "Pitch program (ascent)";
    case Steering::Prograde:
        return "Prograde (along the velocity)";
    case Steering::Retrograde:
        return "Retrograde (against the velocity)";
    case Steering::Circularize:
        return "Towards circular orbit";
    case Steering::MoonRetrograde:
        return "Retrograde, relative to the Moon";
    case Steering::Normal:
        return "Normal (tilt the orbit north)";
    case Steering::Antinormal:
        return "Anti-normal (tilt the orbit south)";
    case Steering::RadialOut:
        return "Radial out (straight up)";
    case Steering::RadialIn:
        return "Radial in (straight down)";
    case Steering::OrbitInsertion:
        return "Guided into orbit (closed loop)";
    }
    return {};
}

bool steeringFromKey(std::string_view key, Steering& steering) {
    for (const Steering s : kAllSteerings) {
        if (steeringKey(s) == key) {
            steering = s;
            return true;
        }
    }
    return false;
}

std::string_view stepEndKey(StepEnd end) {
    switch (end) {
    case StepEnd::PropellantExhausted:
        return "propellant-exhausted";
    case StepEnd::ApoapsisAltitude:
        return "apoapsis-altitude";
    case StepEnd::PeriapsisAltitude:
        return "periapsis-altitude";
    case StepEnd::Altitude:
        return "altitude";
    case StepEnd::Apoapsis:
        return "apoapsis";
    case StepEnd::Periapsis:
        return "periapsis";
    case StepEnd::Circular:
        return "circular";
    case StepEnd::OrbitAt:
        return "orbit-at";
    case StepEnd::MoonAhead:
        return "moon-ahead";
    case StepEnd::MoonClosestApproach:
        return "moon-closest-approach";
    case StepEnd::MoonCapture:
        return "moon-capture";
    case StepEnd::Revolutions:
        return "revolutions";
    case StepEnd::Duration:
        return "duration";
    }
    return "duration";
}

std::string_view stepEndLabel(StepEnd end) {
    switch (end) {
    case StepEnd::PropellantExhausted:
        return "Propellant runs out";
    case StepEnd::ApoapsisAltitude:
        return "Apoapsis altitude reaches";
    case StepEnd::PeriapsisAltitude:
        return "Periapsis altitude reaches";
    case StepEnd::Altitude:
        return "Altitude reaches";
    case StepEnd::Apoapsis:
        return "At apoapsis";
    case StepEnd::Periapsis:
        return "At periapsis";
    case StepEnd::Circular:
        return "Orbit is circular";
    case StepEnd::OrbitAt:
        return "In orbit at (apoapsis held)";
    case StepEnd::MoonAhead:
        return "Moon's arrival point ahead by";
    case StepEnd::MoonClosestApproach:
        return "Closest to the Moon";
    case StepEnd::MoonCapture:
        return "Captured by the Moon, apoapsis below";
    case StepEnd::Revolutions:
        return "Revolutions";
    case StepEnd::Duration:
        return "Time";
    }
    return {};
}

bool stepEndFromKey(std::string_view key, StepEnd& end) {
    for (const StepEnd e : kAllStepEnds) {
        if (stepEndKey(e) == key) {
            end = e;
            return true;
        }
    }
    return false;
}

bool stepEndHasValue(StepEnd end) { return !stepEndUnit(end).empty(); }

std::string_view stepEndUnit(StepEnd end) {
    switch (end) {
    case StepEnd::ApoapsisAltitude:
    case StepEnd::PeriapsisAltitude:
    case StepEnd::Altitude:
    case StepEnd::MoonCapture:
    case StepEnd::OrbitAt:
        return "km";
    case StepEnd::MoonAhead:
        return "°";
    case StepEnd::Revolutions:
        return "rev";
    case StepEnd::Duration:
        return "s";
    case StepEnd::PropellantExhausted:
    case StepEnd::Apoapsis:
    case StepEnd::Periapsis:
    case StepEnd::Circular:
    case StepEnd::MoonClosestApproach:
        break;
    }
    return {};
}

double stepValueToDisplay(StepEnd end, double value) {
    const std::string_view unit = stepEndUnit(end);
    if (unit == "km") return value / 1000.0;
    if (unit == "°") return radToDeg(value);
    return value;
}

double stepValueFromDisplay(StepEnd end, double display) {
    const std::string_view unit = stepEndUnit(end);
    if (unit == "km") return display * 1000.0;
    if (unit == "°") return degToRad(display);
    return display;
}

// The trans-lunar injection (TLI) burn starts in the parking orbit and raises
// the apoapsis beyond the Moon. The rocket then climbs along most of half an
// ellipse while the Moon moves on in its own orbit. For them to meet, the
// Moon must start ahead of the rocket by
//     lead = ν − n_moon · t
// where ν is how far round the transfer ellipse the rocket travels to reach
// the Moon's distance, t is how long that takes (Kepler's equation), and
// n_moon is the Moon's angular speed. A typical lead is about 110–130°.
namespace {

struct Transfer {
    double trueAnomaly; ///< ν: how far round the ellipse the Moon's distance is reached
    double time;        ///< s from the burn to getting there
};

Transfer transferToMoon(const Mission& mission) {
    const double rp = kRadiusEarth + mission.targetAltitude;
    const double ra = std::max(kRadiusEarth + mission.lunar.tliApoapsisAltitude, rp * 1.001);
    const double rMoon = norm(moonPosition(mission.launchEpoch + kTypicalTransferTime));

    const double a = (rp + ra) / 2.0;
    const double e = (ra - rp) / (ra + rp);
    const double p = a * (1.0 - e * e);
    // True anomaly where the ellipse reaches the Moon's distance (r = p / (1 + e cos ν)).
    const double cosNu = std::clamp((p / std::min(rMoon, ra) - 1.0) / e, -1.0, 1.0);
    const double nu = std::acos(cosNu);
    // Kepler: eccentric anomaly E, mean anomaly M = E − e sin E, time = M / n.
    const double E = 2.0 * std::atan(std::sqrt((1.0 - e) / (1.0 + e)) * std::tan(nu / 2.0));
    const double M = E - e * std::sin(E);
    return {nu, M / std::sqrt(kMuEarth / (a * a * a))};
}

} // namespace

double leadAngle(const Mission& mission) {
    const Transfer t = transferToMoon(mission);
    const double moonAngularSpeed = kTwoPi / kSiderealMonth;
    return t.trueAnomaly - moonAngularSpeed * t.time + mission.lunar.leadAngleAdjust;
}

double lunarTransferTime(const Mission& mission) { return transferToMoon(mission).time; }

double lunarTravelAngle(const Mission& mission) {
    return transferToMoon(mission).trueAnomaly + mission.lunar.leadAngleAdjust;
}

FlightProgram flightProgramFor(const Mission& mission) {
    if (mission.plan == FlightPlan::Custom) return mission.program;
    FlightProgram program;

    if (mission.plan == FlightPlan::GuidedOrbit) {
        // Through the thick air on the pitch program (steering against the
        // airflow would be hard on a real rocket), then closed-loop guidance
        // straight into orbit.
        program.push_back({"Ascent through the air", true, Steering::PitchProgram,
                           StepEnd::Altitude, kGuidanceStartAltitude});
        program.push_back({"Guided ascent", true, Steering::OrbitInsertion, StepEnd::OrbitAt,
                           mission.targetAltitude});
        // Only needed if the engine was too strong to hold the apoapsis (or
        // ran dry): then coast up to it and circularize, as the Orbit plan.
        program.push_back({"Coast to apoapsis", false, Steering::Prograde, StepEnd::Apoapsis});
        program.push_back({"Circularization", true, Steering::Circularize, StepEnd::Circular});
        program.push_back({"Coast", false, Steering::Prograde, StepEnd::Revolutions, 1.0});
        return program;
    }

    if (mission.plan == FlightPlan::BurnToDepletion) {
        program.push_back({"Ascent", true, Steering::PitchProgram, StepEnd::PropellantExhausted});
        program.push_back({"Coast", false, Steering::Prograde, StepEnd::Revolutions, 1.0});
        return program;
    }

    // Every other plan starts by reaching a circular orbit at the target altitude.
    program.push_back({"Ascent", true, Steering::PitchProgram, StepEnd::ApoapsisAltitude,
                       mission.targetAltitude});
    program.push_back({"Coast to apoapsis", false, Steering::Prograde, StepEnd::Apoapsis});
    program.push_back({"Circularization", true, Steering::Circularize, StepEnd::Circular});

    if (mission.plan == FlightPlan::Orbit) {
        program.push_back({"Coast", false, Steering::Prograde, StepEnd::Revolutions, 1.0});
        return program;
    }

    // Lunar plans: wait in the parking orbit for the right moment, burn, coast out.
    const double parkingPeriod =
        kTwoPi * std::sqrt(std::pow(kRadiusEarth + mission.targetAltitude, 3) / kMuEarth);
    // Burn when the point where the Moon will be on arrival is exactly as far
    // ahead as the rocket will travel: then both get there together. (Aiming
    // at the Moon's *future* position keeps that point in the orbit plane,
    // when the launch was aimed with azimuthToMoon.)
    //
    // A delay is a timed coast after that moment. An early start can't go
    // back in time, so instead the wait ends early: the angle to the Moon's
    // arrival point shrinks at the rocket's angular speed less the Moon's,
    // so ending it that much sooner, in angle, starts the burn that much
    // sooner (to a fraction of a second).
    const double delay = mission.lunar.injectionDelay;
    const double closingRate = kTwoPi / parkingPeriod - kTwoPi / kSiderealMonth; // rad/s
    FlightStep parking{"Parking orbit",
                       false,
                       Steering::Prograde,
                       StepEnd::MoonAhead,
                       lunarTravelAngle(mission) + closingRate * std::max(-delay, 0.0),
                       2.0 * parkingPeriod};
    parking.lookAhead = lunarTransferTime(mission);
    program.push_back(parking);
    if (delay > 0.0) {
        program.push_back({"Delay", false, Steering::Prograde, StepEnd::Duration, delay});
    }
    program.push_back({"Trans-lunar injection", true, Steering::Prograde, StepEnd::ApoapsisAltitude,
                       mission.lunar.tliApoapsisAltitude});
    program.push_back({"Coast to the Moon", false, Steering::Prograde, StepEnd::MoonClosestApproach,
                       0.0, 8.0 * kSecondsPerDay});

    if (mission.plan == FlightPlan::LunarFlyby) {
        program.push_back(
            {"Coast", false, Steering::Prograde, StepEnd::Duration, 3.0 * kSecondsPerDay});
        return program;
    }

    program.push_back({"Lunar orbit insertion", true, Steering::MoonRetrograde,
                       StepEnd::MoonCapture, mission.lunar.captureApoapsisAltitude});
    program.push_back(
        {"Lunar orbit", false, Steering::Prograde, StepEnd::Duration, 1.0 * kSecondsPerDay});
    return program;
}

} // namespace orbit
