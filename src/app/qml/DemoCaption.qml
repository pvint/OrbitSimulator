// SPDX-License-Identifier: GPL-3.0-or-later
// The demo tour's caption box, at the top of a 3D scene: a title, the text,
// which step this is, and buttons to move through the tour.

import QtQuick
import QtQuick.Controls

Rectangle {
    id: caption
    property string title
    property string text
    property string step // e.g. "3 / 11"
    signal previous()
    signal next()
    signal end()

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
            text: caption.title
            color: "#f1f5f9"
            font.pixelSize: 17
            font.bold: true
        }
        Text {
            width: parent.width
            text: caption.text
            color: "#d6deeb"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            lineHeight: 1.15
        }
        Row {
            spacing: 8
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: caption.step
                color: "#8592ad"
                font.pixelSize: 12
                rightPadding: 6
            }
            Button { text: qsTr("◀ Previous"); onClicked: caption.previous() }
            Button { text: qsTr("Next ▶"); onClicked: caption.next() }
            Button { text: qsTr("End tour"); onClicked: caption.end() }
        }
    }
}
