// SPDX-License-Identifier: GPL-3.0-or-later
// Dates and times for astronomy.
//
// Times are "epochs": seconds since J2000.0, the standard astronomical
// reference moment, 2000-01-01 12:00. We treat all times as UTC and ignore
// the ~1 minute difference between UTC and the uniform time scales
// astronomers use (TT, UT1). For this simulator's purposes that is a few
// hundred metres of Moon position, far below everything else's accuracy.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace orbit {

inline constexpr double kSecondsPerDay = 86400.0;
inline constexpr double kJ2000JulianDate = 2451545.0;

/// A calendar date and time (Gregorian calendar, UTC).
struct CalendarTime {
    int year = 2000;
    int month = 1; ///< 1–12
    int day = 1;   ///< 1–31
    int hour = 12;
    int minute = 0;
    double second = 0.0;
};

/// Julian date: days since noon on 1 January 4713 BC, the continuous day count
/// astronomers use. (Meeus, "Astronomical Algorithms", chapter 7.)
double julianDate(const CalendarTime& time);
CalendarTime calendarFromJulianDate(double jd);

double epochFromCalendar(const CalendarTime& time);
CalendarTime calendarFromEpoch(double epoch);

/// Julian centuries since J2000.0: the time variable of most astronomical
/// series (ephemerides, Earth rotation).
double julianCenturies(double epoch);

/// Parses "1969-07-16T13:32:00Z" (seconds and the Z are optional).
std::optional<double> parseIsoUtc(std::string_view text);

/// Formats as "1969-07-16T13:32:00Z", to the nearest second.
std::string formatIsoUtc(double epoch);

/// Earth rotation angle at an epoch (radians, 0–2π): how far the Earth has
/// turned relative to the stars, measured to the Greenwich meridian. It sets
/// where every launch site is in inertial space at that moment. (IERS
/// Conventions 2010, eq. 5.15.)
double earthRotationAngleAt(double epoch);

} // namespace orbit
