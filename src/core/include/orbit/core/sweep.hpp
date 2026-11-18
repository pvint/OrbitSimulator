// SPDX-License-Identifier: GPL-3.0-or-later
// Parameter sweeps: the same mission flown several times, changing one
// number from one value to another in even steps.
//
// The number can be any mission parameter (mission_parameters.hpp) or, for a
// custom flight program, the value of one of its steps: "step-4-value" is
// the 4th step's value, say the length of a coast before a burn. Sweeping
// that sweeps when the next burn starts.
#pragma once

#include "orbit/core/simulation.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orbit {

struct SweepSpec {
    std::string parameter; ///< a sweep target's key, e.g. "kick-angle" or "step-4-value"
    double from = 0.0;     ///< display units, like the parameter table
    double to = 0.0;
    int steps = 2; ///< number of flights, including both ends
};

/// Something a sweep can vary, in display units.
struct SweepTarget {
    std::string key;   ///< "kick-angle", "step-4-value"
    std::string title; ///< for people: "Kick angle", "Step 4 (Delay): time"
    std::string unit;  ///< "°", "s", ...
    double min = 0.0;  ///< sensible range for the sweep panel
    double max = 0.0;
    double step = 1.0;
    std::function<double(const Mission&)> get;
    std::function<void(Mission&, double)> set;
};

/// Everything this mission lets a sweep vary: every mission parameter, then
/// the values of its custom program's steps (if it flies one).
std::vector<SweepTarget> sweepTargets(const Mission& mission);

/// The target with this key for this mission, if there is one.
std::optional<SweepTarget> findSweepTarget(const Mission& mission, std::string_view key);

/// `steps` evenly spaced values from `from` to `to`, both included.
/// A single step gives just `from`.
std::vector<double> sweepValues(const SweepSpec& spec);

/// One mission per sweep value: `base` with the swept number set.
/// Throws std::invalid_argument if the key isn't a target of `base`.
std::vector<Mission> sweepMissions(const Mission& base, const SweepSpec& spec);

} // namespace orbit
