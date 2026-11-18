// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/parse.hpp"

#include <locale>
#include <sstream>
#include <string>

namespace orbit {

std::optional<double> parseDouble(std::string_view text) {
    // Not std::from_chars: Apple's C++ library only has its floating-point
    // version on recent macOS. Not strtod either: it follows the C locale,
    // which the Qt app sets to the user's (a German one reads "28.5" as 28).
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    double value = 0.0;
    in >> std::noskipws >> value;
    if (in.fail() || in.peek() != std::istringstream::traits_type::eof()) return std::nullopt;
    return value;
}

} // namespace orbit
