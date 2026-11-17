// SPDX-License-Identifier: GPL-3.0-or-later
// From physics coordinates to 3D-scene coordinates.
//
// The physics works in metres with z pointing to the north pole. Qt Quick 3D
// has y pointing up. The scene uses kilometres, and maps
//     scene = (x, z, −y) / 1000
// which is a rotation (not a mirror image), so east stays east. One scene
// unit is 1 km: a 32-bit float then resolves well under a metre near Earth
// and about 30 m at the Moon's distance, plenty for drawing.
#pragma once

#include "orbit/core/vec3.hpp"

#include <QVector3D>

namespace scene {

inline constexpr double kMetresPerUnit = 1000.0;

inline QVector3D fromPhysics(const orbit::Vec3& v) {
    return {static_cast<float>(v.x / kMetresPerUnit), static_cast<float>(v.z / kMetresPerUnit),
            static_cast<float>(-v.y / kMetresPerUnit)};
}

} // namespace scene
