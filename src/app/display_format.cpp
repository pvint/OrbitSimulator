// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_format.hpp"

#include "orbit/core/angles.hpp"

#include <QCoreApplication>
#include <QLocale>

#include <cmath>

namespace display {

namespace {
QString tr(const char* text) { return QCoreApplication::translate("display", text); }

QString number(double value, int decimals) { return QLocale().toString(value, 'f', decimals); }
} // namespace

QString altitude(double metres) {
    if (!std::isfinite(metres)) return QStringLiteral("∞");
    const double km = metres / 1000.0;
    return tr("%1 km").arg(number(km, std::abs(km) < 10.0 ? 2 : 1));
}

QString duration(double seconds) {
    if (!std::isfinite(seconds)) return QStringLiteral("—");
    if (seconds < 120.0) return tr("%1 s").arg(number(seconds, 1));
    if (seconds < 7200.0) return tr("%1 min").arg(number(seconds / 60.0, 1));
    if (seconds < 3.0 * 86400.0) {
        const auto totalMinutes = static_cast<long long>(std::lround(seconds / 60.0));
        return tr("%1 h %2 min").arg(totalMinutes / 60).arg(totalMinutes % 60);
    }
    return tr("%1 days").arg(number(seconds / 86400.0, 1));
}

QString speed(double metresPerSecond) { return tr("%1 m/s").arg(number(metresPerSecond, 0)); }

QString mass(double kilograms) {
    if (std::abs(kilograms) >= 1000.0) return tr("%1 t").arg(number(kilograms / 1000.0, 1));
    return tr("%1 kg").arg(number(kilograms, 0));
}

QString angle(double radians) { return tr("%1°").arg(number(orbit::radToDeg(radians), 2)); }

QString outcomeLabel(orbit::Outcome outcome) {
    switch (outcome) {
    case orbit::Outcome::Crashed:
        return tr("Crashed");
    case orbit::Outcome::Suborbital:
        return tr("Suborbital");
    case orbit::Outcome::Orbit:
        return tr("Orbit");
    case orbit::Outcome::Escape:
        return tr("Escape");
    }
    return {};
}

// The same colours as the prototype, chosen to read well on a dark background.
QColor outcomeColor(orbit::Outcome outcome) {
    switch (outcome) {
    case orbit::Outcome::Crashed:
        return {0xf8, 0x71, 0x71};
    case orbit::Outcome::Suborbital:
        return {0xfb, 0x92, 0x3c};
    case orbit::Outcome::Orbit:
        return {0x4a, 0xde, 0x80};
    case orbit::Outcome::Escape:
        return {0xc0, 0x84, 0xfc};
    }
    return Qt::white;
}

QString eventLabel(orbit::EventType type) {
    switch (type) {
    case orbit::EventType::Cutoff:
        return tr("Engine cutoff (apoapsis at target)");
    case orbit::EventType::CircularizationStart:
        return tr("Circularization burn start");
    case orbit::EventType::CircularizationEnd:
        return tr("Circularization burn end");
    case orbit::EventType::Burnout:
        return tr("Propellant exhausted");
    }
    return {};
}

} // namespace display
