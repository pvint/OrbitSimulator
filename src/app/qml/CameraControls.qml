// SPDX-License-Identifier: GPL-3.0-or-later
// Mouse control of a 3D scene's orbiting camera. Left drag turns the camera
// round the centre; right or middle drag (or Shift + left drag) slides the
// view; the wheel zooms towards the point under the pointer. The scene
// supplies yaw, pitch, distance, panOffset, target, minDistance and
// maxDistance.

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
    // every scale. Towards the pointer: the whole camera rig is scaled about
    // the point under it (at the centre's depth), so that point stays put
    // on screen while everything else closes in on it.
    onWheel: wheel => {
        let factor = Math.pow(1.0015, -wheel.angleDelta.y)
        const wanted = Math.max(scene.minDistance,
                                Math.min(scene.maxDistance, scene.distance * factor))
        factor = wanted / scene.distance
        const under = view.mapTo3DScene(Qt.vector3d(wheel.x, wheel.y, scene.distance))
        scene.panOffset = scene.panOffset.plus(under.minus(scene.target).times(1 - factor))
        scene.distance = wanted
    }
}
