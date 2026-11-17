// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/demo.hpp"

#include <cmath>
#include <nlohmann/json.hpp>

namespace orbit {

namespace {

using nlohmann::json;

std::string stringField(const json& object, const char* key, bool required) {
    const auto it = object.find(key);
    if (it == object.end()) {
        if (required) throw DemoError(std::string(R"(a step needs ")") + key + '"');
        return {};
    }
    if (!it->is_string()) throw DemoError('"' + std::string(key) + R"(" must be text)");
    return it->get<std::string>();
}

double number(const json& object, const char* key, double fallback) {
    const auto it = object.find(key);
    if (it == object.end()) return fallback;
    if (!it->is_number() || !std::isfinite(it->get<double>()) || it->get<double>() < 0.0) {
        throw DemoError('"' + std::string(key) + R"(" must be a number, 0 or more)");
    }
    return it->get<double>();
}

DemoView parseView(const std::string& name) {
    if (name.empty() || name == "auto") return DemoView::Auto;
    if (name == "launch-site") return DemoView::LaunchSite;
    if (name == "north") return DemoView::North;
    if (name == "moon") return DemoView::Moon;
    throw DemoError(R"("view" must be auto, launch-site, north or moon, not ")" + name + '"');
}

DemoCentre parseCentre(const std::string& name) {
    if (name.empty() || name == "earth") return DemoCentre::Earth;
    if (name == "rocket") return DemoCentre::Rocket;
    if (name == "moon") return DemoCentre::Moon;
    throw DemoError(R"("centre" must be earth, rocket or moon, not ")" + name + '"');
}

} // namespace

Demo demoFromJson(std::string_view text) {
    json doc;
    try {
        doc = json::parse(text);
    } catch (const json::parse_error& e) {
        throw DemoError(std::string("not valid JSON: ") + e.what());
    }
    if (!doc.is_object() || doc.value("format", std::string{}) != "orbitsim-demo") {
        throw DemoError(R"(not an Orbit Simulator demo ("format" should be "orbitsim-demo"))");
    }
    const auto version = doc.find("version");
    if (version == doc.end() || !version->is_number_integer() ||
        version->get<int>() > kDemoFormatVersion) {
        throw DemoError("unsupported demo version");
    }

    Demo demo;
    demo.name = stringField(doc, "name", false);
    const auto steps = doc.find("steps");
    if (steps == doc.end() || !steps->is_array() || steps->empty()) {
        throw DemoError("a demo needs a non-empty \"steps\" list");
    }
    for (const json& s : *steps) {
        if (!s.is_object()) throw DemoError("each step must be an object");
        DemoStep step;
        step.title = stringField(s, "title", true);
        step.caption = stringField(s, "caption", true);
        step.scenario = stringField(s, "scenario", true);
        step.view = parseView(stringField(s, "view", false));
        step.centre = parseCentre(stringField(s, "centre", false));
        step.distance = number(s, "distance", 0.0);
        step.speed = number(s, "speed", 0.0);
        step.seconds = number(s, "seconds", 20.0);
        step.startTime = number(s, "start-hours", 0.0) * 3600.0;
        demo.steps.push_back(std::move(step));
    }
    return demo;
}

} // namespace orbit
