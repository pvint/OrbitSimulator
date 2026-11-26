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
    /// The eight planets, Mercury first (vector3d each), at the game clock.
    property var planetPositions: []
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
    /// What the camera circles: 0 = the Sun, 1 = the ship, 2 = the Earth.
    property int followTarget: 0
    /// What hovering shows, as rich text: the eight planets, then the Sun,
    /// then the ship (ten strings). Burns come after, in burnInfoTexts.
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

    /// The last game's choices (orbit::Region's and orbit::Planet's order),
    /// kept for the next: where from, in orbit there or not, where to.
    property int regionIndex: 0
    property bool inOrbit: false
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
          about: qsTr("Home: start in a low orbit, 200 km up, and set off for another planet.") },
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
    /// In orbit: always around the Earth, never where there's no planet.
    readonly property bool startsInOrbit: startPlanet === 2 || (startPlanet >= 0 && inOrbit)
    readonly property bool goingHome: destinationIndex === 2
    readonly property string destinationName: planetNames[destinationIndex] ?? ""
    /// Picking where from: not to the same planet (from the Earth, Mars).
    function chooseRegion(index) {
        regionIndex = index
        if (regions[index].planet === destinationIndex) destinationIndex = destinationIndex === 2 ? 3 : 2
    }

    signal startChosen(int region, bool inOrbit, int destination, int difficulty)
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
    function planetOrbit(index) { return planetOrbits.objectAt(index) }

    readonly property var planetNames: [qsTr("Mercury"), qsTr("Venus"), qsTr("Earth"), qsTr("Mars"),
                                        qsTr("Jupiter"), qsTr("Saturn"), qsTr("Uranus"), qsTr("Neptune")]
    readonly property var planetColors: ["#a8a29e", "#e9c46a", "#4f9de0", "#d9603b",
                                         "#d8a66c", "#e3c98f", "#8fd3e0", "#5b7fe8"]
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
    readonly property vector3d destinationPosition:
        planetPositions.length > destinationIndex ? planetPositions[destinationIndex]
                                                  : Qt.vector3d(0, 0, 0)
    /// What the camera circles: 0 the Sun, 1 the ship, 2 the Earth, 3 the
    /// destination.
    readonly property vector3d centrePoint: followTarget === 1 ? shipPosition
                                          : followTarget === 2 ? earthPosition
                                          : followTarget === 3 ? destinationPosition
                                          : Qt.vector3d(0, 0, 0)
    readonly property vector3d target: centrePoint.plus(panOffset)
    readonly property real minDistance: followTarget === 0 ? 2 * sunRadius : 0.02
    readonly property real maxDistance: 40000

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
            delegate: Model {
                required property var modelData
                required property int index
                source: "#Sphere"
                position: modelData
                // The Earth a little larger: it's where you're going.
                readonly property real size: root.markerScale * (index === 2 ? 1.4 : 1.0)
                scale: Qt.vector3d(size, size, size)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: root.planetColors[index]
                }
            }
        }

        // The orbit the ship started on: where it would go without burns.
        Model {
            geometry: PolylineGeometry { id: startOrbitGeometry }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                vertexColorsEnabled: true
                opacity: 0.3
            }
        }

        // The path: flown (faint) and predicted (bright), coloured by C++.
        Model {
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
                baseColor: root.planetColors[2]
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
    function project(p) {
        root.yaw; root.pitch; root.distance; root.target
        return view.camera ? view.mapFrom3DScene(p) : Qt.vector3d(0, 0, -1)
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
            color: root.planetColors[index]
        }
    }
    Label3D { at: Qt.vector3d(0, 0, 0); text: qsTr("Sun"); color: "#ffd27a" }
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
    /// then flybys.
    readonly property int hoverIndex: {
        if (!hover.hovered || root.infoTexts.length < 10) return -1
        const pointer = hover.point.position
        let best = -1
        let bestDistance = 16 // pixels
        const objects = root.planetPositions.concat([Qt.vector3d(0, 0, 0), root.shipPosition],
                                                    root.burnPositions, root.flybyPositions)
        for (let i = 0; i < objects.length; ++i) {
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
                if (i < 0) return ""
                if (i < 10) return root.infoTexts[i]
                if (i < 10 + burns) return root.burnInfoTexts[i - 10] ?? ""
                return root.flybyInfoTexts[i - 10 - burns] ?? ""
            }
            color: "#e2e8f0"
            font.pixelSize: 12
        }
    }

    CameraControls {
        scene: root
        camera: camera
        view: view
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
                visible: root.cardMode === "start"
                color: "#f8fafc"
                font.pixelSize: 14
                font.bold: true
            }
            Heading { text: qsTr("Where are you?") }
            Flow {
                visible: root.cardMode === "start"
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: root.cardMode === "start" ? root.regions : []
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
            CheckBox {
                id: orbitBox
                visible: root.cardMode === "start" && root.startPlanet >= 0
                objectName: "check-in-orbit"
                enabled: root.startPlanet !== 2
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
                visible: root.cardMode === "start"
                Layout.fillWidth: true
                text: root.regions[root.regionIndex].about
                      + (root.startsInOrbit && root.startPlanet !== 2
                         ? " " + qsTr("You start in orbit around it and must climb out first.") : "")
                color: "#94a3b8"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            // ...where to...
            Heading { text: qsTr("Where to?"); Layout.topMargin: 2 }
            Flow {
                visible: root.cardMode === "start"
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: root.cardMode === "start" ? root.planetNames : []
                    delegate: Chip {
                        required property var modelData
                        required property int index
                        objectName: "button-destination-" + index
                        text: index === 2 ? qsTr("Earth (home)") : modelData
                        chosen: index === root.destinationIndex
                        enabled: index !== root.startPlanet
                        onClicked: root.destinationIndex = index
                    }
                }
            }
            // ...and how hard.
            Heading { text: qsTr("How hard?"); Layout.topMargin: 2 }
            Repeater {
                model: root.cardMode !== "start" ? [] : root.goingHome ? [
                    { name: qsTr("Easy"), goal: qsTr("Reach the Earth's neighbourhood. Plenty of fuel.") },
                    { name: qsTr("Normal"), goal: qsTr("Hit the atmosphere at just the right angle to land.") },
                    { name: qsTr("Hard"), goal: qsTr("Brake into orbit around the Earth. Fuel is tight.") }
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
                                                root.destinationIndex, index)
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
        text: qsTr("Drag to turn · right-drag to pan · wheel to zoom · ←/→ step time, ↑/↓ further, Shift finer · planets not to scale")
    }
}
