// SPDX-License-Identifier: GPL-3.0-or-later
// Command-line front end to the physics core: fly one mission (or a sweep)
// and print the outcome, or write the trajectory as CSV.
//
//   orbitsim-cli                                  default mission
//   orbitsim-cli --kick-angle 30 --latitude 0     change parameters (see --help)
//   orbitsim-cli --scenario scenarios/kourou-equatorial.json
//   orbitsim-cli --sweep kick-angle:5:45:9        a table, one row per flight
//   orbitsim-cli --csv flight.csv                 also write every sample
//   orbitsim-cli --vehicle natural-body           Apophis passing the Earth in 2029

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/earth.hpp"
#include "orbit/core/ephemeris.hpp"
#include "orbit/core/lunar_targeting.hpp"
#include "orbit/core/mission_parameters.hpp"
#include "orbit/core/parse.hpp"
#include "orbit/core/scenario.hpp"
#include "orbit/core/simulation.hpp"
#include "orbit/core/solar_system.hpp"
#include "orbit/core/sweep.hpp"
#include "orbit/core/version.hpp"

#include <algorithm>
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
                 "Rocket options (defaults: the prototype's rocket from Cape Canaveral):\n";
    const auto list = [](orbit::Vehicle vehicle) {
        for (const auto& p : orbit::missionParameters()) {
            if (p.vehicle != vehicle) continue;
            std::cout << "  --" << padded(p.key, 22) << padded(p.unit, 5) << p.help << '\n';
        }
    };
    list(orbit::Vehicle::Rocket);
    std::cout << "  --plan PLAN            orbit, burn-to-depletion, lunar-flyby, lunar-orbit, or\n"
                 "                         custom (needs a scenario with a program)\n"
                 "  --show-program         list the flight program's steps before flying\n"
                 "  --no-circularize       same as --plan burn-to-depletion\n"
                 "  --best-launch-hour     pick the time of day that lets a lunar flight go east\n"
                 "  --aim-moon             set the azimuth so the orbit plane meets the Moon\n"
                 "\nNatural body options (default: asteroid Apophis passing the Earth in 2029):\n"
                 "  --vehicle VEHICLE      rocket (the default) or natural-body\n"
                 "  --body-centre CENTRE   earth (the default) or sun, converting the body's\n"
                 "                         values to keep its path\n"
                 "  --body-given HOW       elements (at closest approach) or state-vector,\n"
                 "                         converting the body's values to keep its path\n"
                 "  --epoch-utc TIME       when the elements or state hold, e.g.\n"
                 "                         2029-04-13T21:45:04Z\n";
    list(orbit::Vehicle::NaturalBody);
    std::cout << "\nScenarios and sweeps:\n"
                 "  --scenario FILE        start from a scenario file (put it first; later\n"
                 "                         options change it)\n"
                 "  --sweep KEY:FROM:TO:STEPS  fly STEPS flights with parameter KEY from FROM\n"
                 "                         to TO, e.g. --sweep kick-angle:5:45:9. In a custom\n"
                 "                         program, step-N-value is step N's value\n"
                 "  --no-sweep             ignore the scenario's sweep\n"
                 "  --save-scenario FILE   save the mission (and sweep) as a scenario file\n"
                 "\nOutput options:\n"
                 "  --csv FILE             write every sample (SI units) to FILE (single flight)\n"
                 "  --version              print the version and exit\n"
                 "  --help                 show this help\n";
}

