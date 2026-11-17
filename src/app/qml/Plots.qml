// SPDX-License-Identifier: GPL-3.0-or-later
// The three time graphs. PlotPanel finds each graph through the
// properties below and feeds it data.

import QtQuick
import QtQuick.Layouts

Rectangle {
    color: "#0b1222"

    property alias altitudeGraph: altitude
    property alias speedGraph: speed
    property alias massGraph: mass

    // Side by side when the panel is wide (docked at the bottom), stacked
    // when it is tall (docked at the side).
    GridLayout {
        anchors.fill: parent
        anchors.margins: 4
        columns: width > height * 1.8 ? 3 : 1
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
    }
}
