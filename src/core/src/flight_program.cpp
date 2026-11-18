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

} // namespace

std::string_view planKey(FlightPlan plan) {
    switch (plan) {
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
    for (const FlightPlan p : {FlightPlan::Orbit, FlightPlan::BurnToDepletion,
                               FlightPlan::LunarFlyby, FlightPlan::LunarOrbit}) {
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
    }
    return {};
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
    FlightProgram program;

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
