// SPDX-License-Identifier: GPL-3.0-or-later
// Parameter sweeps: the same mission flown several times, changing one
// parameter from one value to another in even steps.
#pragma once

#include "orbit/core/simulation.hpp"

#include <string>
#include <vector>

namespace orbit {

struct SweepSpec {
    std::string parameter; ///< key from missionParameters(), e.g. "kick-angle"
    double from = 0.0;     ///< display units, like the parameter table
    double to = 0.0;
    int steps = 2; ///< number of flights, including both ends
};

/// `steps` evenly spaced values from `from` to `to`, both included.
/// A single step gives just `from`.
std::vector<double> sweepValues(const SweepSpec& spec);

/// One mission per sweep value: `base` with the swept parameter set.
/// Throws std::invalid_argument if the parameter key is unknown.
std::vector<Mission> sweepMissions(const Mission& base, const SweepSpec& spec);

} // namespace orbit
