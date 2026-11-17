// SPDX-License-Identifier: GPL-3.0-or-later
// Helpers for aiming a launch at the Moon.
//
// A trans-lunar injection burn keeps the rocket in its parking orbit's plane,
// so the Moon must lie in that plane when the rocket arrives. The plane is
// fixed in space at liftoff by the launch site and the launch direction, so
// for a given launch time there is one launch direction (azimuth) that works.
// Launching at a different time of day turns the launch site, and so the
// plane, around the Earth's axis: some times need a direction close to due
// east, which is the cheapest. Real launch windows come from exactly this.
#pragma once

#include "orbit/core/simulation.hpp"

#include <optional>

namespace orbit {

/// Roughly when (seconds after liftoff) the rocket reaches the Moon's
/// distance: ascent and up to one parking orbit, then the transfer.
double estimatedArrivalTime(const Mission& mission);

/// The launch azimuth (radians) that puts the parking orbit's plane through
/// the Moon at arrival. Starts from the geometry, then refines it with a few
/// quick simulations of the ascent, because the Earth's rotation tilts the
/// actual orbit plane slightly away from the launch direction. Returns
/// nothing if the ascent itself fails (the rocket can't reach orbit).
std::optional<double> azimuthToMoon(const Mission& mission);

/// The time of day (decimal hours UTC) on the mission's launch date whose
/// required launch direction is closest to due east, the cheapest way to
/// reach the Moon from that site that day.
double bestLaunchHourForMoon(const Mission& mission);

} // namespace orbit
