// SPDX-License-Identifier: GPL-3.0-or-later
// The four time graphs. PlotPanel finds each graph through the
// properties below and feeds it data.

import QtQuick
import QtQuick.Layouts

Rectangle {
    color: "#0b1222"

    property alias altitudeGraph: altitude
    property alias speedGraph: speed
    property alias massGraph: mass
    property alias pressureGraph: pressure

    // Side by side when the panel is wide (docked at the bottom), two by
    // two when it is squarer, stacked when it is tall (docked at the side).
    GridLayout {
        anchors.fill: parent
        anchors.margins: 4
        columns: width > height * 2.4 ? 4 : width > height * 0.8 ? 2 : 1
        rowSpacing: 6
        columnSpacing: 12

        TimeGraph {
            id: altitude
            title: qsTr("Altitude (km)")
            lineColor: "#38bdf8"
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        TimeGraph {
            id: speed
            title: qsTr("Speed, inertial (m/s)")
            lineColor: "#4ade80"
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        TimeGraph {
            id: mass
            title: qsTr("Mass (t)")
            lineColor: "#fbbf24"
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        TimeGraph {
            id: pressure
            title: qsTr("Dynamic pressure (kPa)")
            lineColor: "#f472b6"
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
