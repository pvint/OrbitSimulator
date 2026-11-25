// SPDX-License-Identifier: GPL-3.0-or-later
// Turning physics numbers (SI units, radians) into text for people.
#pragma once

#include "orbit/core/simulation.hpp"

#include <QColor>
#include <QString>

namespace display {

QString altitude(double metres);       ///< "199.7 km"; "∞" for an escape's apoapsis
QString distance(double metres);       ///< "1,452 km"
QString duration(double seconds);      ///< "45.2 s", "88.3 min", "3 h 12 min", "2.4 days"
QString speed(double metresPerSecond); ///< "7,789 m/s"
QString mass(double kilograms);        ///< "1.2 t" or "850 kg"
QString angle(double radians);         ///< "28.50°"
QString pressure(double pascals);      ///< "63.0 kPa"
QString date(double epoch);            ///< "12 Mar 2027" (epoch: s since J2000, UTC)
QString longDistance(double metres);   ///< "45,200 km", "1.23 million km", "1.52 au"

QString outcomeLabel(orbit::Outcome outcome); ///< "Orbit", "Crashed", ...
QColor outcomeColor(orbit::Outcome outcome);

} // namespace display
