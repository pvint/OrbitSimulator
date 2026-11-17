// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/astro_time.hpp"

#include "orbit/core/angles.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace orbit {

double julianDate(const CalendarTime& time) {
    int year = time.year;
    int month = time.month;
    // January and February count as months 13 and 14 of the previous year, so
    // the leap day falls at the end of the year.
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    const int century = year / 100;
    const int gregorianCorrection = 2 - century + century / 4;
    const double dayFraction =
        time.day + (time.hour + (time.minute + time.second / 60.0) / 60.0) / 24.0;
    return std::floor(365.25 * (year + 4716)) + std::floor(30.6001 * (month + 1)) + dayFraction +
           gregorianCorrection - 1524.5;
}

CalendarTime calendarFromJulianDate(double jd) {
    const double shifted = jd + 0.5;
    const double z = std::floor(shifted);
    const double f = shifted - z;
    double a = z;
    if (z >= 2299161.0) { // Gregorian calendar
        const double alpha = std::floor((z - 1867216.25) / 36524.25);
        a = z + 1.0 + alpha - std::floor(alpha / 4.0);
    }
    const double b = a + 1524.0;
    const double c = std::floor((b - 122.1) / 365.25);
    const double d = std::floor(365.25 * c);
    const double e = std::floor((b - d) / 30.6001);

    const double dayWithFraction = b - d - std::floor(30.6001 * e) + f;
    CalendarTime t;
    t.month = static_cast<int>(e < 14.0 ? e - 1.0 : e - 13.0);
    t.year = static_cast<int>(t.month > 2 ? c - 4716.0 : c - 4715.0);
    t.day = static_cast<int>(std::floor(dayWithFraction));
    double seconds = (dayWithFraction - t.day) * kSecondsPerDay;
    t.hour = static_cast<int>(seconds / 3600.0);
    seconds -= t.hour * 3600.0;
    t.minute = static_cast<int>(seconds / 60.0);
    t.second = seconds - t.minute * 60.0;
    return t;
}

double epochFromCalendar(const CalendarTime& time) {
    return (julianDate(time) - kJ2000JulianDate) * kSecondsPerDay;
}

CalendarTime calendarFromEpoch(double epoch) {
    return calendarFromJulianDate(kJ2000JulianDate + epoch / kSecondsPerDay);
}

double julianCenturies(double epoch) { return epoch / (kSecondsPerDay * 36525.0); }

std::optional<double> parseIsoUtc(std::string_view text) {
    // YYYY-MM-DDTHH:MM[:SS][Z]
    const auto number = [&](std::size_t pos, std::size_t len, int& out) {
        if (pos + len > text.size()) return false;
        const char* first = text.data() + pos; // NOLINT(bugprone-suspicious-stringview-data-usage)
        const auto [ptr, ec] = std::from_chars(first, first + len, out);
        return ec == std::errc{} && ptr == first + len;
    };
    CalendarTime t;
    int second = 0;
    if (text.size() < 16 || text[4] != '-' || text[7] != '-' ||
        (text[10] != 'T' && text[10] != ' ') || text[13] != ':' || !number(0, 4, t.year) ||
        !number(5, 2, t.month) || !number(8, 2, t.day) || !number(11, 2, t.hour) ||
        !number(14, 2, t.minute)) {
        return std::nullopt;
    }
    std::size_t pos = 16;
    if (pos < text.size() && text[pos] == ':') {
        if (!number(pos + 1, 2, second)) return std::nullopt;
        pos += 3;
    }
    if (pos < text.size() && text[pos] == 'Z') ++pos;
    if (pos != text.size()) return std::nullopt;
    if (t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31 || t.hour > 23 || t.minute > 59 ||
        second > 60) {
        return std::nullopt;
    }
    t.second = second;
    return epochFromCalendar(t);
}

std::string formatIsoUtc(double epoch) {
    // Work in whole seconds since midnight, so rounding can never produce
    // "13:31:60".
    const double rounded = std::round(epoch);
    CalendarTime date = calendarFromEpoch(rounded);
    const auto secondsOfDay = [&] {
        return std::llround(rounded -
                            epochFromCalendar({date.year, date.month, date.day, 0, 0, 0.0}));
    };
    long long sod = secondsOfDay();
    // At midnight the date can come out one day off by a rounding hair; then
    // the seconds-of-day land just outside 0–86399. Step the date and redo.
    if (sod >= 86400) {
        date = calendarFromEpoch(rounded + 1.0);
        sod = secondsOfDay();
    } else if (sod < 0) {
        date = calendarFromEpoch(rounded - 1.0);
        sod = secondsOfDay();
    }
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02dT%02lld:%02lld:%02lldZ", // NOLINT
                  date.year, date.month, date.day, sod / 3600, (sod / 60) % 60, sod % 60);
    return buffer.data();
}

double earthRotationAngleAt(double epoch) {
    const double daysSinceJ2000 = epoch / kSecondsPerDay;
    // Turns: 0.779... at J2000, then 1.0027... turns per day (a solar day is a
    // little longer than one turn relative to the stars).
    const double turns = 0.7790572732640 + 1.00273781191135448 * daysSinceJ2000;
    return wrapTwoPi(kTwoPi * (turns - std::floor(turns)));
}

} // namespace orbit
