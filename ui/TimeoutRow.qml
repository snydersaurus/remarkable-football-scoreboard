import QtQuick

// One labelled row of pips, e.g.  OSU TIMEOUTS  ● ● ○
Row {
    id: row

    property string label: ""
    property int value: 0
    property int total: 3
    property color ink: "#000000"
    property color faint: "#9A9A9A"
    property real pip: 42
    property real labelWidth: 215
    property real gap: 22
    property real labelSize: 34

    spacing: gap

    Text {
        width: row.labelWidth
        text: row.label
        color: row.ink
        font.pixelSize: row.labelSize
        font.letterSpacing: 4
        font.weight: Font.Bold
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
    }

    Repeater {
        model: row.total
        Rectangle {
            width: row.pip
            height: row.pip
            radius: width / 2
            antialiasing: true
            anchors.verticalCenter: parent.verticalCenter
            // A timeout not yet taken is filled; a spent one is an empty ring.
            color: index < row.value ? row.ink : "transparent"
            border.color: index < row.value ? row.ink : row.faint
            border.width: 4
        }
    }
}
