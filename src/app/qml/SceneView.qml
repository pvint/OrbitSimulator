// SPDX-License-Identifier: GPL-3.0-or-later
// The 3D scene. For M0 it is only a placeholder Earth that proves Qt Quick 3D
// works inside the widget window. M3 replaces it with the real globe and
// trajectories.

import QtQuick
import QtQuick3D

Rectangle {
    color: "#070b16"

    View3D {
        anchors.fill: parent

        environment: SceneEnvironment {
            clearColor: "#070b16"
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        PerspectiveCamera {
            position: Qt.vector3d(0, 0, 400)
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(-20, -40, 0)
            brightness: 1.2
        }

        // The built-in sphere has a radius of 50 units. This scale makes it 100.
        Model {
            id: earth
            source: "#Sphere"
            scale: Qt.vector3d(2, 2, 2)
            materials: PrincipledMaterial {
                baseColor: "#2a6fb0"
                roughness: 0.7
            }

            NumberAnimation on eulerRotation.y {
                from: 0
                to: 360
                duration: 20000
                loops: Animation.Infinite
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        color: "#8592ad"
        text: qsTr("3D view placeholder (real globe in M3)")
    }
}
