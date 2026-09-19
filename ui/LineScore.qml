import QtQuick

// Quarter-by-quarter with the total on the right. Football's line score is the
// same idea as baseball's, minus hits and errors: there is only one number that
// matters per quarter.
Column {
    id: ls

    property var periods: []

    // Four quarters are always on screen, even before a point is scored and
    // even if the feed has not sent a linescore yet -- a grid that grows a
    // column at a time is worse than one with dashes in it, and an empty grid
    // showing nothing but the total reads as broken.
    readonly property var cols: (periods && periods.length >= 4)
        ? periods
        : [ { num: "1", away: "-", home: "-" },
            { num: "2", away: "-", home: "-" },
            { num: "3", away: "-", home: "-" },
            { num: "4", away: "-", home: "-" } ]
    property string awayAbbr: ""
    property string homeAbbr: ""
    property var awayTotal: 0
    property var homeTotal: 0

    property color ink: "#000000"
    property color faint: "#9A9A9A"   // rules and outlines
    property color muted: "#545454"   // secondary text
    property real teamColWidth: 150
    property real cellWidth: (width - teamColWidth) / (cols.length + 1)
    property real fontSize: 34

    spacing: 10

    component Cell: Text {
        property bool heavy: false
        width: ls.cellWidth
        color: ls.ink
        font.pixelSize: ls.fontSize
        font.weight: heavy ? Font.Bold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
    }

    // Header
    Row {
        Text {
            width: ls.teamColWidth
            text: ""
            font.pixelSize: ls.fontSize
        }
        Repeater {
            model: ls.cols
            Cell {
                text: modelData.num
                color: ls.muted
                font.weight: Font.Bold
            }
        }
        Cell { text: "T"; heavy: true }
    }

    Rectangle { width: ls.width; height: 3; color: ls.faint }

    // Away
    Row {
        Text {
            width: ls.teamColWidth
            text: ls.awayAbbr
            color: ls.ink
            font.pixelSize: ls.fontSize
            font.weight: Font.Bold
            font.letterSpacing: 2
        }
        Repeater {
            model: ls.cols
            Cell { text: modelData.away }
        }
        Cell { text: ls.awayTotal; heavy: true }
    }

    // Home
    Row {
        Text {
            width: ls.teamColWidth
            text: ls.homeAbbr
            color: ls.ink
            font.pixelSize: ls.fontSize
            font.weight: Font.Bold
            font.letterSpacing: 2
        }
        Repeater {
            model: ls.cols
            Cell { text: modelData.home }
        }
        Cell { text: ls.homeTotal; heavy: true }
    }
}
