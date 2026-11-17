// SPDX-License-Identifier: GPL-3.0-or-later
// Demo tours: a guided sequence of scenarios with captions.
//
// Example:
//   {
//     "format": "orbitsim-demo",
//     "version": 1,
//     "name": "A tour of Orbit Simulator",
//     "steps": [
//       { "title": "First orbit", "caption": "A rocket from Florida...",
//         "scenario": "cape-canaveral-leo.json", "view": "launch-site",
//         "seconds": 25 }
//     ]
//   }
//
// Each step opens a scenario (a file name in scenarios/), sets up the
// camera, plays the flight, and shows the caption. Optional per step:
//   "view"      how to look at it: "auto" (default; fits the flight),
//               "launch-site", "north" (down on the north pole) or "moon"
//   "centre"    what the camera circles: "earth" (default), "rocket" or "moon"
//   "distance"  camera distance in km (default: chosen by the view)
//   "speed"     playback speed (default: shows the whole flight in ~90 s)
//   "start-hours" start playback this far into the flight (default 0)
//   "seconds"   how long the step lasts before moving on (default 20)
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace orbit {

inline constexpr int kDemoFormatVersion = 1;

enum class DemoView { Auto, LaunchSite, North, Moon };
enum class DemoCentre { Earth, Rocket, Moon };

struct DemoStep {
    std::string title;
    std::string caption;
    std::string scenario; ///< file name in scenarios/
    DemoView view = DemoView::Auto;
    DemoCentre centre = DemoCentre::Earth;
    double distance = 0.0;  ///< km; 0 = chosen by the view
    double speed = 0.0;     ///< playback speed; 0 = automatic
    double startTime = 0.0; ///< s into the flight where playback starts
    double seconds = 20.0;
};

struct Demo {
    std::string name;
    std::vector<DemoStep> steps;
};

class DemoError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Parse a demo tour. Throws DemoError if it isn't a valid one.
Demo demoFromJson(std::string_view text);

} // namespace orbit
