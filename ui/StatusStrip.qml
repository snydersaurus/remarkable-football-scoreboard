import QtQuick

// Small-caps bar with a rule under it.
Item {
    id: strip

    property string leftText: ""
    property string rightText: ""
    property color ink: "#000000"
    property color faint: "#9A9A9A"   // the rule
    property color muted: "#545454"   // the text
    property color accent: "#A4123F"
    property bool accentRight: false
    property real u: 1.0

    implicitHeight: Math.round(62 * u)

    Text {
        anchors.left: parent.left
        anchors.top: parent.top
        text: strip.leftText
        color: strip.ink
        // Fixed at 32px this was the smallest type on the page on the Move's
        // landscape canvas, and small DemiBold washes out on Gallery 3 --
        // hierarchy here comes from size and weight, never from grey.
        font.pixelSize: Math.round(34 * strip.u)
        font.letterSpacing: 5 * strip.u
        font.weight: Font.Bold
    }

    Text {
        anchors.right: parent.right
        anchors.top: parent.top
        text: strip.rightText
        color: strip.accentRight ? strip.accent : strip.muted
        font.pixelSize: Math.round(34 * strip.u)
        font.letterSpacing: 5 * strip.u
        font.weight: Font.Bold
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 3
        color: strip.faint
    }
}
