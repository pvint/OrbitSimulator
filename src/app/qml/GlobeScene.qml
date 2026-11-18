// SPDX-License-Identifier: GPL-3.0-or-later
// The 3D view: the textured Earth, the trajectory, the ground track and the
// rocket. Positions are in scene units (1 unit = 1 km, y = north; see
// scene_coordinates.hpp). GlobeView (C++) sets the properties marked "from
// C++" and fills the two line geometries.
//
// The trajectory is drawn in the inertial frame, so it stays put while the
// Earth turns underneath it. The ground track and launch site belong to the
// Earth node and turn with it. The Moon moves along its orbit, and the
// sunlight comes from the Sun's real direction at the playback time.

import QtQuick
import QtQuick.Controls
import QtQuick3D

Item {
    id: root

    // --- from C++ -----------------------------------------------------
    property real earthRotation: 0             // degrees about the north pole
    property vector3d rocketPosition: Qt.vector3d(0, 0, 0)
    property var stagePositions: [] // of vector3d: dropped stages still falling
    property vector3d launchSitePosition: Qt.vector3d(0, 0, 0) // Earth-fixed
    property vector3d moonPosition: Qt.vector3d(384400, 0, 0)
    /// Turns the Moon so its near side faces the Earth (tidal locking).
    property quaternion moonRotation: Qt.quaternion(1, 0, 0, 0)
    /// The Moon's colour map, or "" to draw it plain grey.
    property string moonTexture: ""
    property quaternion sunlightRotation: Qt.quaternion(1, 0, 0, 0)
    /// What the camera circles: 0 = the Earth, 1 = the rocket, 2 = the Moon.
    property int followTarget: 0

    // Demo tour caption (from C++), and what its buttons ask for.
    property bool captionVisible: false
    property string captionTitle
    property string captionText
    property string captionStep // e.g. "3 / 11"
    signal demoPrevious()
    signal demoNext()
    signal demoEnd()

    // For C++ to fill in.
    property alias trajectory: trajectoryGeometry
    property alias groundTrack: groundTrackGeometry
    property alias moonOrbit: moonOrbitGeometry
    property real trajectoryWidth: 2

    /// Extra trajectories: the other flights of a sweep, or fading "ghosts"
    /// of recent flights. A fixed pool; C++ shows as many as it needs.
    readonly property int extraPathCount: 64
    function extraPath(index) { return extraPaths.objectAt(index) }

    // --- camera --------------------------------------------------------
    // The camera orbits a target point: yaw turns it round the north-south
    // axis, pitch raises or lowers it, distance is how far away it is (km).
    readonly property real earthRadius: 6371
    readonly property real moonRadius: 1737.4
    property real yaw: 0
    property real pitch: -25
    property real distance: 4 * earthRadius
    /// Panning slides the view by this much (scene units) from the centre.
    property vector3d panOffset: Qt.vector3d(0, 0, 0)
    readonly property vector3d centrePoint: followTarget === 1 ? rocketPosition
                                          : followTarget === 2 ? moonPosition
                                          : Qt.vector3d(0, 0, 0)
    readonly property vector3d target: centrePoint.plus(panOffset)
    readonly property real minDistance: followTarget === 1 ? 20
                                      : followTarget === 2 ? moonRadius + 50
                                      : earthRadius + 50
    readonly property real maxDistance: 2.0e6

    /// Look from direction `p` (scene coordinates) at the current target,
    /// from `fromDistance` km away.
    function lookAt(p, fromDistance) {
        panOffset = Qt.vector3d(0, 0, 0)
        const len = Math.max(p.length(), 1e-6)
        yaw = Math.atan2(p.x, p.z) * 180 / Math.PI
        pitch = -Math.asin(p.y / len) * 180 / Math.PI
        distance = Math.max(minDistance, fromDistance)
    }

    // Keeps markers the same size on screen, whatever the zoom.
    readonly property real markerScale: distance * 0.006 / 50 // #Sphere has radius 50

    View3D {
        id: view
        anchors.fill: parent

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "#03050b"
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        // Sunlight, from the Sun's direction. A directional light shines down
        // its own −z axis; C++ supplies the rotation that turns −z onto the
        // direction the sunlight travels (from the Sun towards the Earth).
        DirectionalLight {
            rotation: root.sunlightRotation
            brightness: 1.6
            ambientColor: Qt.rgba(0.03, 0.035, 0.05, 1.0)
        }
        // A dim, cool fill light from the opposite direction, so the night
        // side isn't pitch black: continents and craters stay faintly
        // visible, while the day side and the day/night line are untouched.
        Node {
            rotation: root.sunlightRotation
            DirectionalLight {
                eulerRotation.y: 180
                color: "#9db4ff"
                brightness: 0.14
            }
        }

        Node {
            id: cameraYaw
            position: root.target
            eulerRotation.y: root.yaw
            Node {
                eulerRotation.x: root.pitch
                PerspectiveCamera {
                    id: camera
                    z: root.distance
                    fieldOfView: 45
                    // Keep the near/far ratio modest so depth stays precise.
                    clipNear: Math.max(0.05, root.distance * 0.001)
                    clipFar: root.distance * 50 + 2 * root.earthRadius
                }
            }
        }

        // --- The Earth and everything fixed to it -----------------------
        Node {
            id: earth
            eulerRotation.y: root.earthRotation

            Model {
                geometry: GlobeGeometry {}
                scale: Qt.vector3d(root.earthRadius, root.earthRadius, root.earthRadius)
                materials: PrincipledMaterial {
                    baseColorMap: Texture {
                        source: "qrc:/textures/earth_day.jpg"
                        generateMipmaps: true
                        mipFilter: Texture.Linear
                    }
                    roughness: 0.85
                    metalness: 0.0
                    specularAmount: 0.15
                }
            }

            Model {
                geometry: PolylineGeometry { id: groundTrackGeometry }
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    vertexColorsEnabled: true
                    lineWidth: 2
                }
            }

            Model {
                source: "#Sphere"
                position: root.launchSitePosition
                scale: Qt.vector3d(root.markerScale, root.markerScale, root.markerScale)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: "#ffb347"
                }
            }
        }

        // --- The Moon and its orbit ------------------------------------------
        Model {
            geometry: GlobeGeometry {}
            position: root.moonPosition
            rotation: root.moonRotation
            scale: Qt.vector3d(root.moonRadius, root.moonRadius, root.moonRadius)
            materials: PrincipledMaterial {
                baseColor: root.moonTexture === "" ? "#a4a4a4" : "#ffffff"
                baseColorMap: root.moonTexture === "" ? null : moonColour
                roughness: 1.0
                metalness: 0.0
                specularAmount: 0.05
            }
            Texture {
                id: moonColour
                source: root.moonTexture
                generateMipmaps: true
                mipFilter: Texture.Linear
            }
        }
        Model {
            geometry: PolylineGeometry { id: moonOrbitGeometry }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                vertexColorsEnabled: true
                opacity: 0.35
            }
        }

        // --- In the inertial frame -----------------------------------------
        Repeater3D {
            id: extraPaths
            model: root.extraPathCount
            delegate: Model {
                id: pathModel
                property alias line: lineGeometry
                property real lineOpacity: 1.0
                visible: false
                geometry: PolylineGeometry { id: lineGeometry }
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    vertexColorsEnabled: true
                    opacity: pathModel.lineOpacity
                    lineWidth: 1.5
                }
            }
        }

        Model {
            geometry: PolylineGeometry { id: trajectoryGeometry }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                vertexColorsEnabled: true
                lineWidth: root.trajectoryWidth
            }
        }

        // Dropped stages during playback, falling away.
        Repeater3D {
            model: root.stagePositions
            delegate: Model {
                required property vector3d modelData
                source: "#Sphere"
                position: modelData
                scale: Qt.vector3d(root.markerScale * 0.7, root.markerScale * 0.7,
                                   root.markerScale * 0.7)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: "#94a3b8"
                }
            }
        }

        Model {
            source: "#Sphere"
            position: root.rocketPosition
            scale: Qt.vector3d(root.markerScale, root.markerScale, root.markerScale)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: "#ffffff"
            }
        }
    }

    // --- Mouse: drag to turn, wheel to zoom ---------------------------------
    // Left drag turns the camera round the centre; right or middle drag (or
    // Shift + left drag) slides the view; the wheel zooms.
    MouseArea {
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
                const perPixel = 2 * root.distance * Math.tan(camera.fieldOfView * Math.PI / 360)
                                 / view.height
                root.panOffset = root.panOffset.minus(camera.right.times(dx * perPixel))
                                                .plus(camera.up.times(dy * perPixel))
            } else {
                root.yaw -= dx * 0.3
                root.pitch = Math.max(-89, Math.min(89, root.pitch - dy * 0.3))
            }
        }
        // Each wheel notch zooms by the same factor, so it feels the same from
        // low orbit out to the Moon.
        onWheel: wheel => {
            const factor = Math.pow(1.0015, -wheel.angleDelta.y)
            root.distance = Math.max(root.minDistance,
                                     Math.min(root.maxDistance, root.distance * factor))
        }
    }

    // --- Orientation gizmo ------------------------------------------------
    // A small second view whose camera turns exactly like the main one, so its
    // arrows show which way the frame's axes point:
    //   X  towards the vernal equinox (♈), the direction of the Sun at the
    //      March equinox: astronomy's zero of longitude in the sky
    //   Z  celestial north: the Earth's spin axis
    //   Y  completes the right-handed set
    // and, dimmer, ecliptic north: the "up" of the solar system (the pole of
    // the Earth's orbit), tilted 23.4° from celestial north. That tilt is
    // what causes the seasons.
    View3D {
        id: gizmo
        width: 120
        height: 120
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 6
        camera: gizmoCamera
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }
        Node {
            eulerRotation.y: root.yaw
            Node {
                eulerRotation.x: root.pitch
                PerspectiveCamera {
                    id: gizmoCamera
                    z: 330
                    fieldOfView: 45
                    clipNear: 1
                    clipFar: 1000
                }
            }
        }
        // Scene axes are physics (x, z, −y): physics X is scene +x, physics
        // Y is scene −z, physics Z (north) is scene +y.
        AxisArrow { color: "#ef4444"; eulerRotation.z: -90 }  // X
        AxisArrow { color: "#22c55e"; eulerRotation.x: -90 }  // Y
        AxisArrow { color: "#3b82f6" }                        // Z
        AxisArrow { color: "#a08b2c"; eulerRotation.x: 23.44; scale: Qt.vector3d(0.8, 0.8, 0.8) }

        // Labels at the arrow tips, placed by projecting each tip into the view.
        Repeater {
            model: [
                { text: "X ♈", tip: Qt.vector3d(112, 0, 0), color: "#ef4444" },
                { text: "Y", tip: Qt.vector3d(0, 0, -112), color: "#22c55e" },
                { text: "Z N", tip: Qt.vector3d(0, 112, 0), color: "#3b82f6" },
                { text: "ecl", tip: Qt.vector3d(0, 92 * Math.cos(23.44 * Math.PI / 180),
                                               92 * Math.sin(23.44 * Math.PI / 180)),
                  color: "#c9a93a" }
            ]
            delegate: Text {
                required property var modelData
                // Mentioning yaw and pitch makes the binding update as the camera turns.
                readonly property vector3d screen: {
                    root.yaw; root.pitch
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

    // --- Demo tour caption --------------------------------------------------
    Rectangle {
        visible: root.captionVisible
        anchors.top: parent.top
        anchors.topMargin: 10
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 300, 680)
        height: captionColumn.implicitHeight + 24
        radius: 8
        color: "#e60f1628"
        border.color: "#334a6e"

        Column {
            id: captionColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 8

            Text {
                text: root.captionTitle
                color: "#f1f5f9"
                font.pixelSize: 17
                font.bold: true
            }
            Text {
                width: parent.width
                text: root.captionText
                color: "#d6deeb"
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                lineHeight: 1.15
            }
            Row {
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.captionStep
                    color: "#8592ad"
                    font.pixelSize: 12
                    rightPadding: 6
                }
                Button { text: qsTr("◀ Previous"); onClicked: root.demoPrevious() }
                Button { text: qsTr("Next ▶"); onClicked: root.demoNext() }
                Button { text: qsTr("End tour"); onClicked: root.demoEnd() }
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 8
        color: "#8592ad"
        font.pixelSize: 11
        text: qsTr("Drag to turn · right-drag to pan · wheel to zoom · Imagery: NASA Earth Observatory, NASA SVS / LRO")
    }
}
