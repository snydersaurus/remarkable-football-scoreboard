import QtQuick
import QtQuick.Layouts

// Choose the two teams whose seasons the schedule shows.
//
// Two, because that is what the list page is: two columns of a season side by
// side. Tap a slot to arm it, then tap a team to fill it.
//
// College football has 762 teams, which is far too many for a grid, so the
// list is alphabetical with a letter index across the top. The NFL has 32 and
// simply shows all of them.
Item {
    id: picker

    property color ink
    property color faint
    property color muted
    property color accent
    // [{ id, abbr, name }] for the whole league.
    property var teams: []
    // The two currently followed, in order.
    property var followed: []
    property var logoSource: function (id) { return "" }
    property int logoRev: 0
    property bool loaded: false
    property real u: 1.0

    signal teamsChosen(var ids)

    // Which slot the next tap fills.
    property int armed: 0

    // The NFL is short enough to show whole; college needs a letter first.
    readonly property bool needsIndex: teams.length > 60
    property string letter: ""

    readonly property var letters: {
        var seen = {}
        var out = []
        for (var i = 0; i < teams.length; ++i) {
            var c = (teams[i].name || "?").charAt(0).toUpperCase()
            if (!seen[c]) { seen[c] = true; out.push(c) }
        }
        out.sort()
        return out
    }

    readonly property var shown: {
        if (!needsIndex)
            return teams
        if (letter.length === 0)
            return []
        var out = []
        for (var i = 0; i < teams.length; ++i)
            if ((teams[i].name || "?").charAt(0).toUpperCase() === letter)
                out.push(teams[i])
        return out
    }

    function nameFor(id) {
        for (var i = 0; i < teams.length; ++i)
            if (teams[i].id === id)
                return teams[i].name
        return id > 0 ? "—" : "EMPTY"
    }

    function choose(id) {
        var ids = [ followed.length > 0 ? followed[0] : 0,
                    followed.length > 1 ? followed[1] : 0 ]
        ids[picker.armed] = id
        // The same team twice would draw the same season in both columns.
        if (ids[0] === ids[1])
            ids[1 - picker.armed] = 0
        var out = []
        for (var i = 0; i < ids.length; ++i)
            if (ids[i] > 0)
                out.push(ids[i])
        picker.teamsChosen(out)
        // Move to the other slot, so picking two in a row just works.
        picker.armed = picker.armed === 0 ? 1 : 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Text {
            text: "FOLLOWING"
            color: picker.muted
            font.pixelSize: 34 * picker.u
            font.bold: true
            font.letterSpacing: 5 * picker.u
        }

        Item { Layout.preferredHeight: 6 * picker.u }

        Text {
            Layout.fillWidth: true
            text: "The schedule changes straight away. The launcher icon "
                  + "becomes the first team's mark, the next time your tablet "
                  + "restarts. Until you pick one it stays neutral."
            // muted, not faint: faint is for rules and outlines, and secondary
            // text set in it is close to invisible on Gallery 3.
            color: picker.muted
            font.pixelSize: 26 * picker.u
            wrapMode: Text.WordWrap
        }

        Item { Layout.preferredHeight: 18 * picker.u }

        // ---- the two slots ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 20 * picker.u

            Repeater {
                model: 2

                Rectangle {
                    id: slot
                    required property int index
                    readonly property int teamId:
                        picker.followed.length > index ? picker.followed[index] : 0

                    Layout.fillWidth: true
                    Layout.preferredHeight: 108 * picker.u
                    radius: 12 * picker.u
                    color: "transparent"
                    // The armed slot is the one the next tap fills, so it is
                    // the heavier outline.
                    border.width: index === picker.armed
                                  ? Math.max(3, 5 * picker.u)
                                  : Math.max(1, 2 * picker.u)
                    border.color: index === picker.armed ? picker.ink : picker.faint

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16 * picker.u
                        spacing: 14 * picker.u

                        TeamLogo {
                            Layout.preferredWidth: 62 * picker.u
                            Layout.preferredHeight: 62 * picker.u
                            path: picker.logoSource(slot.teamId)
                            rev: picker.logoRev
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Text {
                                text: slot.index === 0 ? "FIRST" : "SECOND"
                                color: picker.muted
                                font.pixelSize: 22 * picker.u
                                font.bold: true
                                font.letterSpacing: 3 * picker.u
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: picker.nameFor(slot.teamId)
                                color: picker.ink
                                font.pixelSize: 40 * picker.u
                                font.bold: true
                                elide: Text.ElideRight
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: picker.armed = slot.index
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 18 * picker.u }

        // ---- the letter index ----
        Flow {
            Layout.fillWidth: true
            visible: picker.needsIndex && picker.loaded
            spacing: 6 * picker.u

            Repeater {
                model: picker.letters

                Rectangle {
                    required property var modelData
                    width: 58 * picker.u
                    height: 58 * picker.u
                    radius: 8 * picker.u
                    color: "transparent"
                    border.width: modelData === picker.letter
                                  ? Math.max(3, 4 * picker.u)
                                  : Math.max(1, 2 * picker.u)
                    border.color: modelData === picker.letter ? picker.ink : picker.faint

                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        color: picker.ink
                        font.pixelSize: 30 * picker.u
                        font.bold: modelData === picker.letter
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: picker.letter = modelData
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 16 * picker.u; visible: picker.needsIndex }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 3; color: picker.faint }

        Item { Layout.preferredHeight: 12 * picker.u }

        // ---- the teams ----
        Text {
            Layout.fillWidth: true
            visible: picker.shown.length === 0
            text: !picker.loaded ? "LOADING THE LEAGUE…"
                 : picker.needsIndex && picker.letter.length === 0
                   ? "Pick a letter to see teams."
                   : "No teams."
            color: picker.muted
            font.pixelSize: 30 * picker.u
            font.bold: true
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: grid.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            // On e-ink every frame of a coasting flick is a full repaint.
            flickDeceleration: 6000
            maximumFlickVelocity: 1400

            GridLayout {
                id: grid
                width: flick.width
                columns: picker.width > picker.height ? 4 : 3
                columnSpacing: 16 * picker.u
                rowSpacing: 14 * picker.u

                Repeater {
                    model: picker.shown

                    Rectangle {
                        required property var modelData
                        readonly property bool isFollowed:
                            picker.followed.indexOf(modelData.id) >= 0

                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.minimumWidth: 0
                        Layout.preferredHeight: 84 * picker.u
                        radius: 10 * picker.u
                        color: "transparent"
                        border.width: isFollowed ? Math.max(3, 4 * picker.u)
                                                 : Math.max(1, 2 * picker.u)
                        border.color: isFollowed ? picker.ink : picker.faint

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12 * picker.u
                            spacing: 10 * picker.u

                            TeamLogo {
                                Layout.preferredWidth: 46 * picker.u
                                Layout.preferredHeight: 46 * picker.u
                                path: picker.logoSource(modelData.id)
                                rev: picker.logoRev
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: modelData.name
                                color: picker.ink
                                font.pixelSize: 30 * picker.u
                                font.bold: isFollowed
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: picker.choose(modelData.id)
                        }
                    }
                }
            }

            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: -10 * picker.u
                width: 5 * picker.u
                radius: width / 2
                color: picker.faint
                visible: flick.contentHeight > flick.height
                height: Math.max(40 * picker.u,
                                 flick.height * flick.height / Math.max(1, flick.contentHeight))
                y: flick.contentHeight > flick.height
                   ? (flick.contentY / (flick.contentHeight - flick.height))
                     * (flick.height - height)
                   : 0
            }
        }
    }
}
