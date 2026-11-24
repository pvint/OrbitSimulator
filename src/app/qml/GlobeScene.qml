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
import QtQuick3D

Item {
    id: root

    // --- from C++ -----------------------------------------------------
    property real earthRotation: 0             // degrees about the north pole
    property vector3d rocketPosition: Qt.vector3d(0, 0, 0)
    property var stagePositions: [] // of vector3d: dropped stages still falling
    property vector3d launchSitePosition: Qt.vector3d(0, 0, 0) // Earth-fixed
    property bool launchSiteVisible: true // not for natural bodies
    property color vehicleColor: "#ffffff" // the rocket white, a natural body rock-coloured
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
    // Far enough to see a natural body 10 million km out (the furthest one
    // is followed). Floats still place things to within a km out there.
    readonly property real maxDistance: 3.0e7

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
                visible: root.launchSiteVisible
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
                baseColor: root.vehicleColor
            }
        }
    }

    // --- Mouse: drag to turn, wheel to zoom ---------------------------------
    CameraControls {
        scene: root
        camera: camera
        view: view
    }

    // --- Orientation gizmo (Z = the Earth's axis up) -----------------------
    OrientationGizmo {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 6
        yaw: root.yaw
        pitch: root.pitch
    }

    // --- Demo tour caption --------------------------------------------------
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
        text: qsTr("Drag to turn · right-drag to pan · wheel to zoom · Imagery: NASA Earth Observatory, NASA SVS / LRO")
    }
}
