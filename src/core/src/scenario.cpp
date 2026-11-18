// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/scenario.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/mission_parameters.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace orbit {

namespace {

using nlohmann::json;

constexpr std::string_view kPlanKey = "plan";
/// Older files: "auto-circularize": false meant burn until empty.
constexpr std::string_view kAutoCircularizeKey = "auto-circularize";
constexpr std::string_view kLaunchUtcKey = "launch-utc";
constexpr std::string_view kProgramKey = "program";

/// Rounds to 12 significant figures, so values that went through a unit
/// conversion are saved as 28.5 rather than 28.500000000000004.
double tidy(double value) {
    std::array<char, 32> buffer{};
    const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                         std::chars_format::general, 12);
    if (ec != std::errc{}) return value;
    double result = value;
    std::from_chars(buffer.data(), end, result);
    return result;
}

double requireNumber(const json& value, std::string_view where) {
    if (!value.is_number()) {
        throw ScenarioError(std::string(where) + " must be a number");
    }
    const double number = value.get<double>();
    if (!std::isfinite(number)) throw ScenarioError(std::string(where) + " must be finite");
    return number;
}

std::string optionalString(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) return {};
    if (!it->is_string()) throw ScenarioError(std::string(key) + " must be text");
    return it->get<std::string>();
}

/// What a key left out of a file of this format version means: the
/// defaults of the program that wrote it. That way an old file flies
/// exactly as it always did.
Mission defaultsForVersion(int version) {
    Mission mission = defaultMission();
    if (version < 5) mission.rocket.separationCoast = 0.0; // stages lit at once
    if (version < 4) {
        // Before version 4 the engines lost nothing in thick air, and the
        // first stage's thrust was 1.2 MN (now 1.36 MN in a vacuum, which is
        // 1.2 MN at sea level).
        for (Stage& stage : mission.rocket.stages) stage.seaLevelLoss = 0.0;
        mission.rocket.stages[0].thrust = 1.2e6;
    }
    if (version < 2) {
        // Version 1 predates the atmosphere: no drag, and the 25° kick
        // that was tuned for flying in a vacuum.
        mission.rocket.dragCoefficient = 0.0;
        mission.guidance.kickAngle = degToRad(25.0);
    }
    return mission;
}

/// One step of a custom program: {"name": "Deorbit burn", "engine": true,
/// "steering": "retrograde", "until": "periapsis-altitude", "value": 50,
/// "timeout": 600}. The value is in the end condition's display unit.
FlightStep readStep(const json& object, const std::string& where) {
    if (!object.is_object()) throw ScenarioError(where + " must be an object");
    FlightStep step;
    step.name = optionalString(object, "name");
    if (const auto it = object.find("engine"); it != object.end()) {
        if (!it->is_boolean()) throw ScenarioError(where + ".engine must be true or false");
        step.engineOn = it->get<bool>();
    }
    const std::string steering = optionalString(object, "steering");
    if (!steering.empty() && !steeringFromKey(steering, step.steering)) {
        throw ScenarioError(where + ".steering \"" + steering + "\" is not a steering mode");
    }
    const std::string until = optionalString(object, "until");
    if (!stepEndFromKey(until, step.end)) {
        throw ScenarioError(where + ".until \"" + until + "\" is not an end condition");
    }
    if (const auto it = object.find("value"); it != object.end()) {
        step.value = stepValueFromDisplay(step.end, requireNumber(*it, where + ".value"));
    }
    if (const auto it = object.find("timeout"); it != object.end()) {
        step.timeout = requireNumber(*it, where + ".timeout");
    }
    if (const auto it = object.find("look-ahead"); it != object.end()) {
        step.lookAhead = requireNumber(*it, where + ".look-ahead");
    }
    return step;
}

nlohmann::ordered_json writeStep(const FlightStep& step) {
    nlohmann::ordered_json object;
    object["name"] = step.name;
    object["engine"] = step.engineOn;
    object["steering"] = steeringKey(step.steering);
    object["until"] = stepEndKey(step.end);
    if (stepEndHasValue(step.end)) object["value"] = tidy(stepValueToDisplay(step.end, step.value));
    if (std::isfinite(step.timeout)) object["timeout"] = tidy(step.timeout);
    if (step.lookAhead != 0.0) object["look-ahead"] = tidy(step.lookAhead);
    return object;
}

Mission readMission(const json& object, int version, std::vector<std::string>& warnings) {
    if (!object.is_object()) throw ScenarioError("\"mission\" must be an object");
    Mission mission = defaultsForVersion(version);

    // The launch moment first, so a "launch-hour" given as well can adjust it.
    if (const auto it = object.find(std::string(kLaunchUtcKey)); it != object.end()) {
        const std::optional<double> epoch =
            it->is_string() ? parseIsoUtc(it->get<std::string>()) : std::nullopt;
        if (!epoch) {
            throw ScenarioError(R"(mission.launch-utc must be a time like "1969-07-16T13:32:00Z")");
        }
        mission.launchEpoch = *epoch;
    }

    // Ordinary parameters first, then derived ones (burn time), which are
    // computed from the others.
    for (const bool derivedPass : {false, true}) {
        for (const ParameterInfo& p : missionParameters()) {
            if (p.derived != derivedPass) continue;
            const auto it = object.find(std::string(p.key));
            if (it == object.end()) continue;
            p.set(mission, requireNumber(*it, "mission." + std::string(p.key)));
        }
    }
    if (const auto it = object.find(std::string(kAutoCircularizeKey)); it != object.end()) {
        if (!it->is_boolean()) {
            throw ScenarioError("mission.auto-circularize must be true or false");
        }
        mission.plan = it->get<bool>() ? FlightPlan::Orbit : FlightPlan::BurnToDepletion;
    }
    if (const auto it = object.find(std::string(kPlanKey)); it != object.end()) {
        if (!it->is_string() || !planFromKey(it->get<std::string>(), mission.plan)) {
            throw ScenarioError(R"(mission.plan must be "orbit", "burn-to-depletion", )"
                                R"("lunar-flyby", "lunar-orbit" or "custom")");
        }
    }
    if (const auto it = object.find(std::string(kProgramKey)); it != object.end()) {
        if (!it->is_array()) throw ScenarioError("mission.program must be a list of steps");
        for (std::size_t i = 0; i < it->size(); ++i) {
            mission.program.push_back(
                readStep((*it)[i], "mission.program[" + std::to_string(i) + "]"));
        }
    }

    for (const auto& [key, value] : object.items()) {
        if (key != kAutoCircularizeKey && key != kPlanKey && key != kLaunchUtcKey &&
            key != kProgramKey && findParameter(key) == nullptr) {
            warnings.push_back("ignored unknown mission parameter \"" + key + "\"");
        }
    }
    return mission;
}

