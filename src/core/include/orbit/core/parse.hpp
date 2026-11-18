// SPDX-License-Identifier: GPL-3.0-or-later
// Reading numbers from text, the same way everywhere.
#pragma once

#include <optional>
#include <string_view>

namespace orbit {

/// The number in `text` ("28.5", "-1e3"), or nothing if `text` is anything
/// else, including surrounding spaces. Always uses '.' for the decimal point,
/// whatever the user's locale.
std::optional<double> parseDouble(std::string_view text);

} // namespace orbit
