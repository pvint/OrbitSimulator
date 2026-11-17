// SPDX-License-Identifier: GPL-3.0-or-later
#include "run_set.hpp"

#include <algorithm>
#include <array>
#include <cmath>

QColor sweepColor(double t) {
    // The same stops as the prototype's sweep gradient.
    static const std::array<QColor, 6> stops{QColor(0x3b, 0x82, 0xf6), QColor(0x06, 0xb6, 0xd4),
                                             QColor(0x22, 0xc5, 0x5e), QColor(0xea, 0xb3, 0x08),
                                             QColor(0xf9, 0x73, 0x16), QColor(0xef, 0x44, 0x44)};
    const double x = std::clamp(t, 0.0, 1.0) * static_cast<double>(stops.size() - 1);
    const auto i = std::min(static_cast<std::size_t>(x), stops.size() - 2);
    const double f = x - static_cast<double>(i);
    const QColor& a = stops[i];
    const QColor& b = stops[i + 1];
    const auto mix = [f](int from, int to) {
        return static_cast<int>(std::lround(from + (to - from) * f));
    };
    return {mix(a.red(), b.red()), mix(a.green(), b.green()), mix(a.blue(), b.blue())};
}
