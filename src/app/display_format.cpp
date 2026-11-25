// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_format.hpp"

#include "orbit/core/angles.hpp"
#include "orbit/core/astro_time.hpp"
#include "orbit/core/ephemeris.hpp"

#include <QCoreApplication>
#include <QDate>
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

QString distance(double metres) { return tr("%1 km").arg(number(metres / 1000.0, 0)); }

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

QString date(double epoch) {
    // The middle of the minute, so rounding in the calendar conversion
    // can't tip the date back into the day before at midnight.
    const orbit::CalendarTime t = orbit::calendarFromEpoch(std::floor(epoch / 60.0) * 60.0 + 1.0);
    return QLocale().toString(QDate(t.year, t.month, t.day), QStringLiteral("d MMM yyyy"));
}

QString longDistance(double metres) {
    if (metres < 1e6 * 1000.0) return distance(metres);
    if (metres < 0.1 * orbit::kAstronomicalUnit) {
        return tr("%1 million km").arg(number(metres / 1e9, metres < 1e10 ? 2 : 1));
    }
    return tr("%1 au").arg(number(metres / orbit::kAstronomicalUnit, 2));
}

QString pressure(double pascals) { return tr("%1 kPa").arg(number(pascals / 1000.0, 1)); }

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
    case orbit::Outcome::LunarImpact:
        return tr("Lunar impact");
    case orbit::Outcome::LunarOrbit:
        return tr("Lunar orbit");
    case orbit::Outcome::Impact:
        return tr("Impact");
    case orbit::Outcome::Flyby:
        return tr("Flyby");
    case orbit::Outcome::TemporaryCapture:
        return tr("Temporary capture");
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
    case orbit::Outcome::LunarImpact:
        return {0xe8, 0x79, 0xf9};
    case orbit::Outcome::LunarOrbit:
        return {0xe2, 0xe8, 0xf0}; // moonlight
    case orbit::Outcome::Impact:
        return {0xf8, 0x71, 0x71};
    case orbit::Outcome::Flyby:
        return {0x7d, 0xd3, 0xfc};
    case orbit::Outcome::TemporaryCapture:
        return {0xfb, 0xbf, 0x24};
    }
    return Qt::white;
}

} // namespace display
