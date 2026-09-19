import QtQuick
import QtQuick.Layouts

// One game from anywhere in college football: status, both teams with rank and
// score, and while it is being played, who has the ball and the down. Outline
// only -- a filled card would be a big dark area, which e-ink repaints slowly.
Item {
    id: card

    property color ink
    property color faint
    property color muted
    // The loser of a finished game recedes. Separate from `muted` so darkening
    // secondary text does not flatten the win/loss distinction.
    // Dark enough to read on Gallery 3, light enough to still read as beaten.
    // #8A8A8A looked right on a monitor and was close to invisible on paper.
    property color dimmed: "#4A4A4A"
    property color accent
    property var   game: ({})
    property var   logoSource: function (id) { return "" }
    property int   logoRev: 0
    property real  u: 1.0

    signal picked()

    implicitHeight: frame.implicitHeight

    readonly property bool live: game.isLive === true
    readonly property bool over: game.isFinal === true

    function lost(mine, theirs) {
        return card.over && mine !== "" && theirs !== "" && Number(mine) < Number(theirs)
    }

    clip: true

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: 16 * card.u
        color: "transparent"
        border.width: Math.max(1, (card.game.followed === true ? 4 : 2) * card.u)
        border.color: card.faint

        implicitHeight: body.implicitHeight + 2 * (24 * card.u)

        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: 24 * card.u
            spacing: 10 * card.u

            RowLayout {
                Layout.fillWidth: true
                spacing: 12 * card.u

                Text {
                    text: card.game.note !== undefined ? card.game.note : ""
                    color: card.live ? card.accent : card.muted
                    font.pixelSize: 31 * card.u
                    font.bold: true
                    font.letterSpacing: 3 * card.u
                }
                Item { Layout.fillWidth: true }
                // Down and distance rides along the status line: it is the
                // other half of "what is happening right now".
                Text {
                    Layout.minimumWidth: 0
                    visible: card.live && text.length > 0
                    text: card.game.downDistance !== undefined ? card.game.downDistance : ""
                    color: card.muted
                    font.pixelSize: 29 * card.u
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
            }

            Repeater {
                model: [
                    { id: card.game.awayId, abbr: card.game.awayAbbr,
                      rank: card.game.awayRank, score: card.game.awayScore,
                      ball: card.live && card.game.possessionId === card.game.awayId,
                      lost: card.lost(card.game.awayScore, card.game.homeScore) },
                    { id: card.game.homeId, abbr: card.game.homeAbbr,
                      rank: card.game.homeRank, score: card.game.homeScore,
                      ball: card.live && card.game.possessionId === card.game.homeId,
                      lost: card.lost(card.game.homeScore, card.game.awayScore) }
                ]

                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 10 * card.u

                    // Possession, as the same slim accent bar the board uses.
                    Rectangle {
                        Layout.preferredWidth: 7 * card.u
                        Layout.preferredHeight: 46 * card.u
                        color: modelData.ball ? card.accent : "transparent"
                    }

                    TeamLogo {
                        Layout.preferredWidth: 52 * card.u
                        Layout.preferredHeight: 52 * card.u
                        opacity: modelData.lost ? 0.7 : 1.0
                        path: card.logoSource(modelData.id)
                        rev: card.logoRev
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: (modelData.rank > 0 ? "#" + modelData.rank + " " : "")
                              + (modelData.abbr !== undefined ? modelData.abbr : "")
                        color: modelData.lost ? card.dimmed : card.ink
                        font.pixelSize: 48 * card.u
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        text: (modelData.score === undefined || modelData.score === "")
                              ? "–" : modelData.score
                        color: modelData.lost ? card.dimmed : card.ink
                        font.pixelSize: 48 * card.u
                        font.bold: true
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 74 * card.u
                    }
                }
            }
        }
    }

    TapHandler { onTapped: card.picked() }
}
