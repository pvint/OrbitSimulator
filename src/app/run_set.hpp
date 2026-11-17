// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "orbit/core/simulation.hpp"
#include "orbit/core/sweep.hpp"

#include <QColor>

#include <memory>
#include <optional>
#include <vector>

/// One simulated flight as the app shows it.
struct FlightRun {
    double value = 0.0; ///< the swept parameter's value (display units); 0 if not a sweep
    QColor color;       ///< its colour in the 3D view, graphs and legend
    std::shared_ptr<const orbit::SimResult> result;
};

/// The outcome of one simulation request: a single flight, or every flight
/// of a sweep.
struct RunSet {
    std::optional<orbit::SweepSpec> sweep;
    std::vector<FlightRun> runs;

    bool isSweep() const { return sweep.has_value(); }
};

/// Colour for position t ∈ [0, 1] along a sweep: blue → cyan → green →
/// yellow → orange → red, readable on a dark background.
QColor sweepColor(double t);
