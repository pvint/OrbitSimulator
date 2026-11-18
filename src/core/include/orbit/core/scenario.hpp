// SPDX-License-Identifier: GPL-3.0-or-later
// Scenario files: a mission (and optionally a sweep) saved as JSON.
//
// Example:
//   {
//     "format": "orbitsim-scenario",
//     "version": 2,
//     "name": "Kick angle sweep",
//     "description": "How far the rocket tilts at liftoff decides everything.",
//     "mission": { "kick-angle": 25, "latitude": 28.5, "auto-circularize": true },
//     "sweep": { "parameter": "kick-angle", "from": 5, "to": 40, "steps": 8 }
//   }
//
// Mission values use the keys and *display units* of the parameter table
// (mission_parameters.hpp): tonnes, kN, degrees, km. Anything left out takes
// its default, so a scenario only needs to say what's different. Unknown keys
// produce warnings, not errors, so a file written by a newer version still
// loads as well as it can.
//
// VERSIONING: "version" is the file format version. If the format changes,
// bump kScenarioFormatVersion and teach scenarioFromJson() to upgrade older
// files. Files from a newer version than we know are refused.
//   1  the first format
//   2  the atmosphere arrives: "drag-coefficient" and "diameter". Keys left
//      out of a version 1 file take version 1's defaults: no drag, and a
//      25° kick (the default is now 20°, which suits flying through air).
//   3  custom flight programs: "plan": "custom" and a "program" list of steps
//      (scenario.cpp, readStep). Nothing else changed, so 2 needs no upgrade.
//   4  engines lose thrust in thick air: "sea-level-loss" (and per stage).
//      Keys left out of older files mean no loss, and the old 1.2 MN thrust.
//   5  a coast between stages: "stage-coast" (s). Older files: 0, the next
//      stage lit the moment the last was dropped.
#pragma once

#include "orbit/core/simulation.hpp"
#include "orbit/core/sweep.hpp"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace orbit {

inline constexpr int kScenarioFormatVersion = 5;
inline constexpr std::string_view kScenarioFormatName = "orbitsim-scenario";

struct Scenario {
    std::string name;
    std::string description;
    Mission mission = defaultMission();
    std::optional<SweepSpec> sweep;
};

/// A file that can't be read as a scenario at all.
class ScenarioError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct LoadedScenario {
    Scenario scenario;
    std::vector<std::string> warnings; ///< e.g. keys that were ignored
};

/// Parse a scenario. Throws ScenarioError if it isn't one.
LoadedScenario scenarioFromJson(std::string_view text);

/// The scenario as pretty-printed JSON. Writes every mission parameter, so
/// the file stays meaningful even if the defaults change later.
std::string scenarioToJson(const Scenario& scenario);

/// File helpers (used by the CLI; the app reads files with Qt).
LoadedScenario loadScenarioFile(const std::filesystem::path& path);
void saveScenarioFile(const Scenario& scenario, const std::filesystem::path& path);

} // namespace orbit
