import QtQuick
import QtQuick.Layouts

// One game in a team's season: week, date, opponent, and either the result, the
// game clock, or kickoff. A bye is a row too -- a college season reads wrong
// without it, and a missing week looks like a bug.
Item {
    id: row

    property color ink
    property color faint
    property color muted
    // The loser of a finished game recedes. Kept separate from `muted` so
    // darkening secondary text does not flatten the win/loss distinction.
    // Dark enough to read on Gallery 3, light enough to still read as beaten.
    // #8A8A8A looked right on a monitor and was close to invisible on paper.
    property color dimmed: "#4A4A4A"
    property color accent
    property var   game: ({})
    property bool  current: false      // the game the board is following
    property var   logoSource: function (id) { return "" }
    property int   logoRev: 0
    property real  u: 1.0

    signal picked()

    readonly property bool bye:   game.bye === true
    readonly property bool live:  game.isLive === true
    readonly property bool over:  game.isFinal === true
    readonly property bool lost:  over && game.won !== true
                                       && game.teamScore !== game.oppScore

    implicitHeight: Math.round(78 * u)

    // On the Move's portrait canvas two season columns are only ~500 units
    // wide, and the full row (week, date, logo, opponent, result) does not fit:
    // it silently overflowed into the next column. Below that width the week
    // number goes -- the date says the same thing in a form the reader already
    // knows -- and everything else tightens.
    readonly property bool compact: width > 0 && width < 620 * u

    // Live games get the one piece of colour on the page.
    Rectangle {
        id: marker
        width: Math.round(8 * row.u)
        height: parent.height * 0.62
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        color: row.live ? row.accent : "transparent"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Math.round(24 * row.u)
        spacing: Math.round((row.compact ? 12 : 16) * row.u)

        Text {
            visible: !row.compact
            text: row.game.week !== undefined ? row.game.week : ""
            color: row.muted
            font.pixelSize: Math.round(28 * row.u)
            font.weight: Font.Bold
            font.letterSpacing: 2
            Layout.preferredWidth: Math.round(96 * row.u)
        }

        // A bye has no date of its own, so in compact rows -- where the week
        // number is gone -- it borrows this column to say which week it was.
        Text {
            text: row.bye
                  ? (row.compact ? (row.game.week !== undefined ? row.game.week : "") : "")
                  : (row.game.date !== undefined ? row.game.date : "")
            color: row.muted
            font.pixelSize: Math.round((row.compact ? 28 : 30) * row.u)
            font.weight: Font.Bold
            Layout.preferredWidth: Math.round((row.compact ? 104 : 126) * row.u)
        }

        TeamLogo {
            Layout.preferredWidth: Math.round((row.compact ? 46 : 54) * row.u)
            Layout.preferredHeight: Math.round((row.compact ? 46 : 54) * row.u)
            opacity: row.lost ? 0.7 : 1.0
            path: row.bye ? "" : row.logoSource(row.game.oppId)
            rev: row.logoRev
        }

        // Home and away is the difference between two very different games, so
        // it leads the opponent rather than hiding at the end of the line.
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: {
                if (row.bye)
                    return "BYE"
                var s = (row.game.home === true ? "vs " : "at ")
                if (row.game.oppRank > 0)
                    s += "#" + row.game.oppRank + " "
                return s + (row.game.oppAbbr !== undefined ? row.game.oppAbbr : "")
            }
            color: row.bye ? row.muted : (row.lost ? row.dimmed : row.ink)
            font.pixelSize: Math.round((row.compact ? 34 : 38) * row.u)
            font.weight: (row.current && !row.bye) ? Font.Bold : Font.Normal
            font.italic: row.bye
            elide: Text.ElideRight
        }

        Text {
            text: row.game.note !== undefined ? row.game.note : ""
            color: row.bye ? row.muted
                 : row.live ? row.accent
                 : row.lost ? row.dimmed : row.ink
            font.pixelSize: Math.round(34 * row.u)
            font.weight: Font.Bold
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: Math.round((row.compact ? 140 : 180) * row.u)
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.max(1, Math.round(2 * row.u))
        color: row.faint
    }

    // A bye is not a game and has nothing to open.
    TapHandler {
        enabled: !row.bye
        onTapped: row.picked()
    }
}