bool parseNumber(std::string_view text, double& out) {
    const std::optional<double> number = orbit::parseDouble(text);
    if (number) out = *number;
    return number.has_value();
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
    if (parts.size() != 4 || parts[0].empty() || !parseNumber(parts[1], spec.from) ||
        !parseNumber(parts[2], spec.to) || !parseNumber(parts[3], steps) || steps < 1 ||
        steps != std::floor(steps)) {
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

/// A moment of a flight as a UTC time.
std::string utc(const orbit::SimResult& r, double t) {
    return orbit::formatIsoUtc(r.startEpoch + t);
}

void printOrbitLine(const char* label, const orbit::OrbitalElements& o) {
    std::cout << label << std::setprecision(1) << "periapsis " << km(o.periapsis)
              << " from the centre, e = " << std::setprecision(4) << o.eccentricity
              << std::setprecision(1) << ", inclination " << orbit::radToDeg(o.inclination)
              << " deg\n";
}

void printBodyResult(const orbit::SimResult& r, const orbit::EncounterSummary& e) {
    const orbit::Sample& closest = e.closest;
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Outcome:          " << orbit::toString(r.outcome)
              << "  (simulation ended: " << orbit::toString(r.endReason) << ")\n"
              << "Time span:        " << utc(r, 0.0) << " to " << utc(r, r.duration) << " ("
              << duration(r.duration) << ")\n"
              << "Closest approach: " << km(orbit::norm(closest.state.r)) << " from the centre ("
              << km(closest.altitude()) << " up) at " << utc(r, closest.t) << ", "
              << std::setprecision(0) << closest.speed() << " m/s\n";
    if (e.arrivalSpeed > 0.0 || e.departureSpeed > 0.0) {
        std::cout << "Speed far away:   " << e.arrivalSpeed << " m/s in, " << e.departureSpeed
                  << " m/s out (v-infinity, if unbound)\n";
    }
    if (std::isfinite(e.impactParameter)) {
        std::cout << std::setprecision(1) << "Aimed at:         " << km(e.impactParameter)
                  << " from the centre (impact parameter: the miss without the Earth's "
                     "gravity)\n";
    }
    if (e.timeBound > 0.0) {
        std::cout << "Bound to Earth:   " << duration(e.timeBound) << '\n';
    }
    printOrbitLine("Orbit at start:   ", e.initialOrbit);
    printOrbitLine("Orbit at end:     ", e.finalOrbit);
    if (r.endReason == orbit::EndReason::Impact) {
        const orbit::Sample& last = r.samples.back();
        const orbit::GeoPoint where =
            orbit::groundPoint(last.state.r, orbit::earthRotationAngleAt(r.startEpoch + last.t));
        std::cout << std::setprecision(2)
                  << "Impact at:        " << std::abs(orbit::radToDeg(where.latitude))
                  << (where.latitude >= 0.0 ? " N, " : " S, ")
                  << std::abs(orbit::radToDeg(where.longitude))
                  << (where.longitude >= 0.0 ? " E" : " W") << ", " << std::setprecision(0)
                  << last.speed() << " m/s\n";
    }
    std::cout << std::setprecision(1) << "Moon:             closest approach "
              << km(r.lunar.closestApproachAltitude) << " above the surface, at "
              << utc(r, r.lunar.closestApproachTime) << '\n';
    if (!r.events.empty()) {
        std::cout << "Events:\n";
        for (const auto& ev : r.events) {
            std::cout << "  " << utc(r, ev.sample.t) << "  " << std::left << std::setw(48)
                      << ev.description << std::right << ' ' << km(orbit::norm(ev.sample.state.r))
                      << " from the centre, " << std::setprecision(0) << ev.sample.speed()
                      << " m/s\n"
                      << std::setprecision(1);
        }
    }
    std::cout << r.samples.size() << " samples\n";
}

std::string au(double metres) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(4) << metres / orbit::kAstronomicalUnit << " au";
    return os.str();
}

void printSolarOrbit(const char* label, const orbit::OrbitalElements& o) {
    std::cout << label << "perihelion " << au(o.periapsis) << ", e = " << std::setprecision(4)
              << o.eccentricity << std::setprecision(2)
              << ", i = " << orbit::radToDeg(o.inclination) << " deg";
    if (o.bound) {
        std::cout << ", a = " << au(o.semiMajorAxis) << std::setprecision(2) << ", period "
                  << o.period / (365.25 * 86400.0) << " years";
    }
    std::cout << '\n';
}

void printSolarResult(const orbit::SimResult& r, const orbit::SolarSummary& s) {
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Outcome:          " << orbit::toString(r.outcome)
              << (s.impacted.empty() ? std::string() : " (" + s.impacted + ")")
              << "  (simulation ended: " << orbit::toString(r.endReason) << ")\n"
              << "Time span:        " << utc(r, 0.0) << " to " << utc(r, r.duration) << " ("
              << duration(r.duration) << ")\n"
              << "Closest to Sun:   " << au(orbit::norm(s.perihelion.state.r)) << " at "
              << utc(r, s.perihelion.t) << ", " << std::setprecision(0) << s.perihelion.speed()
              << " m/s\n";
    printSolarOrbit("Orbit at start:   ", s.initialOrbit);
    printSolarOrbit("Orbit at end:     ", s.finalOrbit);
    std::cout << "Nearest planets:\n";
    for (const orbit::Planet planet : orbit::kAllPlanets) {
        const auto& a = s.approaches.at(static_cast<std::size_t>(planet));
        std::cout << "  " << std::left << std::setw(9) << orbit::planetName(planet) << std::right
                  << std::setw(14) << au(a.distance) << std::setw(18) << km(a.distance) << "  "
                  << utc(r, a.t) << '\n';
    }
    if (!r.events.empty()) {
        std::cout << "Events:\n";
        for (const auto& ev : r.events) {
            std::cout << "  " << utc(r, ev.sample.t) << "  " << std::left << std::setw(42)
                      << ev.description << std::right << ' ' << au(orbit::norm(ev.sample.state.r))
                      << " from the Sun, " << std::setprecision(0) << ev.sample.speed() << " m/s\n";
        }
    }
    std::cout << r.samples.size() << " samples\n";
}

void printResult(const orbit::SimResult& r) {
    using orbit::radToDeg;
    if (r.solar) {
        printSolarResult(r, *r.solar);
        return;
    }
    if (r.encounter) {
        printBodyResult(r, *r.encounter);
        return;
    }
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
    if (r.pressureLoss > 0.0) {
        std::cout << std::setprecision(0) << "Back-pressure:    " << r.pressureLoss
                  << " m/s lost to air pressing on the nozzle\n";
    }
    if (r.endReason == orbit::EndReason::Impact) {
        const orbit::Sample& last = r.samples.back();
        const orbit::GeoPoint where =
            orbit::groundPoint(last.state.r, orbit::earthRotationAngleAt(r.startEpoch + last.t));
        std::cout << std::setprecision(2)
                  << "Came down at:     " << std::abs(radToDeg(where.latitude))
                  << (where.latitude >= 0.0 ? " N, " : " S, ")
                  << std::abs(radToDeg(where.longitude)) << (where.longitude >= 0.0 ? " E" : " W")
                  << '\n';
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

void printProgram(const orbit::FlightProgram& program) {
    std::cout << "Flight program:\n";
    for (std::size_t i = 0; i < program.size(); ++i) {
        const orbit::FlightStep& s = program[i];
        std::cout << "  " << i + 1 << ". " << std::left << std::setw(24) << s.name
                  << (s.engineOn ? "burn  " : "coast ") << std::setw(34)
                  << (s.engineOn ? orbit::steeringLabel(s.steering) : std::string_view{})
                  << orbit::stepEndLabel(s.end);
        if (orbit::stepEndHasValue(s.end)) {
            const double value = orbit::stepValueToDisplay(s.end, s.value);
            std::cout << ' ' << std::fixed << std::setprecision(std::abs(value) >= 100.0 ? 0 : 1)
                      << value << ' ' << orbit::stepEndUnit(s.end);
        }
        std::cout << std::right << '\n';
    }
    std::cout << '\n';
}

void printBodySweep(const std::vector<double>& values,
                    const std::vector<orbit::Mission>& missions) {
    std::cout << std::right << std::setw(12) << "value" << "  " << std::left << std::setw(19)
              << "outcome" << std::right << std::setw(15) << "closest" << std::setw(23)
              << "when (UTC)" << std::setw(12) << "v-inf out" << std::setw(16) << "nearest Moon"
              << std::setw(12) << "bound" << '\n';
    for (std::size_t i = 0; i < missions.size(); ++i) {
        const orbit::SimResult r = orbit::simulate(missions[i]);
        if (!r.encounter) continue;
        const orbit::EncounterSummary& e = *r.encounter;
        std::cout << std::fixed << std::setprecision(3) << std::setw(12) << values[i] << "  "
                  << std::left << std::setw(19) << orbit::toString(r.outcome) << std::right
                  << std::setprecision(1) << std::setw(15) << km(orbit::norm(e.closest.state.r))
                  << std::setw(23) << utc(r, e.closest.t) << std::setprecision(0) << std::setw(8)
                  << e.departureSpeed << " m/s" << std::setprecision(1) << std::setw(16)
                  << km(r.lunar.closestApproachAltitude) << std::setw(12) << duration(e.timeBound)
                  << '\n';
    }
}

void printSweep(const orbit::Mission& base, const orbit::SweepSpec& spec,
                const orbit::SweepTarget& p) {
    const auto values = orbit::sweepValues(spec);
    const auto missions = orbit::sweepMissions(base, spec);
    if (base.vehicle == orbit::Vehicle::NaturalBody) {
        std::cout << "Sweep of " << p.title
                  << (p.unit.empty() ? std::string() : " (" + p.unit + ")") << "\n\n";
        printBodySweep(values, missions);
        return;
    }
    std::cout << "Sweep of " << p.title << (p.unit.empty() ? std::string() : " (" + p.unit + ")")
              << "\n\n"
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
    bool showProgram = false;

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
        if (arg == "--show-program") {
            showProgram = true;
            continue;
        }
        if (arg == "--no-sweep") {
            scenario.sweep.reset();
            continue;
        }
        const bool takesText = arg == "--csv" || arg == "--scenario" || arg == "--sweep" ||
                               arg == "--save-scenario" || arg == "--plan" || arg == "--vehicle" ||
                               arg == "--body-given" || arg == "--epoch-utc" ||
                               arg == "--body-centre";
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
                std::cerr << "--plan must be orbit, burn-to-depletion, lunar-flyby, lunar-orbit or "
                             "custom\n";
                return 1;
            }
        } else if (arg == "--vehicle") {
            if (!orbit::vehicleFromKey(value, scenario.mission.vehicle)) {
                std::cerr << "--vehicle must be rocket or natural-body\n";
                return 1;
            }
        } else if (arg == "--body-centre") {
            orbit::BodyCentre centre{};
            if (!orbit::bodyCentreFromKey(value, centre)) {
                std::cerr << "--body-centre must be earth or sun\n";
                return 1;
            }
            orbit::setBodyCentre(scenario.mission.body, centre);
        } else if (arg == "--body-given") {
            orbit::BodyGiven given{};
            if (!orbit::bodyGivenFromKey(value, given)) {
                std::cerr << "--body-given must be elements or state-vector\n";
                return 1;
            }
            orbit::setBodyGiven(scenario.mission.body, given);
        } else if (arg == "--epoch-utc") {
            const std::optional<double> epoch = orbit::parseIsoUtc(value);
            if (!epoch) {
                std::cerr << "--epoch-utc needs a time like 2029-04-13T21:45:04Z\n";
                return 1;
            }
            scenario.mission.body.epoch = *epoch;
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

    if (showProgram) printProgram(orbit::flightProgramFor(scenario.mission));
    if (scenario.sweep) {
        const std::optional<orbit::SweepTarget> target =
            orbit::findSweepTarget(scenario.mission, scenario.sweep->parameter);
        if (!target) {
            std::cerr << "Can't sweep \"" << scenario.sweep->parameter
                      << "\": not a parameter (see --help), nor a value of this mission's "
                         "custom program (step-N-value)\n";
            return 1;
        }
        if (!csvPath.empty()) std::cerr << "note: --csv is ignored for sweeps\n";
        printSweep(scenario.mission, *scenario.sweep, *target);
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
