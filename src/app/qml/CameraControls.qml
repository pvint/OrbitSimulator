// SPDX-License-Identifier: GPL-3.0-or-later
// Mouse control of a 3D scene's orbiting camera. Left drag turns the camera
// round the centre; right or middle drag (or Shift + left drag) slides the
// view; the wheel zooms. The scene supplies yaw, pitch, distance, panOffset,
// minDistance and maxDistance.

import QtQuick
import QtQuick3D

MouseArea {
    id: controls
    required property Item scene
    required property PerspectiveCamera camera
    required property View3D view

    anchors.fill: parent
    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
    property point last
    property bool panning: false
    onPressed: mouse => {
        last = Qt.point(mouse.x, mouse.y)
        panning = mouse.button !== Qt.LeftButton || (mouse.modifiers & Qt.ShiftModifier)
    }
    onPositionChanged: mouse => {
        const dx = mouse.x - last.x
        const dy = mouse.y - last.y
        last = Qt.point(mouse.x, mouse.y)
        if (panning) {
            // Scene units per pixel at the distance of the centre, so the
            // point under the mouse follows it at any zoom.
            const perPixel = 2 * scene.distance * Math.tan(camera.fieldOfView * Math.PI / 360)
                             / view.height
            scene.panOffset = scene.panOffset.minus(camera.right.times(dx * perPixel))
                                             .plus(camera.up.times(dy * perPixel))
        } else {
            scene.yaw -= dx * 0.3
            scene.pitch = Math.max(-89, Math.min(89, scene.pitch - dy * 0.3))
        }
    }
    // Each wheel notch zooms by the same factor, so it feels the same at
    // every scale.
    onWheel: wheel => {
        const factor = Math.pow(1.0015, -wheel.angleDelta.y)
        scene.distance = Math.max(scene.minDistance,
                                  Math.min(scene.maxDistance, scene.distance * factor))
    }
}