SweepSpec readSweep(const json& object, const Mission& mission) {
    if (!object.is_object()) throw ScenarioError("\"sweep\" must be an object");
    SweepSpec spec;
    spec.parameter = optionalString(object, "parameter");
    if (!findSweepTarget(mission, spec.parameter)) {
        throw ScenarioError("sweep.parameter \"" + spec.parameter + "\" is not a known parameter");
    }
    for (const char* key : {"from", "to", "steps"}) {
        if (!object.contains(key)) throw ScenarioError(std::string("sweep.") + key + " is missing");
    }
    spec.from = requireNumber(object.at("from"), "sweep.from");
    spec.to = requireNumber(object.at("to"), "sweep.to");
    const double steps = requireNumber(object.at("steps"), "sweep.steps");
    if (steps < 1 || steps > 1000 || steps != std::floor(steps)) {
        throw ScenarioError("sweep.steps must be a whole number from 1 to 1000");
    }
    spec.steps = static_cast<int>(steps);
    return spec;
}

} // namespace

LoadedScenario scenarioFromJson(std::string_view text) {
    json doc;
    try {
        doc = json::parse(text);
    } catch (const json::parse_error& e) {
        throw ScenarioError(std::string("not valid JSON: ") + e.what());
    }
    if (!doc.is_object()) throw ScenarioError("a scenario must be a JSON object");

    if (doc.value("format", std::string{}) != kScenarioFormatName) {
        throw ScenarioError(R"(not an Orbit Simulator scenario ("format" should be ")" +
                            std::string(kScenarioFormatName) + "\")");
    }
    const auto version = doc.find("version");
    if (version == doc.end() || !version->is_number_integer()) {
        throw ScenarioError("\"version\" is missing or not a whole number");
    }
    const int formatVersion = version->get<int>();
    if (formatVersion > kScenarioFormatVersion) {
        throw ScenarioError("this scenario needs a newer version of Orbit Simulator (format " +
                            std::to_string(formatVersion) + ")");
    }
    // Older versions are upgraded as they are read: see defaultsForVersion().

    LoadedScenario loaded;
    Scenario& s = loaded.scenario;
    s.name = optionalString(doc, "name");
    s.description = optionalString(doc, "description");
    if (const auto it = doc.find("mission"); it != doc.end()) {
        s.mission = readMission(*it, formatVersion, loaded.warnings);
    } else {
        s.mission = defaultsForVersion(formatVersion);
    }
    if (const auto it = doc.find("sweep"); it != doc.end() && !it->is_null()) {
        s.sweep = readSweep(*it, s.mission);
    }
    for (const auto& [key, value] : doc.items()) {
        if (key != "format" && key != "version" && key != "name" && key != "description" &&
            key != "mission" && key != "sweep") {
            loaded.warnings.push_back("ignored unknown section \"" + key + "\"");
        }
    }
    return loaded;
}

std::string scenarioToJson(const Scenario& scenario) {
    // ordered_json keeps keys in the parameter table's order, which reads better
    // than alphabetical.
    nlohmann::ordered_json mission = nlohmann::ordered_json::object();
    for (const ParameterInfo& p : missionParameters()) {
        if (p.derived || !p.inScenario) continue; // follows from the others
        mission[std::string(p.key)] = tidy(p.get(scenario.mission));
    }
    mission[std::string(kPlanKey)] = planKey(scenario.mission.plan);
    mission[std::string(kLaunchUtcKey)] = formatIsoUtc(scenario.mission.launchEpoch);
    if (scenario.mission.plan == FlightPlan::Custom) {
        nlohmann::ordered_json steps = nlohmann::ordered_json::array();
        for (const FlightStep& step : scenario.mission.program) steps.push_back(writeStep(step));
        mission[std::string(kProgramKey)] = steps;
    }

    nlohmann::ordered_json doc;
    doc["format"] = kScenarioFormatName;
    doc["version"] = kScenarioFormatVersion;
    doc["name"] = scenario.name;
    if (!scenario.description.empty()) doc["description"] = scenario.description;
    doc["mission"] = mission;
    if (scenario.sweep) {
        doc["sweep"] = {{"parameter", scenario.sweep->parameter},
                        {"from", scenario.sweep->from},
                        {"to", scenario.sweep->to},
                        {"steps", scenario.sweep->steps}};
    }
    return doc.dump(2) + "\n";
}

LoadedScenario loadScenarioFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ScenarioError("cannot open " + path.string());
    std::ostringstream text;
    text << in.rdbuf();
    return scenarioFromJson(text.str());
}

void saveScenarioFile(const Scenario& scenario, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    out << scenarioToJson(scenario);
    if (!out) throw ScenarioError("cannot write " + path.string());
}

} // namespace orbit
