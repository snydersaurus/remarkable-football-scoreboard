import QtQuick
import QtQuick.Layouts

// The list page: both followed seasons side by side, a column each. College
// football is weekly, so the useful list is not "today's games" -- most days
// there are none -- but the season, with the game that is on or next in it.
Item {
    id: slate

    property color ink
    property color faint
    property color muted
    property color accent
    // [{ teamId, name, abbr, record, standing, rank, current, games: [...] }]
    property var teams: []
    property string seasonLabel: ""
    property bool loaded: false
    property bool headerVisible: true
    property var logoSource: function (id) { return "" }
    property int logoRev: 0
    property real u: 1.0

    signal gamePicked(var row)
    signal dismissed()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Text {
            visible: slate.headerVisible
            text: slate.loaded
                  ? (slate.seasonLabel.length > 0 ? slate.seasonLabel : "SEASON")
                  : "LOADING…"
            color: slate.muted
            font.pixelSize: 40 * slate.u
            font.letterSpacing: 5 * slate.u
            font.bold: true
        }

        Item { Layout.preferredHeight: 28 * slate.u }

        // An NFL season is eighteen games plus a bye, and in landscape that is
        // taller than the page: the rows simply ran on underneath the menu bar
        // at the bottom. Scroll instead, and keep the header and the menu put.
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: columns.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            // No kinetic overshoot: on e-ink every frame of a flick is a full
            // panel repaint, so a drag that keeps coasting is unpleasant.
            flickDeceleration: 6000
            maximumFlickVelocity: 1400

        RowLayout {
            id: columns
            width: flick.width
            spacing: 48 * slate.u

            Repeater {
                model: slate.teams

                ColumnLayout {
                    id: column
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1        // divide the width evenly
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: (columns.width - 48 * slate.u
                                          * (slate.teams.length - 1))
                                         / Math.max(1, slate.teams.length)
                    Layout.alignment: Qt.AlignTop
                    spacing: 0

                    // ---- team header ----
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16 * slate.u

                        TeamLogo {
                            Layout.preferredWidth: 72 * slate.u
                            Layout.preferredHeight: 72 * slate.u
                            path: slate.logoSource(column.modelData.teamId)
                            rev: slate.logoRev
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: (column.modelData.rank > 0
                                       ? "#" + column.modelData.rank + " " : "")
                                      + (column.modelData.name !== undefined
                                         ? column.modelData.name.toUpperCase() : "")
                                color: slate.ink
                                font.pixelSize: 44 * slate.u
                                font.bold: true
                                font.letterSpacing: 2
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Text {
                                text: {
                                    var bits = []
                                    if (column.modelData.record) bits.push(column.modelData.record)
                                    if (column.modelData.standing) bits.push(column.modelData.standing)
                                    return bits.join("   ·   ")
                                }
                                visible: text.length > 0
                                color: slate.muted
                                font.pixelSize: 28 * slate.u
                                font.weight: Font.Bold
                                font.letterSpacing: 1
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Item { Layout.preferredHeight: 14 * slate.u }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 5
                        color: slate.ink
                    }

                    Item { Layout.preferredHeight: 8 * slate.u }

                    Repeater {
                        model: column.modelData.games

                        ScheduleRow {
                            required property var modelData
                            Layout.fillWidth: true
                            ink: slate.ink
                            faint: slate.faint
                            muted: slate.muted
                            accent: slate.accent
                            logoSource: slate.logoSource
                            logoRev: slate.logoRev
                            u: slate.u
                            game: modelData
                            current: modelData.eventId !== undefined
                                     && modelData.eventId === column.modelData.current
                            onPicked: slate.gamePicked(modelData)
                        }
                    }

                }
            }
        }

            // Only when there is more than fits: a bar that is always there is
            // chrome, and a scrollable page with no sign of it is a trap.
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: -10 * slate.u
                width: 5 * slate.u
                radius: width / 2
                color: slate.faint
                visible: flick.contentHeight > flick.height
                height: Math.max(40 * slate.u,
                                 flick.height * flick.height / Math.max(1, flick.contentHeight))
                y: flick.contentHeight > flick.height
                   ? (flick.contentY / (flick.contentHeight - flick.height))
                     * (flick.height - height)
                   : 0
            }
        }
    }
}
