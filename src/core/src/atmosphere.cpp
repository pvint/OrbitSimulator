// SPDX-License-Identifier: GPL-3.0-or-later
#include "orbit/core/atmosphere.hpp"

#include "orbit/core/constants.hpp"

#include <array>
#include <cmath>

namespace orbit {

namespace {

// Constants of the 1976 US Standard Atmosphere.
constexpr double kGasConstant = 8.31432;       ///< R*, J/(mol·K) (the value the standard uses)
constexpr double kMolarMass = 0.0289644;       ///< M, kg/mol of air
constexpr double kEffectiveRadius = 6356766.0; ///< r0, m, for geopotential height
constexpr double kHeatCapacityRatio = 1.4;     ///< γ for air

/// One layer: from geopotential height `base` (m) up to the next layer's.
struct Layer {
    double base;        ///< H_b, m
    double temperature; ///< T_b at the base, K
    double lapseRate;   ///< L, K/m (negative: cooler with height)
    double pressure;    ///< P_b at the base, Pa
};

// The standard's layers, with its tabulated base pressures. Each base
// pressure is what the layer below gives at that height (a test checks).
constexpr std::array kLayers{
    Layer{0.0, 288.15, -0.0065, 101325.0},     // troposphere: weather, cooling with height
    Layer{11000.0, 216.65, 0.0, 22632.06},     // tropopause
    Layer{20000.0, 216.65, 0.001, 5474.889},   // stratosphere: ozone absorbs sunlight
    Layer{32000.0, 228.65, 0.0028, 868.0187},  //   and warms it
    Layer{47000.0, 270.65, 0.0, 110.9063},     // stratopause
    Layer{51000.0, 270.65, -0.0028, 66.93887}, // mesosphere: cooling again
    Layer{71000.0, 214.65, -0.002, 3.956420},
};

/// Geometric altitude where the standard's layers end and the table starts.
constexpr double kStandardTop = 86e3;

/// The 1976 standard at geometric altitude z (m), for z ≤ 86 km.
Air standardAtmosphere(double z) {
    // Gravity weakens with height, so a metre of climb higher up means less
    // added weight of air below: geopotential height H counts that.
    const double h = kEffectiveRadius * z / (kEffectiveRadius + z);

    // The layer containing H (the first one also covers anything below it).
    std::size_t i = kLayers.size() - 1;
    while (i > 0 && h < kLayers[i].base) --i;
    const Layer& layer = kLayers[i];

    const double dh = h - layer.base;
    const double temperature = layer.temperature + layer.lapseRate * dh;
    // g0·M/R: how quickly pressure falls with height, per kelvin.
    const double k = kG0 * kMolarMass / kGasConstant;
    const double pressure =
        layer.lapseRate == 0.0
            ? layer.pressure * std::exp(-k * dh / layer.temperature)
            : layer.pressure * std::pow(layer.temperature / temperature, k / layer.lapseRate);

    Air air;
    air.temperature = temperature;
    air.pressure = pressure;
    air.density = pressure * kMolarMass / (kGasConstant * temperature); // ideal gas law
    air.speedOfSound = std::sqrt(kHeatCapacityRatio * kGasConstant * temperature / kMolarMass);
    return air;
}

/// Density (kg/m³) at geometric altitudes above 86 km: Vallado table 8-4.
struct DensityPoint {
    double altitude; ///< m
    double density;  ///< kg/m³
};
constexpr std::array kUpperDensities{
    DensityPoint{90e3, 3.396e-6},   DensityPoint{100e3, 5.297e-7},   DensityPoint{110e3, 9.661e-8},
    DensityPoint{120e3, 2.438e-8},  DensityPoint{130e3, 8.484e-9},   DensityPoint{140e3, 3.845e-9},
    DensityPoint{150e3, 2.070e-9},  DensityPoint{180e3, 5.464e-10},  DensityPoint{200e3, 2.789e-10},
    DensityPoint{250e3, 7.248e-11}, DensityPoint{300e3, 2.418e-11},  DensityPoint{350e3, 9.518e-12},
    DensityPoint{400e3, 3.725e-12}, DensityPoint{450e3, 1.585e-12},  DensityPoint{500e3, 6.967e-13},
    DensityPoint{600e3, 1.454e-13}, DensityPoint{700e3, 3.614e-14},  DensityPoint{800e3, 1.170e-14},
    DensityPoint{900e3, 5.245e-15}, DensityPoint{1000e3, 3.019e-15},
};

/// Density above 86 km: exponential between neighbouring table entries,
/// starting from the standard atmosphere's value at 86 km.
double upperDensity(double z) {
    static const Air top = standardAtmosphere(kStandardTop);
    DensityPoint lower{kStandardTop, top.density};
    for (const DensityPoint& upper : kUpperDensities) {
        if (z <= upper.altitude) {
            const double scaleHeight =
                (upper.altitude - lower.altitude) / std::log(lower.density / upper.density);
            return lower.density * std::exp(-(z - lower.altitude) / scaleHeight);
        }
        lower = upper;
    }
    return 0.0;
}

} // namespace

Air atmosphereAt(double altitude) {
    if (altitude <= kStandardTop) return standardAtmosphere(altitude);
    if (altitude > kAtmosphereTop) return {};

    static const Air top = standardAtmosphere(kStandardTop);
    Air air;
    air.temperature = top.temperature;
    air.density = upperDensity(altitude);
    air.pressure = air.density * kGasConstant * air.temperature / kMolarMass;
    air.speedOfSound = top.speedOfSound;
    return air;
}

} // namespace orbit
