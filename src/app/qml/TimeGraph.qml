// SPDX-License-Identifier: GPL-3.0-or-later
// One quantity against time: a title, and a line graph underneath. C++
// (PlotPanel) fills `series` and sets the axis ranges.

import QtQuick
import QtGraphs

Item {
    id: root

    property string title
    property string timeUnit: "s"
    property color lineColor: "#38bdf8"
    property alias series: line
    property alias view: graphsView
    property alias xAxis: xAxis
    property alias yAxis: yAxis

    Text {
        id: heading
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.top: parent.top
        text: root.title
        color: "#c8d2e6"
        font.pixelSize: 12
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.baseline: heading.baseline
        text: qsTr("time (%1)").arg(root.timeUnit)
        color: "#8592ad"
        font.pixelSize: 11
    }

    GraphsView {
        id: graphsView
        anchors.top: heading.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        marginLeft: 4
        marginRight: 12
        marginTop: 4
        marginBottom: 4

        theme: GraphsTheme {
            colorScheme: GraphsTheme.ColorScheme.Dark
            backgroundColor: "#0b1222"
            plotAreaBackgroundColor: "#0b1222"
            labelTextColor: "#9aa6bf"
            grid.mainColor: "#1f2a44"
            grid.subColor: "#151d31"
        }

        axisX: ValueAxis {
            id: xAxis
            max: 1
            subTickCount: 0
            labelFormat: "%.0f"
        }
        axisY: ValueAxis {
            id: yAxis
            max: 1
            subTickCount: 0
            labelFormat: "%.0f"
        }

        LineSeries {
            id: line
            color: root.lineColor
            width: 2
        }
    }
}
