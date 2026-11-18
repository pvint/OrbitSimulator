// SPDX-License-Identifier: GPL-3.0-or-later
// The adjustable numbers of a Mission, described once for every front end.
//
// The GUI builds its sliders from this table, the CLI its --options, and
// later the sweeps and scenario files use the same keys. Values go in and out
// in *display units* (t, kN, km, degrees), because that's what people type
// and read. The get/set functions convert to and from the SI units and
// radians the physics uses.
#pragma once

#include "orbit/core/simulation.hpp"

#include <span>
#include <string>
#include <string_view>

namespace orbit {

struct ParameterInfo {
    std::string_view key;   ///< stable identifier, e.g. "kick-angle" (CLI: --kick-angle)
    std::string_view label; ///< for people, e.g. "Kick angle"
    std::string_view unit;  ///< display unit, e.g. "°"
    std::string_view group; ///< heading it belongs under, e.g. "Pitch program"
    std::string_view help;  ///< one-line explanation
    double min;             ///< sensible slider range, in display units
    double max;
    double step;
    double (*get)(const Mission&); ///< value in display units
    void (*set)(Mission&, double); ///< from a value in display units
    /// Computed from other parameters; setting it changes them instead
    /// (burn time sets the propellant). Derived parameters can be swept and
    /// typed on the command line, but get no slider of their own.
    bool derived = false;
    /// Written to scenario files under its key. False for parts of a value
    /// stored another way (the launch hour is saved within "launch-utc").
    bool inScenario = true;
};

/// Seconds since midnight UTC of the launch day, and setting it while
/// keeping the date.
double launchHourOfDay(const Mission& mission);
void setLaunchHourOfDay(Mission& mission, double hours);

/// All mission parameters, in display order, grouped by `group`.
std::span<const ParameterInfo> missionParameters();

/// A name that is unique among the parameters: the label, or, where
/// several share it (every stage has a "Thrust"), the group and the label:
/// "Stage 2: Thrust (vacuum)". For lists of all parameters, like the sweep's.
std::string parameterTitle(const ParameterInfo& parameter);

/// The parameter with this key, or nullptr.
const ParameterInfo* findParameter(std::string_view key);

} // namespace orbit
