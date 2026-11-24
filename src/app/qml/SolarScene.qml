// SPDX-License-Identifier: GPL-3.0-or-later
// The solar-system view: the Sun, the planets on their orbits, and a
// natural body's path around the Sun. Positions are in scene units of a
// million km (1 au = 149.6 units), in the heliocentric ecliptic J2000 frame
// with scene y = ecliptic north, the "up" of the solar system (see
// scene::fromHeliocentric). GlobeView (C++) sets the properties marked
// "from C++" and fills the line geometries.
//
// The Sun and planets are far too small to see at this scale (the Earth
// would be a twentieth of a pixel across at 1 au), so they are drawn as
// markers of a fixed size on screen, the Sun a little larger.

import QtQuick
import QtQuick3D

Item {
    id: root

    // --- from C++ -----------------------------------------------------
    property vector3d bodyPosition: Qt.vector3d(0, 0, 0)
    property color vehicleColor: "#d6c7a1"
    /// The eight planets, Mercury first (vector3d each).
    property var planetPositions: []
    /// What the camera circles: 0 = the Sun, 1 = the body, 2 = the Earth.
    property int followTarget: 0

    // Demo tour caption (from C++), and what its buttons ask for.
    property bool captionVisible: false
    property string captionTitle
    property string captionText
    property string captionStep
    signal demoPrevious()
    signal demoNext()
    signal demoEnd()

    // For C++ to fill in.
    property alias trajectory: trajectoryGeometry
    property real trajectoryWidth: 2
    readonly property int extraPathCount: 64
    function extraPath(index) { return extraPaths.objectAt(index) }
    function planetOrbit(index) { return planetOrbits.objectAt(index) }

    readonly property var planetNames: ["Mercury", "Venus", "Earth", "Mars",
                                        "Jupiter", "Saturn", "Uranus", "Neptune"]
    readonly property var planetColors: ["#a8a29e", "#e9c46a", "#4f9de0", "#d9603b",
                                         "#d8a66c", "#e3c98f", "#8fd3e0", "#5b7fe8"]

    // --- camera --------------------------------------------------------
    readonly property real sunRadius: 0.6957 // million km
    property real yaw: 0
    property real pitch: -55
    property real distance: 600
    property vector3d panOffset: Qt.vector3d(0, 0, 0)
    readonly property vector3d earthPosition: planetPositions.length > 2 ? planetPositions[2]
                                                                         : Qt.vector3d(0, 0, 0)
    readonly property vector3d centrePoint: followTarget === 1 ? bodyPosition
                                          : followTarget === 2 ? earthPosition
                                          : Qt.vector3d(0, 0, 0)
    readonly property vector3d target: centrePoint.plus(panOffset)
    readonly property real minDistance: followTarget === 0 ? 2 * sunRadius : 0.02
    readonly property real maxDistance: 400000 // about 2700 au

    /// Look from direction `p` (scene coordinates) at the current target,
    /// from `fromDistance` units away.
    function lookAt(p, fromDistance) {
        panOffset = Qt.vector3d(0, 0, 0)
        const len = Math.max(p.length(), 1e-6)
        yaw = Math.atan2(p.x, p.z) * 180 / Math.PI
        pitch = -Math.asin(p.y / len) * 180 / Math.PI
        distance = Math.max(minDistance, fromDistance)
    }

    readonly property real markerScale: distance * 0.006 / 50 // #Sphere has radius 50

    View3D {
        id: view
        anchors.fill: parent
        camera: camera // the labels project through it, so not left to chance

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "#03050b"
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            position: root.target
            eulerRotation.y: root.yaw
            Node {
                eulerRotation.x: root.pitch
                PerspectiveCamera {
                    id: camera
                    z: root.distance
                    fieldOfView: 45
                    clipNear: Math.max(0.001, root.distance * 0.001)
                    clipFar: root.distance * 50 + 10000
                }
            }
        }

        // The Sun: its real size, or a marker if that is too small to see.
        Model {
            source: "#Sphere"
            readonly property real size: Math.max(root.sunRadius / 50, root.markerScale * 1.8)
            scale: Qt.vector3d(size, size, size)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: "#ffd27a"
            }
        }

        // The planets' orbits, faint, and the planets on them.
        Repeater3D {
            id: planetOrbits
            model: 8
            delegate: Model {
                property alias line: orbitGeometry
                geometry: PolylineGeometry { id: orbitGeometry }
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    vertexColorsEnabled: true
                    opacity: 0.45
                }
            }
        }
        Repeater3D {
            model: root.planetPositions
            delegate: Model {
                required property var modelData
                required property int index
                source: "#Sphere"
                position: modelData
                scale: Qt.vector3d(root.markerScale, root.markerScale, root.markerScale)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: root.planetColors[index]
                }
            }
        }

        // Other flights of a sweep, or ghosts of recent ones.
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

        Model {
            source: "#Sphere"
            position: root.bodyPosition
            scale: Qt.vector3d(root.markerScale, root.markerScale, root.markerScale)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: root.vehicleColor
            }
        }
    }

    // Names next to the Sun and the planets, placed by projecting each
    // position into the view.
    Repeater {
        model: root.planetPositions.length
        delegate: Text {
            required property int index
            readonly property vector3d screen: {
                root.yaw; root.pitch; root.distance; root.target
                return view.camera ? view.mapFrom3DScene(root.planetPositions[index])
                                   : Qt.vector3d(0, 0, -1)
            }
            visible: screen.z > 0
            x: screen.x + 7
            y: screen.y - height / 2
            text: root.planetNames[index]
            color: root.planetColors[index]
            font.pixelSize: 11
        }
    }
    Text {
        readonly property vector3d screen: {
            root.yaw; root.pitch; root.distance; root.target
            return view.camera ? view.mapFrom3DScene(Qt.vector3d(0, 0, 0)) : Qt.vector3d(0, 0, -1)
        }
        visible: screen.z > 0
        x: screen.x + 9
        y: screen.y - height / 2
        text: qsTr("Sun")
        color: "#ffd27a"
        font.pixelSize: 11
    }

    CameraControls {
        scene: root
        camera: camera
        view: view
    }

    // --- Orientation gizmo (ecliptic north up) -----------------------------
    OrientationGizmo {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 6
        yaw: root.yaw
        pitch: root.pitch
        ecliptic: true
    }

    DemoCaption {
        visible: root.captionVisible
        title: root.captionTitle
        text: root.captionText
        step: root.captionStep
        onPrevious: root.demoPrevious()
        onNext: root.demoNext()
        onEnd: root.demoEnd()
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 8
        color: "#8592ad"
        font.pixelSize: 11
        text: qsTr("Drag to turn · right-drag to pan · wheel to zoom · Sun and planets not to scale")
    }
}
