// SPDX-License-Identifier: GPL-3.0-or-later
// Command-line front end to the physics core: fly one mission (or a sweep)
// and print the outcome, or write the trajectory as CSV.
//
//   orbitsim-cli                                  default mission
//   orbitsim-cli --kick-angle 30 --latitude 0     change parameters (see --help)
//   orbitsim-cli --scenario scenarios/kourou-equatorial.json
//   orbitsim-cli --sweep kick-angle:5:45:9        a table, one row per flight
//   orbitsim-cli --csv flight.csv                 also write every sample

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/lunar_targeting.hpp"
#include "orbit/core/mission_parameters.hpp"
#include "orbit/core/scenario.hpp"
#include "orbit/core/simulation.hpp"
#include "orbit/core/sweep.hpp"
#include "orbit/core/version.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// `text` padded with spaces to `width` characters. Counts characters, not
/// bytes, so "°" (two bytes in UTF-8) lines up.
std::string padded(std::string_view text, std::size_t width) {
    const auto chars = static_cast<std::size_t>(std::ranges::count_if(
        text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
    return std::string(text) + std::string(width > chars ? width - chars : 0, ' ');
}

void printUsage() {
    std::cout << "Usage: orbitsim-cli [options]\n\n"
                 "Flies one mission with the orbit simulator physics and prints the result.\n\n"
                 "Mission options (defaults: the prototype's rocket from Cape Canaveral):\n";
    for (const auto& p : orbit::missionParameters()) {
        std::cout << "  --" << padded(p.key, 17) << padded(p.unit, 4) << p.help << '\n';
    }
    std::cout << "  --plan PLAN            orbit, burn-to-depletion, lunar-flyby or lunar-orbit\n"
                 "  --no-circularize       same as --plan burn-to-depletion\n"
                 "  --best-launch-hour     pick the time of day that lets a lunar flight go east\n"
                 "  --aim-moon             set the azimuth so the orbit plane meets the Moon\n"
                 "\nScenarios and sweeps:\n"
                 "  --scenario FILE        start from a scenario file (put it first; later\n"
                 "                         options change it)\n"
                 "  --sweep KEY:FROM:TO:STEPS  fly STEPS flights with parameter KEY from FROM\n"
                 "                         to TO, e.g. --sweep kick-angle:5:45:9\n"
                 "  --no-sweep             ignore the scenario's sweep\n"
                 "  --save-scenario FILE   save the mission (and sweep) as a scenario file\n"
                 "\nOutput options:\n"
                 "  --csv FILE             write every sample (SI units) to FILE (single flight)\n"
                 "  --version              print the version and exit\n"
                 "  --help                 show this help\n";
}

bool parseNumber(std::string_view text, double& out) {
    const char* begin =
        text.data(); // NOLINT(bugprone-suspicious-stringview-data-usage): end is passed too
    const char* end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

/// Parses "kick-angle:5:45:9".
std::optional<orbit::SweepSpec> parseSweep(std::string_view text) {
    std::vector<std::string_view> parts;
    for (std::size_t start = 0;;) {
        const std::size_t colon = text.find(':', start);
        parts.push_back(text.substr(start, colon - start));
        if (colon == std::string_view::npos) break;
        start = colon + 1;
    }
    orbit::SweepSpec spec;
    double steps = 0.0;
    if (parts.size() != 4 || orbit::findParameter(parts[0]) == nullptr ||
        !parseNumber(parts[1], spec.from) || !parseNumber(parts[2], spec.to) ||
        !parseNumber(parts[3], steps) || steps < 1 || steps != std::floor(steps)) {
        return std::nullopt;
    }
    spec.parameter = std::string(parts[0]);
    spec.steps = static_cast<int>(steps);
    return spec;
}

std::string km(double metres) {
    if (!std::isfinite(metres)) return "infinite";
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << metres / 1000.0 << " km";
    return os.str();
}

std::string duration(double seconds) {
    if (!std::isfinite(seconds)) return "-";
    std::ostringstream os;
    os << std::fixed << std::setprecision(1);
    if (seconds < 120.0) {
        os << seconds << " s";
    } else if (seconds < 7200.0) {
        os << seconds / 60.0 << " min";
    } else if (seconds < 2.0 * 86400.0) {
        os << seconds / 3600.0 << " h";
    } else {
        os << seconds / 86400.0 << " days";
    }
    return os.str();
}

void printResult(const orbit::SimResult& r) {
    using orbit::radToDeg;
    const auto& o = r.orbit;
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Outcome:          " << orbit::toString(r.outcome)
              << "  (simulation ended: " << orbit::toString(r.endReason) << " at T+"
              << duration(r.duration) << ")\n"
              << "Periapsis:        " << km(o.periapsisAltitude) << '\n'
              << "Apoapsis:         " << km(o.apoapsisAltitude) << '\n'
              << "Inclination:      " << radToDeg(o.inclination) << " deg\n"
              << "RAAN:             " << radToDeg(o.raan) << " deg\n"
              << std::setprecision(5) << "Eccentricity:     " << o.eccentricity << '\n'
              << std::setprecision(1) << "Period:           " << duration(o.period) << '\n'
              << "Max altitude:     " << km(r.maxAltitude) << '\n'
              << std::setprecision(0) << "Delta-v used:     " << r.deltaVUsed << " m/s\n"
              << "Propellant left:  " << r.propellantLeft << " kg\n";
    if (r.maxDynamicPressure) {
        const orbit::Sample& s = *r.maxDynamicPressure;
        const orbit::Aerodynamics aero = orbit::aerodynamicsAt(r, s);
        std::cout << std::setprecision(1) << "Max Q:            " << aero.dynamicPressure / 1000.0
                  << " kPa at T+" << duration(s.t) << ", " << km(s.altitude()) << ", Mach "
                  << aero.mach << '\n'
                  << std::setprecision(0) << "Drag loss:        " << r.dragLoss << " m/s\n";
    }
    for (const orbit::SpentStage& spent : r.spentStages) {
        const orbit::Sample& separation = spent.samples.front();
        std::cout << "Stage " << spent.stage + 1 << ":          dropped at T+"
                  << duration(separation.t) << ", " << km(separation.altitude()) << ", "
                  << std::setprecision(0) << separation.speed() << " m/s; ";
        if (spent.impactTime) {
            std::cout << "came down " << km(spent.impactDistance) << " downrange at T+"
                      << duration(*spent.impactTime) << '\n';
        } else {
            std::cout << "still up at the end\n";
        }
    }
    const orbit::LunarSummary& moon = r.lunar;
    if (moon.enteredSphereOfInfluence || r.mission.plan == orbit::FlightPlan::LunarFlyby ||
        r.mission.plan == orbit::FlightPlan::LunarOrbit) {
        std::cout << "Moon:             closest approach " << km(moon.closestApproachAltitude)
                  << " above the surface, at T+" << duration(moon.closestApproachTime) << '\n';
        if (moon.finalOrbit) {
            std::cout << "  orbit round it: " << km(moon.finalOrbit->periapsisAltitude) << " x "
                      << km(moon.finalOrbit->apoapsisAltitude) << ", inclination "
                      << std::setprecision(1) << radToDeg(moon.finalOrbit->inclination) << " deg\n";
        }
    }
    if (!r.events.empty()) {
        std::cout << "Events:\n";
        for (const auto& e : r.events) {
            std::cout << "  T+" << std::left << std::setw(10) << duration(e.sample.t) << std::left
                      << std::setw(48) << e.description << std::right << " alt "
                      << km(e.sample.altitude()) << ", speed " << std::setprecision(0)
                      << e.sample.speed() << " m/s\n";
        }
    }
    std::cout << r.samples.size() << " samples\n";
}

bool writeCsv(const orbit::SimResult& result, const std::string& path) {
    std::ofstream out(path);
    if (!out) return false;
    out << "t,x,y,z,vx,vy,vz,mass,altitude,speed,burning,mach,dynamic_pressure,drag\n"
        << std::setprecision(10);
    for (const auto& s : result.samples) {
        const auto& y = s.state;
        const orbit::Aerodynamics aero = orbit::aerodynamicsAt(result, s);
        out << s.t << ',' << y.r.x << ',' << y.r.y << ',' << y.r.z << ',' << y.v.x << ',' << y.v.y
            << ',' << y.v.z << ',' << y.m << ',' << s.altitude() << ',' << s.speed() << ','
            << (s.burning ? 1 : 0) << ',' << aero.mach << ',' << aero.dynamicPressure << ','
            << orbit::norm(aero.drag) << '\n';
    }
    return static_cast<bool>(out);
}

void printSweep(const orbit::Mission& base, const orbit::SweepSpec& spec) {
    const orbit::ParameterInfo& p = *orbit::findParameter(spec.parameter);
    const auto values = orbit::sweepValues(spec);
    const auto missions = orbit::sweepMissions(base, spec);
    std::cout << "Sweep of " << orbit::parameterTitle(p)
              << (p.unit.empty() ? std::string() : " (" + std::string(p.unit) + ")") << "\n\n"
              << std::right << std::setw(10) << "value" << "  " << std::left << std::setw(13)
              << "outcome" << std::right << std::setw(13) << "periapsis" << std::setw(13)
              << "apoapsis" << std::setw(9) << "incl." << std::setw(11) << "dv used"
              << std::setw(16) << "nearest Moon" << std::setw(10) << "max Q" << std::setw(11)
              << "drag loss" << '\n';
    for (std::size_t i = 0; i < missions.size(); ++i) {
        const orbit::SimResult r = orbit::simulate(missions[i]);
        std::cout << std::fixed << std::setprecision(2) << std::setw(10) << values[i] << "  "
                  << std::left << std::setw(13) << orbit::toString(r.outcome) << std::right
                  << std::setw(13) << km(r.orbit.periapsisAltitude) << std::setw(13)
                  << km(r.orbit.apoapsisAltitude) << std::setprecision(1) << std::setw(9)
                  << orbit::radToDeg(r.orbit.inclination) << std::setprecision(0) << std::setw(7)
                  << r.deltaVUsed << " m/s" << std::setw(16) << km(r.lunar.closestApproachAltitude);
        if (r.maxDynamicPressure) {
            const double q = orbit::aerodynamicsAt(r, *r.maxDynamicPressure).dynamicPressure;
            std::cout << std::setprecision(1) << std::setw(6) << q / 1000.0 << " kPa"
                      << std::setprecision(0) << std::setw(7) << r.dragLoss << " m/s";
        }
        std::cout << '\n';
    }
}

} // namespace

int main(int argc, char* argv[]) {
    const std::span args(argv + 1, static_cast<std::size_t>(argc - 1));
    orbit::Scenario scenario;
    std::string csvPath;
    std::string savePath;
    bool aimMoon = false;
    bool bestHour = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--version") {
            std::cout << orbit::kProjectName << ' ' << orbit::kVersion << '\n';
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--no-circularize") {
            scenario.mission.plan = orbit::FlightPlan::BurnToDepletion;
            continue;
        }
        if (arg == "--aim-moon") {
            aimMoon = true;
            continue;
        }
        if (arg == "--best-launch-hour") {
            bestHour = true;
            continue;
        }
        if (arg == "--no-sweep") {
            scenario.sweep.reset();
            continue;
        }
        const bool takesText = arg == "--csv" || arg == "--scenario" || arg == "--sweep" ||
                               arg == "--save-scenario" || arg == "--plan";
        const orbit::ParameterInfo* param =
            arg.starts_with("--") ? orbit::findParameter(arg.substr(2)) : nullptr;
        if (!takesText && param == nullptr) {
            std::cerr << "Unknown option: " << arg << "\n\n";
            printUsage();
            return 1;
        }
        if (i + 1 >= args.size()) {
            std::cerr << arg << " needs a value\n";
            return 1;
        }
        const std::string_view value = args[++i];

        if (arg == "--csv") {
            csvPath = value;
        } else if (arg == "--plan") {
            if (!orbit::planFromKey(value, scenario.mission.plan)) {
                std::cerr
                    << "--plan must be orbit, burn-to-depletion, lunar-flyby or lunar-orbit\n";
                return 1;
            }
        } else if (arg == "--save-scenario") {
            savePath = value;
        } else if (arg == "--scenario") {
            try {
                const orbit::LoadedScenario loaded = orbit::loadScenarioFile(std::string(value));
                for (const auto& warning : loaded.warnings) {
                    std::cerr << "warning: " << warning << '\n';
                }
                scenario = loaded.scenario;
                std::cout << "Scenario: " << scenario.name << "\n\n";
            } catch (const orbit::ScenarioError& e) {
                std::cerr << value << ": " << e.what() << '\n';
                return 1;
            }
        } else if (arg == "--sweep") {
            scenario.sweep = parseSweep(value);
            if (!scenario.sweep) {
                std::cerr << "--sweep needs KEY:FROM:TO:STEPS, e.g. kick-angle:5:45:9\n";
                return 1;
            }
        } else {
            double number = 0.0;
            if (!parseNumber(value, number)) {
                std::cerr << "Not a number for " << arg << ": " << value << '\n';
                return 1;
            }
            param->set(scenario.mission, number);
        }
    }

    // Lunar aiming happens after all other options, which it depends on.
    if (bestHour) {
        orbit::setLaunchHourOfDay(scenario.mission, orbit::bestLaunchHourForMoon(scenario.mission));
        std::cout << "Launch time: " << orbit::formatIsoUtc(scenario.mission.launchEpoch) << '\n';
    }
    if (aimMoon) {
        const std::optional<double> azimuth = orbit::azimuthToMoon(scenario.mission);
        if (!azimuth) {
            std::cerr << "Can't aim at the Moon: the rocket doesn't reach orbit\n";
            return 1;
        }
        scenario.mission.guidance.azimuth = *azimuth;
        std::cout << std::fixed << std::setprecision(2)
                  << "Launch azimuth: " << orbit::radToDeg(*azimuth) << " deg\n\n";
    }

    if (!savePath.empty()) {
        if (scenario.name.empty()) scenario.name = "Saved from orbitsim-cli";
        try {
            orbit::saveScenarioFile(scenario, savePath);
            std::cout << "Saved " << savePath << "\n\n";
        } catch (const orbit::ScenarioError& e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }

    if (scenario.sweep) {
        if (!csvPath.empty()) std::cerr << "note: --csv is ignored for sweeps\n";
        printSweep(scenario.mission, *scenario.sweep);
        return 0;
    }

    const orbit::SimResult result = orbit::simulate(scenario.mission);
    printResult(result);

    if (!csvPath.empty()) {
        if (!writeCsv(result, csvPath)) {
            std::cerr << "Could not write " << csvPath << '\n';
            return 1;
        }
        std::cout << "Wrote " << csvPath << '\n';
    }
    return 0;
}
