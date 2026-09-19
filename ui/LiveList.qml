import QtQuick
import QtQuick.Layouts

// Everything being played in college football right now, as a grid of cards.
// Tap one to put it on the board -- which is the only way to watch a game
// neither followed team is in, and the only way to see the live board at all
// on a day when Ohio State and Youngstown State are both idle.
Item {
    id: list

    property color ink
    property color faint
    property color muted
    property color accent
    property var games: []
    property int liveCount: 0
    property int dayCount: 0
    property string dateLabel: ""
    property bool loaded: false
    property bool headerVisible: true
    property var logoSource: function (id) { return "" }
    property int logoRev: 0
    property real u: 1.0

    signal gamePicked(var row)
    signal dismissed()

    readonly property real gap: 26 * list.u
    // Big enough to read at arm's length beats fitting as many as possible.
    readonly property int cols: width > height ? 3 : 2

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Text {
            visible: list.headerVisible
            text: {
                if (!list.loaded)
                    return "LOADING…"
                if (list.liveCount > 0)
                    return list.liveCount + (list.liveCount === 1 ? " GAME" : " GAMES")
                           + " IN PROGRESS"
                // Nothing on: say so, and say what the rest of the list is,
                // rather than looking like it failed to load.
                return list.dayCount > 0
                    ? "NOTHING IN PROGRESS — " + list.dateLabel
                    : "NO GAMES — " + list.dateLabel
            }
            color: list.liveCount > 0 ? list.accent : list.muted
            font.pixelSize: 40 * list.u
            font.letterSpacing: 5 * list.u
            font.bold: true
        }

        Item { Layout.preferredHeight: 24 * list.u }

        // A Saturday afternoon can have a dozen games in progress at once,
        // which is taller than the page. Same treatment as the season list.
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: grid.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            flickDeceleration: 6000
            maximumFlickVelocity: 1400

        GridLayout {
            id: grid
            width: flick.width
            columns: list.cols
            columnSpacing: list.gap
            rowSpacing: list.gap

            Repeater {
                model: list.games

                MatchupCard {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1      // let the grid divide evenly
                    // Explicit, rather than trusting the grid to distribute:
                    // minimumWidth 0 lets a card shrink to its share (its own
                    // clip hides anything that will not fit), and the maximum
                    // is that share, so the row can never be wider than the
                    // page.
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: (grid.width - list.gap * (list.cols - 1))
                                         / list.cols
                    ink: list.ink
                    faint: list.faint
                    muted: list.muted
                    accent: list.accent
                    logoSource: list.logoSource
                    logoRev: list.logoRev
                    u: list.u
                    game: modelData
                    onPicked: list.gamePicked(modelData)
                }
            }
        }

            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: -10 * list.u
                width: 5 * list.u
                radius: width / 2
                color: list.faint
                visible: flick.contentHeight > flick.height
                height: Math.max(40 * list.u,
                                 flick.height * flick.height / Math.max(1, flick.contentHeight))
                y: flick.contentHeight > flick.height
                   ? (flick.contentY / (flick.contentHeight - flick.height))
                     * (flick.height - height)
                   : 0
            }
        }
    }
}
