// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QUrl>

/// Links to the project's GitHub issue forms (.github/ISSUE_TEMPLATE/),
/// with what the app knows filled in, so a report says exactly which build
/// and system it came from.
namespace issues {

/// The repository's address.
inline constexpr const char* kRepository = "https://github.com/pvint/OrbitSimulator";

/// "0.1.0 (v0.1.0-12-g3e2d75d)": the version, and the commit it was built
/// from when that is known (git describe, recorded when CMake configured).
QString buildDescription();

/// "Gentoo Linux, Qt 6.11.0 (x86_64)".
QString systemDescription();

/// A new "Something's wrong" issue, with the version and system filled in.
QUrl reportProblemUrl();

/// A new "An idea" issue.
QUrl suggestIdeaUrl();

} // namespace issues
