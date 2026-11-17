// SPDX-License-Identifier: GPL-3.0-or-later
// The 3D view: the textured Earth, the trajectory, the ground track and the
// rocket. Positions are in scene units (1 unit = 1 km, y = north; see
// scene_coordinates.hpp). GlobeView (C++) sets the properties marked "from
// C++" and fills the two line geometries.
//
// The trajectory is drawn in the inertial frame, so it stays put while the
// Earth turns underneath it. The ground track and launch site belong to the
// Earth node and turn with it.

import QtQuick
import QtQuick3D

Item {
    id: root

    // --- from C++ -----------------------------------------------------
    property real earthRotation: 0             // degrees about the north pole
    property vector3d rocketPosition: Qt.vector3d(0, 0, 0)
    property vector3d launchSitePosition: Qt.vector3d(0, 0, 0) // Earth-fixed
    property bool followRocket: false

    // For C++ to fill in.
    property alias trajectory: trajectoryGeometry
    property alias groundTrack: groundTrackGeometry
    property real trajectoryWidth: 2

    /// Extra trajectories: the other flights of a sweep, or fading "ghosts"
    /// of recent flights. A fixed pool; C++ shows as many as it needs.
    readonly property int extraPathCount: 64
    function extraPath(index) { return extraPaths.objectAt(index) }

    // --- camera --------------------------------------------------------
    // The camera orbits a target point: yaw turns it round the north-south
    // axis, pitch raises or lowers it, distance is how far away it is (km).
    readonly property real earthRadius: 6371
    property real yaw: 0
    property real pitch: -25
    property real distance: 4 * earthRadius
    readonly property vector3d target: followRocket ? rocketPosition : Qt.vector3d(0, 0, 0)
    readonly property real minDistance: followRocket ? 20 : earthRadius + 50
    readonly property real maxDistance: 2.0e6

    /// Look at a point (scene coordinates) on or above the Earth from a
    /// distance of a few Earth radii.
    function lookAt(p) {
        const len = Math.max(p.length(), 1e-6)
        yaw = Math.atan2(p.x, p.z) * 180 / Math.PI
        pitch = -Math.asin(p.y / len) * 180 / Math.PI
        distance = 3.2 * earthRadius
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

        // Sunlight. Until the simulation knows the date, the Sun sits in a
        // fixed direction that lights the default launch site at liftoff.
        DirectionalLight {
            eulerRotation: Qt.vector3d(-20, -20, 0)
            brightness: 1.5
            ambientColor: Qt.rgba(0.06, 0.07, 0.09, 1.0) // so the night side isn't pitch black
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
    MouseArea {
        anchors.fill: parent
        property point last
        onPressed: mouse => last = Qt.point(mouse.x, mouse.y)
        onPositionChanged: mouse => {
            root.yaw -= (mouse.x - last.x) * 0.3
            root.pitch = Math.max(-89, Math.min(89, root.pitch - (mouse.y - last.y) * 0.3))
            last = Qt.point(mouse.x, mouse.y)
        }
        // Each wheel notch zooms by the same factor, so it feels the same from
        // low orbit out to the Moon.
        onWheel: wheel => {
            const factor = Math.pow(1.0015, -wheel.angleDelta.y)
            root.distance = Math.max(root.minDistance,
                                     Math.min(root.maxDistance, root.distance * factor))
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 8
        color: "#8592ad"
        font.pixelSize: 11
        text: qsTr("Drag to turn · wheel to zoom · Earth imagery: NASA Earth Observatory (Blue Marble)")
    }
}
