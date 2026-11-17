// SPDX-License-Identifier: GPL-3.0-or-later
// One arrow of the orientation gizmo: a shaft and a cone, pointing along +y
// before `eulerRotation` turns it into place. Length 100 units.

import QtQuick
import QtQuick3D

Node {
    id: arrow
    property color color: "white"

    // #Cylinder and #Cone are 100 units tall and 50 in radius, centred on the origin.
    Model {
        source: "#Cylinder"
        y: 37.5
        scale: Qt.vector3d(0.06, 0.75, 0.06)
        materials: PrincipledMaterial {
            lighting: PrincipledMaterial.NoLighting
            baseColor: arrow.color
        }
    }
    Model {
        source: "#Cone"
        y: 75
        scale: Qt.vector3d(0.16, 0.25, 0.16)
        materials: PrincipledMaterial {
            lighting: PrincipledMaterial.NoLighting
            baseColor: arrow.color
        }
    }
}
