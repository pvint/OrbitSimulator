// SPDX-License-Identifier: GPL-3.0-or-later
// The "Lost in space" game's view: the Sun, the planets on their orbits, and
// the ship with its predicted path. Scene units are millions of km, in the
// heliocentric ecliptic J2000 frame with scene y = ecliptic north
// (scene::fromHeliocentric), as in SolarScene.qml. GameWindow (C++) sets the
// properties marked "from C++" and fills the line geometries.
//
// On top: what you're trying to do and the date (top left), where the path
// takes you (top right), and a card for starting a game and for the ending.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D

Item {
    id: root

    // --- from C++ -----------------------------------------------------
    property vector3d shipPosition: Qt.vector3d(0, 0, 0)
    /// FLOATING ORIGIN. Every position here is relative to what the camera
    /// follows (worked out in double precision in C++), not to the Sun: a
    /// 32-bit float 9.5 au out resolves only about 120 km, enough to make a
    /// close view of Titan jitter. So the Sun sits at sunPosition, and the
    /// big lines drawn around it (the planets' orbits, the belt) and the
    /// path are each drawn relative to a point near the camera, placed at
    /// staticPosition and pathPosition.
    property vector3d sunPosition: Qt.vector3d(0, 0, 0)
    property vector3d staticPosition: Qt.vector3d(0, 0, 0)
    property vector3d pathPosition: Qt.vector3d(0, 0, 0)
    /// The eight planets, Mercury first (vector3d each), at the game clock.
    property var planetPositions: []
    /// Every body's name and colour, in orbit::Body order: the planets,
    /// then the 18 moons (the Moon, Jupiter's four, ...).
    property var bodyNames: []
    property var bodyColors: []
    /// The moons (vector3d each, orbit::Body order from the Moon), the
    /// planet each goes round (orbit::Body), its orbit's size and its sphere
    /// of influence (scene units).
    property var moonPositions: []
    property var moonPlanets: []
    property var moonOrbitRadii: []
    property var moonInfluenceRadii: []
    /// For the start card: each planet's moons (orbit::Body indices,
    /// innermost first), and whether a ship can orbit each body.
    property var planetMoons: []
    property var bodyParkable: []
    /// What hovering a moon shows.
    property var moonInfoTexts: []
    /// Where each burn happens (vector3d), whether it has fired, and the
    /// one being edited.
    property var burnPositions: []
    property var burnFired: []
    property int selectedBurn: -1
    /// The closest approach to the Earth: the ship and the Earth then.
    property bool arrivalVisible: false
    property vector3d arrivalShip: Qt.vector3d(0, 0, 0)
    property vector3d arrivalEarth: Qt.vector3d(0, 0, 0)
    property string arrivalLabel
    /// What the camera circles: 0 = the Sun, 1 = the ship, 2 = a planet or
    /// a moon (followPlanet, orbit::Body order).
    property int followTarget: 0
    property int followPlanet: 2
    /// Each planet's sphere of influence (scene units): where its gravity
    /// takes over from the Sun's. Drawn as a ring in the ecliptic and a
    /// faint sphere, brighter for the destination.
    property var influenceRadii: []
    /// Double-clicking an object asks to centre on it (index as hoverIndex).
    signal centreRequested(int index)
    /// What hovering shows, as rich text: the eight planets, then the Sun,
    /// then the ship (ten strings). Burns come after, in burnInfoTexts, then
    /// flybys, then the moons.
    property var infoTexts: []
    /// The same for each burn marker.
    property var burnInfoTexts: []
    /// Each flyby (a pass inside a planet's sphere of influence): where the
    /// ship is at its closest, a short label, and what hovering shows.
    property var flybyPositions: []
    property var flybyLabels: []
    property var flybyInfoTexts: []

    property string goalText
    property string dateText
    property string speedText
    property string statusText
    property color statusColor: "#cbd5e1"
    property string busyText

    /// The card in the middle: "start" (pick where and how hard), "message"
    /// (one button) or "" (hidden).
    property string cardMode: "start"
    property string cardTitle: qsTr("Lost in space")
    property string cardText
    property string cardButton: qsTr("OK")

    /// The last game's choices (orbit::Region's and orbit::Body's order),
    /// kept for the next: where from, in orbit there or not, where to (a
    /// planet or a moon).
    property int regionIndex: 0
    property bool inOrbit: false
    /// In orbit around this moon of the place's planet (body index), or -1.
    property int startMoonIndex: -1
    property int destinationIndex: 2
    /// Each place: its name, the planet it's about (orbit::Planet, or -1)
    /// and what it's like.
    readonly property var regions: [
        { name: qsTr("Between the planets"), planet: -1,
          about: qsTr("A random orbit not far from the Earth's: the place to start. Harder puzzles start further out.") },
        { name: qsTr("Mercury"), planet: 0,
          about: qsTr("Closest to the Sun and fastest: its gravity well is the Sun's, and climbing out of it costs a lot.") },
        { name: qsTr("Venus"), planet: 1,
          about: qsTr("Closer to the Sun than the Earth: speed up to climb out to its orbit. About half a year to the Earth.") },
        { name: qsTr("Earth"), planet: 2,
          about: qsTr("Home: start in a low orbit, 200 km up, and set off for the Moon or another planet.") },
        { name: qsTr("Mars"), planet: 3,
          about: qsTr("Further out than the Earth: slow down to fall in towards the Sun. About a year to the Earth.") },
        { name: qsTr("Asteroid belt"), planet: -1,
          about: qsTr("Between Mars and Jupiter. A long fall home, more than a year, arriving fast.") },
        { name: qsTr("Jupiter"), planet: 4,
          about: qsTr("Five times further from the Sun than the Earth. About three years to the Earth, arriving so fast that landing means braking first.") },
        { name: qsTr("Saturn"), planet: 5,
          about: qsTr("Ten times further out. Six years to the Earth the direct way: can Jupiter's gravity help?") },
        { name: qsTr("Uranus"), planet: 6,
          about: qsTr("Twenty times further out. Sixteen years to the Earth the direct way, unless a giant planet bends your path.") },
        { name: qsTr("Neptune"), planet: 7,
          about: qsTr("The edge of the planets, thirty times further out. Thirty years the direct way: look for a gravity assist.") }
    ]
    readonly property int startPlanet: regions[regionIndex].planet
    readonly property var startMoons: planetMoons[startPlanet] ?? []
    readonly property bool atMoon: startMoonIndex >= 0
    /// Home from the Moon: the Earth's within reach already, so always a
    /// re-entry (orbit::goalFor).
    readonly property bool homeFromMoon: atMoon && startPlanet === 2 && destinationIndex === 2
    /// In orbit: always around the Earth or a moon, and around a planet
    /// going to one of its moons; never where there's no planet.
    readonly property bool startsInOrbit: startPlanet === 2 || atMoon
                                          || (startPlanet >= 0 && (inOrbit || destinationPlanet === startPlanet))
    readonly property bool goingHome: destinationIndex === 2
    readonly property string destinationName: bodyNames[destinationIndex] ?? ""
    /// The destination's planet: itself, or the one a moon goes round.
    readonly property int destinationPlanet: destinationIndex < 8
                                             ? destinationIndex
                                             : (moonPlanets[destinationIndex - 8] ?? 2)
    readonly property bool toMoon: destinationIndex >= 8
    readonly property var destinationMoons: planetMoons[destinationPlanet] ?? []
    /// A body's name in a sentence: "the Earth", "the Moon", "Titan".
    function nameInText(body, start) {
        if (body === 2) return start ? qsTr("The Earth") : qsTr("the Earth")
        if (body === 8) return start ? qsTr("The Moon") : qsTr("the Moon")
        return bodyNames[body] ?? ""
    }
    /// Picking a planet as where to; picking the one you start at offers
    /// its moons instead (from the Earth, the Moon), other than the one you
    /// start at; but from the Moon, the Earth is home.
    function chooseDestinationPlanet(index) {
        if (index !== startPlanet || (atMoon && index === 2)) {
            destinationIndex = index
            return
        }
        const others = (planetMoons[index] ?? []).filter(m => m !== startMoonIndex)
        if (others.length > 0) destinationIndex = others[0]
    }
    /// Picking where from: not to the same planet (from the Earth, Mars).
    function chooseRegion(index) {
        regionIndex = index
        startMoonIndex = -1
        if (regions[index].planet === destinationIndex) destinationIndex = destinationIndex === 2 ? 3 : 2
    }
    /// Picking a moon to start at (again: not at it): not to itself, nor to
    /// its planet unless that's home.
    function chooseStartMoon(moon) {
        startMoonIndex = startMoonIndex === moon ? -1 : moon
        if (!atMoon) {
            if (destinationIndex === startPlanet) chooseDestinationPlanet(startPlanet)
            return
        }
        if (destinationIndex === moon || (destinationIndex === startPlanet && startPlanet !== 2)) {
            const others = startMoons.filter(m => m !== moon)
            destinationIndex = others.length > 0 ? others[0] : 2
        }
    }

    signal startChosen(int region, bool inOrbit, int startMoon, int destination, int difficulty)

    /// The start card shows "puzzle" (where from, where to, how hard) or
    /// "roam" (free roam: a planet or a moon to start in orbit around).
    property string startMode: "puzzle"
    readonly property bool puzzleStart: cardMode === "start" && startMode === "puzzle"
    readonly property bool roamStart: cardMode === "start" && startMode === "roam"
    /// Free roam's body (orbit::Body index), its planet and that planet's moons.
    property int roamBody: 2
    readonly property int roamPlanet: roamBody < 8 ? roamBody : (moonPlanets[roamBody - 8] ?? 2)
    readonly property var roamMoons: planetMoons[roamPlanet] ?? []
    signal freeRoamChosen(int body)
    /// Playing free roam: no destination to pick out; the body you started
    /// at is marked instead.
    property bool freeRoam: false
    readonly property int markedIndex: freeRoam ? roamBody : destinationIndex
    /// The message card can be hidden for good ("Don't show this again"),
    /// and some cards already are (the start card offers to show them again).
    property bool cardHideable: false
    property bool cardsHidden: false
    signal cardAccepted(bool hideFromNowOn)
    signal hiddenCardsReset()

    // For C++ to fill in.
    property alias path: pathGeometry
    property alias arrivalLine: arrivalLineGeometry
    property alias startOrbit: startOrbitGeometry
    property alias belt: beltGeometry
    property alias unitCircle: unitCircleGeometry
    function planetOrbit(index) { return planetOrbits.objectAt(index) }
    function moonOrbit(index) { return moonOrbits.objectAt(index) }
    readonly property var planetNames: bodyNames.slice(0, 8)
    readonly property color shipColor: "#f8fafc"
    readonly property color burnColor: "#fb923c"
    readonly property color flybyColor: "#c084fc"

    // --- camera --------------------------------------------------------
    readonly property real sunRadius: 0.6957 // million km
    property real yaw: 0
    property real pitch: -60
    property real distance: 600
    property vector3d panOffset: Qt.vector3d(0, 0, 0)
    readonly property vector3d earthPosition: planetPositions.length > 2 ? planetPositions[2]
                                                                         : Qt.vector3d(0, 0, 0)
    readonly property vector3d centrePoint:
        followTarget === 1 ? shipPosition
        : followTarget === 2 && followPlanet < 8 && planetPositions.length > followPlanet
            ? planetPositions[followPlanet]
        : followTarget === 2 && moonPositions.length > followPlanet - 8
            ? moonPositions[followPlanet - 8]
        : sunPosition
    readonly property vector3d target: centrePoint.plus(panOffset)
    /// As close as 2,000 km; but not inside the Sun when circling it.
    readonly property real minDistance:
        followTarget === 0 && panOffset.length() < 2 * sunRadius ? 2 * sunRadius : 0.002
    readonly property real maxDistance: 40000

    function lookAt(p, fromDistance) {
        panOffset = Qt.vector3d(0, 0, 0)
        const len = Math.max(p.length(), 1e-6)
        yaw = Math.atan2(p.x, p.z) * 180 / Math.PI
        pitch = -Math.asin(p.y / len) * 180 / Math.PI
        distance = Math.max(minDistance, fromDistance)
    }

    readonly property real markerScale: distance * 0.006 / 50 // #Sphere has radius 50

    /// A moon is drawn once the camera is near enough for its orbit to be a
    /// few pixels across (its name, nearer still): far out they would only
    /// clutter their planet.
    function cameraDistanceTo(p) {
        root.yaw; root.pitch; root.distance; root.target
        return camera.scenePosition.minus(p).length()
    }
    function moonShown(i) {
        return i < moonPositions.length && cameraDistanceTo(moonPositions[i]) < 120 * (moonOrbitRadii[i] ?? 0)
    }
    function moonNamed(i) {
        return i < moonPositions.length && cameraDistanceTo(moonPositions[i]) < 30 * (moonOrbitRadii[i] ?? 0)
    }

    View3D {
        id: view
        anchors.fill: parent
        camera: camera

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

        Model {
            source: "#Sphere"
            position: root.sunPosition
            readonly property real size: Math.max(root.sunRadius / 50, root.markerScale * 1.8)
            scale: Qt.vector3d(size, size, size)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: "#ffd27a"
            }
        }

        Repeater3D {
            id: planetOrbits
            model: 8
            delegate: Model {
                property alias line: orbitGeometry
                position: root.staticPosition
                geometry: PolylineGeometry { id: orbitGeometry }
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    vertexColorsEnabled: true
                    opacity: 0.35
                }
            }
        }
        Repeater3D {
            model: root.planetPositions
            delegate: Node {
                id: planetNode
                required property var modelData
                required property int index
                position: modelData
                readonly property real influence: root.influenceRadii[index] ?? 0
                readonly property bool destination: index === root.markedIndex
                Model {
                    source: "#Sphere"
                    // The destination a little larger: it's where you're going.
                    readonly property real size: root.markerScale * (planetNode.destination ? 1.4 : 1.0)
                    scale: Qt.vector3d(size, size, size)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColor: root.bodyColors[planetNode.index]
                    }
                }
                // Its sphere of influence: a ring in the ecliptic, and a
                // faint sphere (only seen when zoomed in near it).
                Model {
                    visible: planetNode.influence > 0
                    geometry: unitCircleGeometry
                    scale: Qt.vector3d(planetNode.influence, planetNode.influence, planetNode.influence)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        vertexColorsEnabled: true
                        baseColor: root.bodyColors[planetNode.index]
                        opacity: planetNode.destination ? 0.9 : 0.45
                    }
                }
                Model {
                    visible: planetNode.influence > 0
                    source: "#Sphere"
                    readonly property real size: planetNode.influence / 50 // #Sphere has radius 50
                    scale: Qt.vector3d(size, size, size)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColor: root.bodyColors[planetNode.index]
                        opacity: planetNode.destination ? 0.07 : 0.035
                        depthDrawMode: Material.NeverDepthDraw
                    }
                }
            }
        }

        // The moons: each one's orbit around its planet (a ring that moves
        // with the planet, filled from C++), the moon, and its sphere of
        // influence.
        Repeater3D {
            id: moonOrbits
            model: 18
            delegate: Model {
                required property int index
                property alias line: moonOrbitGeometry
                visible: root.moonShown(index)
                position: root.planetPositions[root.moonPlanets[index] ?? 0] ?? Qt.vector3d(0, 0, 0)
                geometry: PolylineGeometry { id: moonOrbitGeometry }
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    vertexColorsEnabled: true
                    opacity: 0.35
                }
            }
        }
        Repeater3D {
            model: root.moonPositions
            delegate: Node {
                id: moonNode
                required property var modelData
                required property int index
                readonly property int body: index + 8
                readonly property bool destination: body === root.markedIndex
                readonly property real influence: root.moonInfluenceRadii[index] ?? 0
                position: modelData
                visible: root.moonShown(index) || destination
                Model {
                    source: "#Sphere"
                    readonly property real size: root.markerScale * (moonNode.destination ? 1.1 : 0.7)
                    scale: Qt.vector3d(size, size, size)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColor: root.bodyColors[moonNode.body] ?? "#cccccc"
                    }
                }
                Model {
                    visible: moonNode.influence > 0
                    geometry: unitCircleGeometry
                    scale: Qt.vector3d(moonNode.influence, moonNode.influence, moonNode.influence)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        vertexColorsEnabled: true
                        baseColor: root.bodyColors[moonNode.body] ?? "#cccccc"
                        opacity: moonNode.destination ? 0.9 : 0.4
                    }
                }
                Model {
                    visible: moonNode.influence > 0
                    source: "#Sphere"
                    readonly property real size: moonNode.influence / 50
                    scale: Qt.vector3d(size, size, size)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColor: root.bodyColors[moonNode.body] ?? "#cccccc"
                        opacity: moonNode.destination ? 0.07 : 0.035
                        depthDrawMode: Material.NeverDepthDraw
                    }
                }
            }
        }

        // The orbit the ship started on: where it would go without burns.
        Model {
            position: root.staticPosition
            geometry: PolylineGeometry { id: startOrbitGeometry }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                vertexColorsEnabled: true
                opacity: 0.3
            }
        }

        // The path: flown (faint) and predicted (bright), coloured by C++.
        Model {
            position: root.pathPosition
            geometry: PolylineGeometry { id: pathGeometry }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                vertexColorsEnabled: true
                lineWidth: 2
            }
        }

        // Closest approach: where you'll be, where the Earth will be, and a
        // line between them.
        Model {
            visible: root.arrivalVisible
            geometry: PolylineGeometry { id: arrivalLineGeometry }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                vertexColorsEnabled: true
                opacity: 0.8
            }
        }
        Model {
            visible: root.arrivalVisible
            source: "#Sphere"
            position: root.arrivalEarth
            readonly property real size: root.markerScale * 1.4
            scale: Qt.vector3d(size, size, size)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: root.bodyColors[root.destinationIndex] ?? "#4f9de0"
                opacity: 0.45
            }
        }
        Model {
            visible: root.arrivalVisible
            source: "#Sphere"
            position: root.arrivalShip
            readonly property real size: root.markerScale * 0.8
            scale: Qt.vector3d(size, size, size)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: root.shipColor
                opacity: 0.5
            }
        }

        // Burns: orange ahead, the edited one larger, fired ones grey.
        Repeater3D {
            model: root.burnPositions
            delegate: Model {
                id: burnMarker
                required property var modelData
                required property int index
                source: "#Sphere"
                position: modelData
                readonly property bool fired: root.burnFired[index] === true
                readonly property real size: root.markerScale * (index === root.selectedBurn ? 1.5 : 1.0)
                scale: Qt.vector3d(size, size, size)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: burnMarker.fired ? "#64748b" : root.burnColor
                }
            }
        }

        // A circle of radius 1 in the ecliptic, scaled for each sphere of
        // influence (filled from C++).
        PolylineGeometry { id: unitCircleGeometry }

        // The asteroid belt: faint dots (see asteroid_belt.hpp).
        Model {
            position: root.staticPosition
            geometry: PolylineGeometry { id: beltGeometry }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                vertexColorsEnabled: true
            }
        }

        // Flybys: violet, where the ship passes closest.
        Repeater3D {
            model: root.flybyPositions
            delegate: Model {
                required property var modelData
                source: "#Sphere"
                position: modelData
                readonly property real size: root.markerScale * 1.1
                scale: Qt.vector3d(size, size, size)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: root.flybyColor
                }
            }
        }

        Model {
            source: "#Sphere"
            position: root.shipPosition
            readonly property real size: root.markerScale * 1.2
            scale: Qt.vector3d(size, size, size)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: root.shipColor
            }
        }
    }

    // --- labels --------------------------------------------------------
    /// Where a point is on the screen (x, y in pixels), and z how far in
    /// front of the camera it is (negative: behind). mapFrom3DScene's own z
    /// comes from the depth range, which is far too coarse close up (the
    /// view reaches from 2,000 km to beyond Neptune), so it isn't used.
    function project(p) {
        root.yaw; root.pitch; root.distance; root.target
        if (!view.camera) return Qt.vector3d(0, 0, -1)
        const s = view.mapFrom3DScene(p)
        return Qt.vector3d(s.x, s.y, p.minus(camera.scenePosition).dotProduct(camera.forward))
    }
    component Label3D: Text {
        property vector3d at
        // Also after the camera itself has moved: its position follows
        // root.distance through a binding that may update after this one.
        readonly property vector3d screen: { root.yaw; root.pitch; root.distance; root.target
                                             camera.scenePosition; camera.sceneRotation
                                             return root.project(at) }
        visible: screen.z > 0
        x: screen.x + 8
        y: screen.y - height / 2
        font.pixelSize: 11
    }
    Repeater {
        model: root.planetPositions.length
        delegate: Label3D {
            required property int index
            at: root.planetPositions[index]
            text: root.planetNames[index]
            color: root.bodyColors[index]
        }
    }
    Repeater {
        model: root.moonPositions.length
        delegate: Label3D {
            required property int index
            at: root.moonPositions[index]
            visible: screen.z > 0 && (root.moonNamed(index) || index + 8 === root.destinationIndex)
            text: root.bodyNames[index + 8] ?? ""
            color: root.bodyColors[index + 8] ?? "#cccccc"
            font.pixelSize: 10
        }
    }
    Label3D { at: root.sunPosition; text: qsTr("Sun"); color: "#ffd27a" }
    Label3D {
        at: root.shipPosition
        text: qsTr("You")
        color: root.shipColor
        font.bold: true
        font.pixelSize: 12
    }
    Repeater {
        model: root.flybyPositions.length
        delegate: Label3D {
            required property int index
            at: root.flybyPositions[index]
            text: root.flybyLabels[index] ?? ""
            color: root.flybyColor
        }
    }
    Label3D {
        visible: root.arrivalVisible && screen.z > 0
        at: root.arrivalShip
        text: root.arrivalLabel
        color: "#93c5fd"
    }

    // --- hover: the object under the pointer -------------------------------
    HoverHandler { id: hover }
    /// Index into infoTexts of the object nearest the pointer (within a few
    /// pixels), or -1. Planets 0-7, the Sun 8, the ship 9, burns 10 and up,
    /// then flybys, then the moons (only those drawn).
    readonly property int hoverIndex: {
        if (!hover.hovered || root.infoTexts.length < 10) return -1
        const pointer = hover.point.position
        let best = -1
        let bestDistance = 16 // pixels
        const objects = root.planetPositions.concat([root.sunPosition, root.shipPosition],
                                                    root.burnPositions, root.flybyPositions,
                                                    root.moonPositions)
        const firstMoon = objects.length - root.moonPositions.length
        for (let i = 0; i < objects.length; ++i) {
            if (i >= firstMoon && !root.moonShown(i - firstMoon)) continue
            const s = root.project(objects[i])
            if (s.z <= 0) continue
            const d = Math.hypot(s.x - pointer.x, s.y - pointer.y)
            // The ship and the burns win ties: they are what you care about.
            if (d < bestDistance || (i >= 9 && d <= bestDistance)) { best = i; bestDistance = d }
        }
        return best
    }
    Rectangle {
        id: infoCard
        z: 10
        visible: root.hoverIndex >= 0
        x: Math.max(4, Math.min(hover.point.position.x + 18, root.width - width - 4))
        y: Math.max(4, Math.min(hover.point.position.y + 12, root.height - height - 4))
        width: infoText.implicitWidth + 20
        height: infoText.implicitHeight + 16
        radius: 6
        color: "#ee0b1220"
        border.color: "#475569"
        Text {
            id: infoText
            anchors.centerIn: parent
            textFormat: Text.RichText
            text: {
                const i = root.hoverIndex
                const burns = root.burnPositions.length
                const flybys = root.flybyPositions.length
                if (i < 0) return ""
                if (i < 10) return root.infoTexts[i]
                if (i < 10 + burns) return root.burnInfoTexts[i - 10] ?? ""
                if (i < 10 + burns + flybys) return root.flybyInfoTexts[i - 10 - burns] ?? ""
                return root.moonInfoTexts[i - 10 - burns - flybys] ?? ""
            }
            color: "#e2e8f0"
            font.pixelSize: 12
        }
    }

    CameraControls {
        scene: root
        camera: camera
        view: view
        onDoubleClicked: if (root.hoverIndex >= 0) root.centreRequested(root.hoverIndex)
    }

    // --- heads-up display ----------------------------------------------
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        width: Math.min(root.width * 0.45, 420)
        height: hud.implicitHeight + 20
        radius: 6
        color: "#cc0b1220"
        border.color: "#334155"
        visible: root.goalText.length > 0
        ColumnLayout {
            id: hud
            anchors.fill: parent
            anchors.margins: 10
            spacing: 4
            Text {
                text: qsTr("GOAL")
                color: "#94a3b8"
                font.pixelSize: 10
                font.letterSpacing: 1.5
            }
            Text {
                Layout.fillWidth: true
                text: root.goalText
                color: "#e2e8f0"
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Text {
                text: root.dateText
                color: "#f8fafc"
                font.pixelSize: 22
                font.bold: true
            }
            Text {
                visible: root.speedText !== ""
                text: root.speedText
                color: "#e2e8f0"
                font.pixelSize: 13
            }
        }
    }
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        width: Math.min(root.width * 0.4, 360)
        height: status.implicitHeight + 20
        radius: 6
        color: "#cc0b1220"
        border.color: "#334155"
        visible: root.statusText.length > 0
        Text {
            id: status
            anchors.fill: parent
            anchors.margins: 10
            text: root.statusText
            textFormat: Text.StyledText
            color: root.statusColor
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 30
        visible: root.busyText.length > 0
        text: root.busyText
        color: "#fde68a"
        font.pixelSize: 14
    }

    // --- the card --------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        color: "#99000000"
        // Only the start card dims the view; messages sit at the bottom and
        // leave the scene (the Earth!) visible and usable.
        visible: root.cardMode === "start"
        MouseArea { anchors.fill: parent } // the view stays still behind it
    }
    Rectangle {
        id: card
        visible: root.cardMode.length > 0
        readonly property bool atBottom: root.cardMode === "message"
        x: (root.width - width) / 2
        y: atBottom ? root.height - height - 16 : (root.height - height) / 2
        width: Math.min(root.width - 40, 560)
        height: Math.min(cardColumn.implicitHeight + 40, root.height - 32)
        radius: 10
        color: "#0f172a"
        border.color: "#475569"
        ColumnLayout {
            id: cardColumn
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12
            Text {
                text: root.cardTitle
                color: "#f8fafc"
                font.pixelSize: 24
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: root.cardText
                textFormat: Text.StyledText
                color: "#cbd5e1"
                font.pixelSize: 14
                wrapMode: Text.WordWrap
                lineHeight: 1.2
            }
            // Start: where from...
            component Chip: Button {
                id: chip
                property bool chosen: false
                implicitHeight: 28
                leftPadding: 12
                rightPadding: 12
                opacity: enabled ? 1.0 : 0.35
                contentItem: Text {
                    text: chip.text
                    color: chip.chosen ? "#0f172a" : "#e2e8f0"
                    font.pixelSize: 13
                    font.bold: chip.chosen
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 14
                    color: chip.chosen ? "#93c5fd" : (chip.hovered ? "#1e3a5f" : "#1e293b")
                    border.color: chip.chosen || chip.hovered ? "#60a5fa" : "#334155"
                }
            }
            component Heading: Text {
                visible: root.puzzleStart
                color: "#f8fafc"
                font.pixelSize: 14
                font.bold: true
            }
            // Play: a puzzle, or free roam.
            Flow {
                visible: root.cardMode === "start"
                Layout.fillWidth: true
                spacing: 6
                Chip {
                    objectName: "button-mode-puzzle"
                    text: qsTr("A puzzle")
                    chosen: root.startMode === "puzzle"
                    onClicked: root.startMode = "puzzle"
                }
                Chip {
                    objectName: "button-mode-roam"
                    text: qsTr("Free roam")
                    chosen: root.startMode === "roam"
                    onClicked: root.startMode = "roam"
                }
            }
            // Free roam: in orbit around a planet...
            Heading {
                visible: root.roamStart
                text: qsTr("Start in orbit around:")
            }
            Flow {
                visible: root.roamStart
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: root.roamStart ? root.planetNames : []
                    delegate: Chip {
                        required property var modelData
                        required property int index
                        objectName: "button-roam-" + index
                        text: modelData
                        chosen: index === root.roamPlanet
                        onClicked: root.roamBody = index
                    }
                }
            }
            // ...or one of its moons...
            Flow {
                visible: root.roamStart && root.roamMoons.length > 0
                Layout.fillWidth: true
                spacing: 6
                Text {
                    height: 28
                    text: root.roamMoons.length === 1 ? qsTr("Or its moon:") : qsTr("Or one of its moons:")
                    color: "#94a3b8"
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                }
                Repeater {
                    model: root.roamStart ? root.roamMoons : []
                    delegate: Chip {
                        required property int modelData
                        objectName: "button-roam-" + modelData
                        text: root.bodyNames[modelData] ?? ""
                        chosen: modelData === root.roamBody
                        // Too small to orbit, so near its planet.
                        enabled: root.bodyParkable[modelData] === true
                        onClicked: root.roamBody = modelData
                    }
                }
            }
            Text {
                visible: root.roamStart
                Layout.fillWidth: true
                text: qsTr("No goal and unlimited fuel: go anywhere, try anything. Speed up to climb, slow down to fall, and watch where each burn takes you. Burns cost nothing here; the game adds up their Δv, the number a real mission would need propellant for.")
                color: "#94a3b8"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Button {
                visible: root.roamStart
                objectName: "button-start-roam"
                Layout.fillWidth: true
                implicitHeight: 46
                contentItem: Column {
                    spacing: 1
                    leftPadding: 6
                    Text { text: qsTr("Start exploring"); color: "#f8fafc"; font.pixelSize: 15; font.bold: true }
                    Text {
                        text: qsTr("In orbit around %1").arg(root.nameInText(root.roamBody, false))
                        color: "#94a3b8"
                        font.pixelSize: 12
                    }
                }
                background: Rectangle {
                    radius: 6
                    color: parent.hovered ? "#1e3a5f" : "#1e293b"
                    border.color: parent.hovered ? "#60a5fa" : "#334155"
                }
                onClicked: root.freeRoamChosen(root.roamBody)
            }
            Heading { text: qsTr("Where are you?") }
            Flow {
                visible: root.puzzleStart
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: root.puzzleStart ? root.regions : []
                    delegate: Chip {
                        required property var modelData
                        required property int index
                        objectName: "button-region-" + index
                        text: modelData.name
                        chosen: index === root.regionIndex
                        onClicked: root.chooseRegion(index)
                    }
                }
            }
            // ...or around one of its moons...
            Flow {
                visible: root.puzzleStart && root.startMoons.length > 0
                Layout.fillWidth: true
                spacing: 6
                Text {
                    height: 28
                    text: root.startMoons.length === 1 ? qsTr("Or in orbit around its moon:")
                                                       : qsTr("Or in orbit around one of its moons:")
                    color: "#94a3b8"
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                }
                Repeater {
                    model: root.puzzleStart ? root.startMoons : []
                    delegate: Chip {
                        required property int modelData
                        objectName: "button-start-moon-" + modelData
                        text: root.bodyNames[modelData] ?? ""
                        chosen: modelData === root.startMoonIndex
                        onClicked: root.chooseStartMoon(modelData)
                    }
                }
            }
            CheckBox {
                id: orbitBox
                visible: root.puzzleStart && root.startPlanet >= 0 && !root.atMoon
                objectName: "check-in-orbit"
                enabled: root.startPlanet !== 2 && root.destinationPlanet !== root.startPlanet
                checked: root.startsInOrbit
                onToggled: root.inOrbit = checked
                contentItem: Text {
                    leftPadding: orbitBox.indicator.width + 6
                    text: qsTr("In orbit around it")
                    color: "#e2e8f0"
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Text {
                visible: root.puzzleStart
                Layout.fillWidth: true
                text: root.atMoon
                      ? qsTr("You start in a low orbit around %1, which goes round %2. Leaving takes two climbs: out of %1's gravity, then out of %2's, though %1's speed round %2 comes with you.")
                            .arg(root.nameInText(root.startMoonIndex, false)).arg(root.nameInText(root.startPlanet, false))
                      : root.regions[root.regionIndex].about
                        + (root.startsInOrbit && root.startPlanet !== 2
                           ? " " + qsTr("You start in orbit around it and must climb out first.") : "")
                color: "#94a3b8"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            // ...where to...
            Heading { text: qsTr("Where to?"); Layout.topMargin: 2 }
            Flow {
                visible: root.puzzleStart
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: root.puzzleStart ? root.planetNames : []
                    delegate: Chip {
                        required property var modelData
                        required property int index
                        objectName: "button-destination-" + index
                        text: index === 2 ? qsTr("Earth (home)") : modelData
                        chosen: index === root.destinationPlanet
                        enabled: index !== root.startPlanet
                                 || (root.atMoon && index === 2)
                                 || (root.planetMoons[index] ?? []).some(m => m !== root.startMoonIndex)
                        onClicked: root.chooseDestinationPlanet(index)
                    }
                }
            }
            // ...or one of its moons...
            Flow {
                visible: root.puzzleStart && root.destinationMoons.length > 0
                Layout.fillWidth: true
                spacing: 6
                Text {
                    height: 28
                    text: root.destinationMoons.length === 1 ? qsTr("Or its moon:")
                                                              : qsTr("Or one of its moons:")
                    color: "#94a3b8"
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                }
                Repeater {
                    model: root.puzzleStart ? root.destinationMoons : []
                    delegate: Chip {
                        required property int modelData
                        objectName: "button-destination-" + modelData
                        text: root.bodyNames[modelData] ?? ""
                        chosen: modelData === root.destinationIndex
                        enabled: modelData !== root.startMoonIndex
                        onClicked: root.destinationIndex = modelData
                    }
                }
            }
            Text {
                visible: root.puzzleStart && root.toMoon
                Layout.fillWidth: true
                text: root.destinationPlanet === root.startPlanet && root.atMoon
                      ? qsTr("%1 goes round %2, as %3 does: leave %3 at the right moment to meet it.")
                            .arg(root.nameInText(root.destinationIndex, true)).arg(root.nameInText(root.destinationPlanet, false))
                            .arg(root.nameInText(root.startMoonIndex, false))
                      : root.destinationPlanet === root.startPlanet
                      ? qsTr("%1 goes round %2, and so do you: wait for the right moment in your orbit, then set off to meet it.")
                            .arg(root.nameInText(root.destinationIndex, true)).arg(root.nameInText(root.destinationPlanet, false))
                      : qsTr("%1 goes round %2: get to %2 first, then aim for %1 once you're near.")
                            .arg(root.nameInText(root.destinationIndex, true)).arg(root.nameInText(root.destinationPlanet, false))
                color: "#94a3b8"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            // ...and how hard.
            Heading { text: qsTr("How hard?"); Layout.topMargin: 2 }
            Repeater {
                model: !root.puzzleStart ? [] : root.homeFromMoon ? [
                    { name: qsTr("Easy"), goal: qsTr("Fall home and hit the atmosphere at just the right angle. Plenty of fuel.") },
                    { name: qsTr("Normal"), goal: qsTr("The same, with less to spare.") },
                    { name: qsTr("Hard"), goal: qsTr("The same, on tight fuel.") }
                ] : root.goingHome ? [
                    { name: qsTr("Easy"), goal: qsTr("Reach the Earth's neighbourhood. Plenty of fuel.") },
                    { name: qsTr("Normal"), goal: qsTr("Hit the atmosphere at just the right angle to land.") },
                    { name: qsTr("Hard"), goal: qsTr("Brake into orbit around the Earth. Fuel is tight.") }
                ] : root.toMoon ? [
                    { name: qsTr("Easy"), goal: qsTr("Reach %1's neighbourhood. Plenty of fuel.").arg(root.nameInText(root.destinationIndex, false)) },
                    { name: qsTr("Normal"), goal: qsTr("Fly close past %1.").arg(root.nameInText(root.destinationIndex, false)) },
                    root.bodyParkable[root.destinationIndex]
                        ? { name: qsTr("Hard"), goal: qsTr("Brake into orbit around %1. Fuel is tight.").arg(root.nameInText(root.destinationIndex, false)) }
                        : { name: qsTr("Hard"), goal: qsTr("Fly close past %1 on tight fuel (too small to orbit so near %2).")
                                                          .arg(root.nameInText(root.destinationIndex, false)).arg(root.nameInText(root.destinationPlanet, false)) }
                ] : [
                    { name: qsTr("Easy"), goal: qsTr("Reach %1's neighbourhood. Plenty of fuel.").arg(root.destinationName) },
                    { name: qsTr("Normal"), goal: qsTr("Fly close past %1, like Voyager.").arg(root.destinationName) },
                    { name: qsTr("Hard"), goal: qsTr("Brake into orbit around %1, like Cassini or Juno. Fuel is tight.").arg(root.destinationName) }
                ]
                delegate: Button {
                    required property var modelData
                    required property int index
                    objectName: "button-difficulty-" + index
                    Layout.fillWidth: true
                    implicitHeight: 46
                    contentItem: Column {
                        spacing: 1
                        leftPadding: 6
                        Text { text: modelData.name; color: "#f8fafc"; font.pixelSize: 15; font.bold: true }
                        Text { text: modelData.goal; color: "#94a3b8"; font.pixelSize: 12 }
                    }
                    background: Rectangle {
                        radius: 6
                        color: parent.hovered ? "#1e3a5f" : "#1e293b"
                        border.color: parent.hovered ? "#60a5fa" : "#334155"
                    }
                    onClicked: root.startChosen(root.regionIndex, root.startsInOrbit,
                                                root.startMoonIndex, root.destinationIndex, index)
                }
            }
            Text {
                visible: root.cardMode === "start" && root.cardsHidden
                objectName: "link-show-hidden"
                text: qsTr("Show the messages you hid again")
                color: "#60a5fa"
                font.pixelSize: 12
                font.underline: linkArea.containsMouse
                MouseArea {
                    id: linkArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.hiddenCardsReset()
                }
            }
            RowLayout {
                visible: root.cardMode === "message"
                Layout.fillWidth: true
                CheckBox {
                    id: hideBox
                    visible: root.cardHideable
                    objectName: "check-hide-card"
                    contentItem: Text {
                        leftPadding: hideBox.indicator.width + 6
                        text: qsTr("Don't show this again")
                        color: "#94a3b8"
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: root.cardButton
                    onClicked: {
                        root.cardAccepted(hideBox.visible && hideBox.checked)
                        hideBox.checked = false
                    }
                }
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 8
        color: "#64748b"
        font.pixelSize: 11
        text: qsTr("Drag to turn · right-drag to pan · wheel to zoom where you point · double-click to centre · ←/→ step time, ↑/↓ further · planets not to scale")
    }
}
