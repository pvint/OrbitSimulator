// SPDX-License-Identifier: GPL-3.0-or-later
// Command-line front end to the physics core: fly one mission and print the
// outcome, or write the trajectory as CSV.
//
//   orbitsim-cli                          default mission
//   orbitsim-cli --kick-angle 30 --latitude 0   change parameters (see --help)
//   orbitsim-cli --csv flight.csv         also write every sample

#include "orbit/core/angles.hpp"
#include "orbit/core/mission_parameters.hpp"
#include "orbit/core/simulation.hpp"
#include "orbit/core/version.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
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
    std::cout << "  --no-circularize       burn until the propellant runs out\n"
                 "\nOutput options:\n"
                 "  --csv FILE             write every sample (SI units) to FILE\n"
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
    } else {
        os << seconds / 60.0 << " min";
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
    if (!r.events.empty()) {
        std::cout << "Events:\n";
        for (const auto& e : r.events) {
            std::cout << std::setprecision(1) << "  T+" << std::setw(7) << e.sample.t << " s  "
                      << std::left << std::setw(36) << orbit::toString(e.type) << std::right
                      << " alt " << km(e.sample.altitude()) << ", speed " << std::setprecision(0)
                      << e.sample.speed() << " m/s\n";
        }
    }
    std::cout << r.samples.size() << " samples\n";
}

bool writeCsv(const orbit::SimResult& result, const std::string& path) {
    std::ofstream out(path);
    if (!out) return false;
    out << "t,x,y,z,vx,vy,vz,mass,altitude,speed,burning\n" << std::setprecision(10);
    for (const auto& s : result.samples) {
        const auto& y = s.state;
        out << s.t << ',' << y.r.x << ',' << y.r.y << ',' << y.r.z << ',' << y.v.x << ',' << y.v.y
            << ',' << y.v.z << ',' << y.m << ',' << s.altitude() << ',' << s.speed() << ','
            << (s.burning ? 1 : 0) << '\n';
    }
    return static_cast<bool>(out);
}

} // namespace

int main(int argc, char* argv[]) {
    const std::span args(argv + 1, static_cast<std::size_t>(argc - 1));
    orbit::Mission mission = orbit::defaultMission();
    std::string csvPath;

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
            mission.autoCircularize = false;
            continue;
        }
        const orbit::ParameterInfo* param =
            arg.starts_with("--") ? orbit::findParameter(arg.substr(2)) : nullptr;
        if (arg != "--csv" && param == nullptr) {
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
            continue;
        }
        double number = 0.0;
        if (!parseNumber(value, number)) {
            std::cerr << "Not a number for " << arg << ": " << value << '\n';
            return 1;
        }
        param->set(mission, number);
    }

    const orbit::SimResult result = orbit::simulate(mission);
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
