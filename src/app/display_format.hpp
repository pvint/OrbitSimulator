// SPDX-License-Identifier: GPL-3.0-or-later
// Turning physics numbers (SI units, radians) into text for people.
#pragma once

#include "orbit/core/simulation.hpp"

#include <QColor>
#include <QString>

namespace display {

QString altitude(double metres);       ///< "199.7 km"; "∞" for an escape's apoapsis
QString duration(double seconds);      ///< "45.2 s", "88.3 min", "3 h 12 min", "2.4 days"
QString speed(double metresPerSecond); ///< "7,789 m/s"
QString mass(double kilograms);        ///< "1.2 t" or "850 kg"
QString angle(double radians);         ///< "28.50°"

QString outcomeLabel(orbit::Outcome outcome); ///< "Orbit", "Crashed", ...
QColor outcomeColor(orbit::Outcome outcome);
QString eventLabel(orbit::EventType type);

} // namespace display
