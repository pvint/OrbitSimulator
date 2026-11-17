// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/sweep.hpp"

#include "orbit/core/mission_parameters.hpp"

#include <stdexcept>

namespace orbit {

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
    const ParameterInfo* param = findParameter(spec.parameter);
    if (param == nullptr) {
        throw std::invalid_argument("unknown sweep parameter: " + spec.parameter);
    }
    std::vector<Mission> missions;
    for (const double value : sweepValues(spec)) {
        Mission m = base;
        param->set(m, value);
        missions.push_back(m);
    }
    return missions;
}

} // namespace orbit
