// SPDX-License-Identifier: GPL-3.0-or-later
// A small view in the corner whose camera turns exactly like the main one,
// so its arrows show which way the frame's axes point:
//   X  towards the vernal equinox (♈), the direction of the Sun at the
//      March equinox: astronomy's zero of longitude in the sky
//   N  celestial north: the Earth's spin axis
//   ecl  ecliptic north: the "up" of the solar system (the pole of the
//      Earth's orbit), tilted 23.4° from celestial north. That tilt is what
//      causes the seasons.
// In the Earth's frame Z is celestial north and ecliptic north is the
// tilted, dimmer arrow; in the solar system's frame it's the other way round.

import QtQuick
import QtQuick3D

View3D {
    id: gizmo
    property real yaw: 0
    property real pitch: 0
    /// Scene "up" is ecliptic north (the solar-system view), rather than the
    /// Earth's axis.
    property bool ecliptic: false

    readonly property real tilt: 23.44
    readonly property real tiltRad: tilt * Math.PI / 180

    width: 120
    height: 120
    camera: gizmoCamera
    environment: SceneEnvironment {
        backgroundMode: SceneEnvironment.Transparent
        antialiasingMode: SceneEnvironment.MSAA
        antialiasingQuality: SceneEnvironment.High
    }
    Node {
        eulerRotation.y: gizmo.yaw
        Node {
            eulerRotation.x: gizmo.pitch
            PerspectiveCamera {
                id: gizmoCamera
                z: 330
                fieldOfView: 45
                clipNear: 1
                clipFar: 1000
            }
        }
    }
    // Scene axes are physics (x, z, −y): physics X is scene +x, physics Y is
    // scene −z, physics Z ("up") is scene +y.
    AxisArrow { color: "#ef4444"; eulerRotation.z: -90 }  // X
    AxisArrow { color: "#22c55e"; eulerRotation.x: -90 }  // Y
    AxisArrow { color: gizmo.ecliptic ? "#c9a93a" : "#3b82f6" } // up
    // The other pole, tilted towards scene +z (Earth's frame: ecliptic north
    // leans towards −y) or −z (solar frame: celestial north leans towards +y).
    AxisArrow {
        color: gizmo.ecliptic ? "#3b6fd0" : "#a08b2c"
        eulerRotation.x: gizmo.ecliptic ? -gizmo.tilt : gizmo.tilt
        scale: Qt.vector3d(0.8, 0.8, 0.8)
    }

    // Labels at the arrow tips, placed by projecting each tip into the view.
    Repeater {
        model: [
            // U+FE0E asks for the plain symbol: macOS otherwise draws ♈ as an emoji.
            { text: "X ♈︎", tip: Qt.vector3d(112, 0, 0), color: "#ef4444" },
            { text: "Y", tip: Qt.vector3d(0, 0, -112), color: "#22c55e" },
            { text: gizmo.ecliptic ? "ecl N" : "Z N", tip: Qt.vector3d(0, 112, 0),
              color: gizmo.ecliptic ? "#e0c050" : "#3b82f6" },
            { text: gizmo.ecliptic ? "N" : "ecl",
              tip: Qt.vector3d(0, 92 * Math.cos(gizmo.tiltRad),
                               (gizmo.ecliptic ? -92 : 92) * Math.sin(gizmo.tiltRad)),
              color: gizmo.ecliptic ? "#5b8def" : "#c9a93a" }
        ]
        delegate: Text {
            required property var modelData
            // Mentioning yaw and pitch makes the binding update as the camera turns.
            readonly property vector3d screen: {
                gizmo.yaw; gizmo.pitch
                return gizmo.mapFrom3DScene(modelData.tip)
            }
            x: screen.x - width / 2
            y: screen.y - height / 2
            text: modelData.text
            color: modelData.color
            font.pixelSize: 11
            font.bold: true
        }
    }
}
