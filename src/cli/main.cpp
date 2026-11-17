// SPDX-License-Identifier: GPL-3.0-or-later
// Command-line front end to the physics core.
//
// For now it only reports its version and the constants it uses. From M1 on
// it runs a scenario and prints the outcome and orbit.

#include "orbit/core/constants.hpp"
#include "orbit/core/version.hpp"

#include <iostream>
#include <span>
#include <string_view>

namespace {

void printUsage() {
    std::cout << "Usage: orbitsim-cli [--version] [--help]\n"
                 "\n"
                 "Runs the orbit simulator physics without the GUI.\n";
}

void printConstants() {
    std::cout << "Physical constants (SI units):\n"
              << "  mu_earth    " << orbit::kMuEarth << " m^3/s^2\n"
              << "  r_earth     " << orbit::kRadiusEarth << " m\n"
              << "  omega_earth " << orbit::kOmegaEarth << " rad/s\n"
              << "  g0          " << orbit::kG0 << " m/s^2\n";
}

} // namespace

int main(int argc, char* argv[]) {
    const std::span args(argv + 1, static_cast<std::size_t>(argc - 1));

    for (const std::string_view arg : args) {
        if (arg == "--version") {
            std::cout << orbit::kProjectName << ' ' << orbit::kVersion << '\n';
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        std::cerr << "Unknown option: " << arg << "\n\n";
        printUsage();
        return 1;
    }

    printConstants();
    return 0;
}
