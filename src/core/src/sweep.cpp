// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/sweep.hpp"

#include "orbit/core/mission_parameters.hpp"

#include <stdexcept>

namespace orbit {

namespace {

/// Slider-like ranges for a program step's value, by its unit.
SweepTarget stepTarget(std::size_t index, const FlightStep& step) {
    SweepTarget target;
    target.key = "step-" + std::to_string(index + 1) + "-value";
    target.title = "Step " + std::to_string(index + 1) + " (" + step.name +
                   "): " + std::string(stepEndLabel(step.end));
    target.unit = stepEndUnit(step.end);
    if (target.unit == "km") {
        target.max = 1e6;
    } else if (target.unit == "°") {
        target.min = -180.0;
        target.max = 360.0;
        target.step = 0.1;
    } else if (target.unit == "rev") {
        target.max = 100.0;
        target.step = 0.1;
    } else {
        target.max = 1e7; // s
    }
    const StepEnd end = step.end;
    target.get = [index, end](const Mission& m) {
        return index < m.program.size() ? stepValueToDisplay(end, m.program[index].value) : 0.0;
    };
    target.set = [index, end](Mission& m, double v) {
        if (index < m.program.size()) m.program[index].value = stepValueFromDisplay(end, v);
    };
    return target;
}

} // namespace

std::vector<SweepTarget> sweepTargets(const Mission& mission) {
    std::vector<SweepTarget> targets;
    for (const ParameterInfo& p : missionParameters()) {
        if (!parameterInUse(p, mission)) continue; // a rocket's thrust, for a natural body
        targets.push_back({std::string(p.key), parameterTitle(p), std::string(p.unit), p.min, p.max,
                           p.step, p.get, p.set});
    }
    if (mission.vehicle == Vehicle::Rocket && mission.plan == FlightPlan::Custom) {
        for (std::size_t i = 0; i < mission.program.size(); ++i) {
            if (stepEndHasValue(mission.program[i].end)) {
                targets.push_back(stepTarget(i, mission.program[i]));
            }
        }
    }
    return targets;
}

std::optional<SweepTarget> findSweepTarget(const Mission& mission, std::string_view key) {
    for (SweepTarget& target : sweepTargets(mission)) {
        if (target.key == key) return std::move(target);
    }
    return std::nullopt;
}

std::vector<double> sweepValues(const SweepSpec& spec) {
    if (spec.steps <= 1) return {spec.from};
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(spec.steps));
    for (int i = 0; i < spec.steps; ++i) {
        values.push_back(spec.from + (spec.to - spec.from) * i / (spec.steps - 1));
    }
    return values;
}

std::vector<Mission> sweepMissions(const Mission& base, const SweepSpec& spec) {
    const std::optional<SweepTarget> target = findSweepTarget(base, spec.parameter);
    if (!target) throw std::invalid_argument("unknown sweep parameter: " + spec.parameter);
    std::vector<Mission> missions;
    for (const double value : sweepValues(spec)) {
        Mission m = base;
        target->set(m, value);
        missions.push_back(m);
    }
    return missions;
}

} // namespace orbit
