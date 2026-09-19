import QtQuick

// Where the ball is, drawn as the field itself: a hundred yards left to right,
// the team in possession attacking rightwards. This is football's answer to the
// base diamond -- one glance has to answer "how far have they got, and how far
// to go", which no amount of text does as fast.
//
// ESPN's yardLine is measured from the possessing team's own goal line, so
// ballOn maps straight onto this strip with no direction flag needed.
Item {
    id: f

    // 0-100 from the possessing team's own goal line. -1 = nothing to draw.
    property int ballOn: -1
    property int toGo: 0
    property bool redZone: false
    property string ownAbbr: ""        // the team with the ball, on the left
    property string oppAbbr: ""        // the team defending, on the right
    property color ink: "#000000"
    property color faint: "#5A5A5A"
    property color accent: "#A4123F"
    property real u: 1.0

    // End zones are drawn as their own blocks either side of the playing field,
    // which is what makes the strip read as a field rather than a progress bar.
    readonly property real endZone: Math.round(58 * u)
    readonly property real fieldX: endZone
    readonly property real fieldW: Math.max(1, width - endZone * 2)
    readonly property real stroke: Math.max(2, Math.round(3 * u))

    function xFor(yard) {
        return fieldX + fieldW * Math.max(0, Math.min(100, yard)) / 100
    }

    implicitHeight: Math.round(120 * u)

    // The playing field.
    Rectangle {
        id: field
        x: f.fieldX
        width: f.fieldW
        y: 0
        height: parent.height
        color: "transparent"
        border.width: f.stroke
        border.color: f.faint
    }

    // The defending team's red zone. Outlined, never filled: a twenty-yard
    // block of ink would be the largest dark area on the page and e-ink
    // repaints it slowly.
    Rectangle {
        x: f.xFor(80)
        width: f.xFor(100) - f.xFor(80)
        y: 0
        height: parent.height
        color: "transparent"
        border.width: f.redZone ? Math.max(3, f.stroke * 2) : f.stroke
        border.color: f.redZone ? f.accent : f.faint
    }

    // Ten-yard lines, with the fifty drawn heavier so the halves read apart.
    Repeater {
        model: [10, 20, 30, 40, 50, 60, 70, 80, 90]
        Rectangle {
            x: f.xFor(modelData) - width / 2
            y: Math.round(14 * f.u)
            width: modelData === 50 ? f.stroke * 2 : f.stroke
            height: f.height - Math.round(28 * f.u)
            color: f.faint
        }
    }

    // End zones, labelled with whose they are.
    Repeater {
        model: [ { at: 0,    abbr: f.ownAbbr },
                 { at: 100,  abbr: f.oppAbbr } ]

        Item {
            x: modelData.at === 0 ? 0 : f.width - f.endZone
            width: f.endZone
            height: f.height

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: f.stroke
                border.color: f.faint
            }
            Text {
                anchors.centerIn: parent
                rotation: modelData.at === 0 ? -90 : 90
                text: modelData.abbr
                color: f.ink
                font.pixelSize: Math.round(28 * f.u)
                font.weight: Font.Bold
                font.letterSpacing: 2
            }
        }
    }

    // The line to gain, ahead of the ball.
    Rectangle {
        visible: f.ballOn >= 0 && f.toGo > 0 && f.ballOn + f.toGo <= 100
        x: f.xFor(f.ballOn + f.toGo) - width / 2
        y: 0
        width: Math.max(3, Math.round(4 * f.u))
        height: f.height
        color: f.accent
    }

    // The ball. A tick the full height of the field plus a marker, so it is
    // findable at arm's length without being a big dark blob.
    Item {
        visible: f.ballOn >= 0
        x: f.xFor(f.ballOn)
        y: 0
        height: f.height

        Rectangle {
            x: -Math.max(3, Math.round(5 * f.u)) / 2
            width: Math.max(3, Math.round(5 * f.u))
            height: parent.height
            color: f.ink
        }
        Rectangle {
            property real s: Math.round(26 * f.u)
            x: -s / 2
            y: -s / 2
            width: s
            height: s
            radius: s / 2
            color: f.ink
        }
    }
}
