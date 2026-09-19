import QtQuick
import QtQuick.Layouts

// The whole view: the game on page 0, both seasons on page 1. Owned by neither
// shell -- the AppLoad frontend and the desktop/fullscreen preview both host
// this same item, so there is one copy of the layout rather than two that drift
// apart.
//
// Everything arrives in `s`, a flat map. There is no model plumbing: adding a
// field is one line in GameFeed.cpp and one binding here.
Item {
    id: root

    // Whole application state.
    property var s: ({})
    // id -> a file:// url for that team's cached logo, or "".
    property var logoFor: function (id) { return "" }
    // Bumped whenever another logo finishes downloading, so the images that
    // were asked for too early can try again.
    property int logoRev: 0
    // E-ink contrast is hard to judge without the panel in front of you, so the
    // two secondary greys are overridable at runtime.
    property string mutedHex: ""
    property string faintHex: ""
    property int startPage: 0

    signal gameSelected(string eventId, var row)
    signal teamRequested()
    // Asks the backend to start or stop fetching the league-wide list. It is
    // expensive, so it runs only while that page is on screen.
    signal liveWatched(bool on)
    // The picker opened -- ask the backend for the league's team list.
    signal teamsWanted()
    signal teamsChosen(var ids)

    function v(key, dflt) {
        return (s && s[key] !== undefined && s[key] !== null && s[key] !== "")
               ? s[key] : dflt
    }

    // --- geometry ----------------------------------------------------------
    // Lay out against a fixed design canvas so every tuned size still holds,
    // then scale that canvas into whatever box the shell gives us.
    readonly property bool landscape: width > height

    readonly property real designH: 2160
    readonly property real designW:
        Math.round(designH * Math.min(width, height) / Math.max(width, height))

    readonly property real canvasW: landscape ? designH : designW
    readonly property real canvasH: landscape ? designW : designH

    // --- palette -----------------------------------------------------------
    // Gallery 3 renders colour, but muted. Keep the page white, the type black,
    // and spend colour only where it carries meaning -- here, possession and
    // the word LIVE.
    readonly property color paper:  "#FFFFFF"
    readonly property color ink:    "#000000"
    // Two greys, because e-ink needs them further apart than a screen does.
    // faint draws rules, borders and empty pips; muted is for secondary text.
    readonly property color faint: faintHex.length > 0 ? ("#" + faintHex) : "#5A5A5A"
    readonly property color muted: mutedHex.length > 0 ? ("#" + mutedHex) : "#000000"
    readonly property color accent: "#A4123F"

    // The landscape sizes below were tuned against a 1620-tall canvas (Paper
    // Pro). On the Move that canvas is only 1215 tall, so the vertical-heavy
    // elements have to give way or the line score and footer fall off.
    readonly property real vScale: landscape ? Math.min(1, canvasH / 1620) : 1

    readonly property real pageMargin: landscape ? Math.round(64 * vScale) : 70
    readonly property real gutter: 110
    readonly property real contentW: canvasW - pageMargin * 2
    readonly property real colW: landscape ? (contentW - gutter) / 2 : contentW

    // --- type scale --------------------------------------------------------
    readonly property int scoreSize: Math.round((landscape ? 215 : 190) * vScale)
    readonly property int nameSize:  Math.round((landscape ?  84 :  74) * vScale)
    readonly property int nameSize2: Math.round((landscape ?  66 :  56) * vScale)
    readonly property int bodySize:  Math.round((landscape ?  44 :  38) * vScale)
    readonly property int gridSize:  Math.round((landscape ?  37 :  34) * vScale)

    // 0 = the game, 1 = both seasons, 2 = everything live in the league. The
    // top strip swaps between them.
    property int page: startPage
    onPageChanged: {
        root.liveWatched(page === 2)
        if (page === 3)
            root.teamsWanted()
    }

    // The three pages, named in the bar along the bottom. An earlier version
    // hid these behind a tap on the top strip, on the theory that a board you
    // only glance at should carry no chrome -- and then nobody found them. An
    // invisible control is not a control; one thin rule and three words is a
    // price worth paying.
    readonly property var pages: [
        { label: "GAME",     target: 0 },
        { label: "SCHEDULE", target: 1 },
        { label: "LIVE NOW", target: 2 },
        { label: "TEAMS",    target: 3 }
    ]

    // Height reserved at the foot of every page for that bar.
    readonly property real tabBarH: Math.round(96 * vScale)

    // Where to open, decided once from the first state that arrives.
    //
    // Nobody has picked teams yet -> the picker, because the built-in pair is
    // a starting point rather than a choice, and an app that opens onto
    // somebody else's teams never tells you that you can change them.
    // Otherwise, with nothing being played -- the offseason -- open on the
    // list, so the first thing on screen is something rather than an empty
    // board. After that the page is whatever the reader last tapped.
    property bool pageChosen: false
    onSChanged: {
        if (pageChosen || !v("loaded", false))
            return
        pageChosen = true
        if (!v("teamsPicked", true))
            page = 3
        else if (!v("hasGame", false) && root.v("teams", []).length > 0)
            page = 1
    }

    readonly property bool isLive:  v("abstractState", "") === "Live"
    readonly property bool isFinal: v("abstractState", "") === "Final"
    readonly property bool isPre:   !isLive && !isFinal

    // "2ND  7:12", or just the word when the clock is not running.
    readonly property string clockLine: {
        var p = v("periodLabel", "")
        var c = v("clock", "")
        if (p === "HALFTIME" || c.length === 0)
            return p
        return p.length > 0 ? p + "   " + c : c
    }

    // Leaders are on screen whenever there are any -- during play, at half and
    // after the final. An earlier version showed them only when the game was
    // stopped, on the theory that the last play deserved the space while play
    // was live; who is throwing and running the ball is worth more than that.
    readonly property bool showLeaders: v("leaders", []).length > 0

    readonly property int possessionId: v("possessionId", 0)

    Item {
        id: stage
        anchors.fill: parent

        Item {
            id: canvas
            width: root.canvasW
            height: root.canvasH
            anchors.centerIn: parent
            transformOrigin: Item.Center
            scale: Math.min(stage.width / width, stage.height / height)

            ColumnLayout {
                visible: root.page === 0
                anchors.fill: parent
                anchors.margins: root.pageMargin
                anchors.topMargin: Math.round(56 * root.vScale)
                anchors.bottomMargin: root.pageMargin + root.tabBarH
                spacing: 0

                // ------------------------------------------------------- top
                StatusStrip {
                    Layout.fillWidth: true
                    ink: root.ink
                    faint: root.faint
                    muted: root.muted
                    accent: root.accent
                    u: root.vScale
                    leftText: !v("loaded", false) ? ""
                            : root.isLive ? root.clockLine
                            : root.isFinal ? v("note", "")
                            : v("hasGame", false) ? v("seasonLabel", "") : ""
                    rightText: !v("loaded", false) ? ""
                             : root.isLive ? "LIVE"
                             : root.isFinal ? v("finalLabel", "FINAL")
                             : v("hasGame", false) ? "SCHEDULED" : ""
                    accentRight: root.isLive
                }

                Item { Layout.preferredHeight: root.landscape ? Math.round(40 * root.vScale) : 50 }

                // ------------------------------------------------------- body
                // Two columns side by side in landscape, stacked in portrait.
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.landscape ? 2 : 1
                    columnSpacing: root.gutter
                    rowSpacing: 0

                    // ===== left / upper: score, then where the ball is =====
                    ColumnLayout {
                        Layout.preferredWidth: root.colW
                        Layout.maximumWidth: root.colW
                        Layout.alignment: Qt.AlignTop
                        spacing: 0

                        Repeater {
                            model: [
                                { name: v("awayName", "Away"),
                                  score: v("awayScore", 0),
                                  teamId: v("awayId", 0),
                                  rank: v("awayRank", 0),
                                  rec: v("awayRecord", ""),
                                  place: v("awayStanding", ""),
                                  hasBall: root.isLive && root.possessionId > 0
                                           && root.possessionId === v("awayId", -1) },
                                { name: v("homeName", "Home"),
                                  score: v("homeScore", 0),
                                  teamId: v("homeId", 0),
                                  rank: v("homeRank", 0),
                                  rec: v("homeRecord", ""),
                                  place: v("homeStanding", ""),
                                  hasBall: root.isLive && root.possessionId > 0
                                           && root.possessionId === v("homeId", -1) }
                            ]

                            RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                visible: v("hasGame", false)
                                spacing: 22

                                // Possession marker. Black, not accent: the
                                // accent is a colour, and Gallery 3 renders
                                // colour as a muted grey, so at 14 units wide
                                // it could not be seen at all. Still a slim
                                // bar rather than a filled row -- big dark
                                // areas repaint slowly on e-ink.
                                Rectangle {
                                    Layout.preferredWidth: 22
                                    Layout.preferredHeight: root.scoreSize * 0.62
                                    color: modelData.hasBall ? root.ink : "transparent"
                                }

                                TeamLogo {
                                    Layout.preferredWidth: root.nameSize * 1.15
                                    Layout.preferredHeight: root.nameSize * 1.15
                                    path: root.logoFor(modelData.teamId)
                                    rev: root.logoRev
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    // The rank is part of the name in college
                                    // football -- "#6 Ohio State" is how the
                                    // team is referred to all week.
                                    Text {
                                        text: (modelData.rank > 0 ? "#" + modelData.rank + " " : "")
                                              + modelData.name
                                        color: root.ink
                                        font.pixelSize: root.nameSize
                                        font.weight: modelData.hasBall ? Font.Bold : Font.Normal
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: {
                                            var bits = []
                                            if (modelData.rec) bits.push(modelData.rec)
                                            if (modelData.place) bits.push(modelData.place)
                                            return bits.join("   ")
                                        }
                                        visible: text.length > 0
                                        color: root.muted
                                        font.pixelSize: Math.round(root.nameSize * 0.46)
                                        font.weight: Font.Bold
                                        font.letterSpacing: 1
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                }

                                Text {
                                    text: modelData.score
                                    color: root.ink
                                    font.pixelSize: root.scoreSize
                                    font.weight: Font.Bold
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: root.scoreSize * 1.30
                                }
                            }
                        }

                        Item {
                            Layout.preferredHeight: Math.round(26 * root.vScale)
                            visible: v("hasGame", false)
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 6
                            color: root.ink
                            visible: v("hasGame", false)
                        }

                        Item {
                            Layout.preferredHeight: root.landscape ? Math.round(70 * root.vScale) : 56
                            visible: root.isLive
                        }

                        // ---- down, distance, and the field ----------------
                        RowLayout {
                            Layout.fillWidth: true
                            visible: root.isLive && root.v("downDistance", "").length > 0
                            spacing: 24

                            Text {
                                text: root.v("downDistance", "")
                                color: root.ink
                                font.pixelSize: Math.round(root.nameSize2 * 1.15)
                                font.weight: Font.Bold
                                font.letterSpacing: 2
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: root.v("fieldPos", "")
                                color: root.muted
                                font.pixelSize: root.nameSize2
                                font.weight: Font.Bold
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
                            }
                        }

                        Item {
                            Layout.preferredHeight: Math.round(28 * root.vScale)
                            visible: root.isLive
                        }

                        FieldStrip {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.round(130 * root.vScale)
                            visible: root.isLive && root.v("ballOn", -1) >= 0
                            ink: root.ink
                            faint: root.faint
                            accent: root.accent
                            u: root.vScale
                            ballOn: root.v("ballOn", -1)
                            toGo: root.v("toGo", 0)
                            redZone: root.v("isRedZone", false)
                            // The left end of the strip is always the goal line
                            // the team with the ball is defending.
                            ownAbbr: root.possessionId === v("homeId", -1)
                                     ? v("homeAbbr", "") : v("awayAbbr", "")
                            oppAbbr: root.possessionId === v("homeId", -1)
                                     ? v("awayAbbr", "") : v("homeAbbr", "")
                        }

                        Item {
                            Layout.preferredHeight: Math.round(34 * root.vScale)
                            visible: root.isLive
                        }

                        ColumnLayout {
                            visible: root.isLive && root.v("homeTimeouts", -1) >= 0
                            spacing: Math.round(18 * root.vScale)

                            Repeater {
                                model: [
                                    { l: v("awayAbbr", ""), n: v("awayTimeouts", 0) },
                                    { l: v("homeAbbr", ""), n: v("homeTimeouts", 0) }
                                ]
                                TimeoutRow {
                                    required property var modelData
                                    label: modelData.l + " TIMEOUTS"
                                    value: modelData.n
                                    total: 3
                                    ink: root.ink
                                    faint: root.faint
                                    labelSize: Math.round(30 * root.vScale)
                                    pip: Math.round(34 * root.vScale)
                                    labelWidth: Math.round(330 * root.vScale)
                                    gap: Math.round(16 * root.vScale)
                                }
                            }
                        }
                    }

                    // ===== right / lower: the last play, or the matchup =====
                    ColumnLayout {
                        Layout.preferredWidth: root.colW
                        Layout.maximumWidth: root.colW
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: root.landscape ? 0 : 60
                        spacing: 0

                        // ---- before kickoff: what there is to know ----
                        GridLayout {
                            Layout.fillWidth: true
                            visible: root.isPre && v("hasGame", false)
                            columns: 2
                            columnSpacing: 32
                            rowSpacing: 14

                            Repeater {
                                model: [
                                    { l: "KICKOFF", t: v("statusText", "") },
                                    { l: "TV",      t: v("broadcast", "") },
                                    { l: "LINE",    t: v("oddsLine", "") },
                                    { l: "AT",      t: v("venue", "") }
                                ]

                                RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.columnSpan: 2
                                    visible: modelData.t.length > 0
                                    spacing: 32

                                    Text {
                                        text: modelData.l
                                        color: root.muted
                                        font.pixelSize: Math.round(32 * root.vScale)
                                        font.bold: true
                                        font.letterSpacing: 4
                                        Layout.preferredWidth: Math.round(230 * root.vScale)
                                    }
                                    Text {
                                        text: modelData.t
                                        color: root.ink
                                        font.pixelSize: Math.round(root.nameSize2 * 0.78)
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        // ---- the play that just happened ----
                        Text {
                            Layout.fillWidth: true
                            visible: root.isLive && text.length > 0
                            text: root.v("lastPlayTag", "")
                            color: root.ink
                            font.pixelSize: Math.round(root.bodySize * 0.82)
                            font.bold: true
                            font.letterSpacing: 2
                            elide: Text.ElideRight
                        }

                        Item {
                            Layout.preferredHeight: Math.round(22 * root.vScale)
                            visible: root.isLive
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 3
                            color: root.faint
                            visible: root.isLive
                        }

                        Item {
                            Layout.preferredHeight: Math.round(32 * root.vScale)
                            visible: root.isLive
                        }

                        // While play is live this is the last play; once it is
                        // over the last play is "End of game", which says
                        // nothing, so the slot carries ESPN's recap sentence.
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0 && (root.isLive || root.isFinal)
                            text: root.isFinal ? v("recap", "") : v("lastPlay", "")
                            color: root.ink
                            font.pixelSize: root.bodySize
                            font.italic: true
                            lineHeight: 1.3
                            wrapMode: Text.WordWrap
                            // Two lines while the leaders are below it, four
                            // when they are not: on the Move's short landscape
                            // canvas both at full height do not fit.
                            maximumLineCount: root.showLeaders ? 2 : 4
                            elide: Text.ElideRight
                        }

                        // ---- passing, rushing, receiving ----
                        Item {
                            Layout.preferredHeight: Math.round(40 * root.vScale)
                            visible: root.showLeaders
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: root.showLeaders
                            spacing: Math.round(12 * root.vScale)

                            Repeater {
                                model: root.v("leaders", [])

                                RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: Math.round(16 * root.vScale)

                                    Text {
                                        text: modelData.label
                                        color: root.muted
                                        font.pixelSize: Math.round(root.bodySize * 0.8)
                                        font.bold: true
                                        font.letterSpacing: 2
                                        Layout.preferredWidth: Math.round(120 * root.vScale)
                                    }
                                    Text {
                                        text: modelData.name
                                        color: root.ink
                                        font.pixelSize: root.bodySize
                                        font.bold: true
                                        Layout.preferredWidth: Math.round(300 * root.vScale)
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: modelData.line
                                        color: root.muted
                                        font.pixelSize: Math.round(root.bodySize * 0.85)
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: !v("hasGame", false)
                            text: !v("loaded", false) ? "Loading…" : "No game scheduled"
                            color: root.muted
                            font.pixelSize: root.landscape ? 42 : 48
                            lineHeight: 1.3
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Whitespace lives here, between the body and the line score,
                // so neither can collide with the other.
                Item { Layout.fillHeight: true; Layout.minimumHeight: Math.round(50 * root.vScale) }

                // --------------------------------------- line score (full width)
                LineScore {
                    Layout.fillWidth: true
                    visible: v("hasGame", false) && (root.isLive || root.isFinal)
                    ink: root.ink
                    faint: root.faint
                    muted: root.muted
                    fontSize: root.gridSize
                    teamColWidth: 190
                    periods: v("periods", [])
                    awayAbbr: v("awayAbbr", "")
                    homeAbbr: v("homeAbbr", "")
                    awayTotal: v("awayScore", 0)
                    homeTotal: v("homeScore", 0)
                }

                Item { Layout.preferredHeight: Math.round(44 * root.vScale) }

                // ---------------------------------------------------- footer
                StatusStrip {
                    Layout.fillWidth: true
                    ink: root.muted
                    faint: root.faint
                    muted: root.muted
                    u: root.vScale
                    leftText: v("venue", "")
                    rightText: v("error", "") !== ""
                               ? "OFFLINE"
                               : ("UPDATED " + v("updatedAt", "—"))
                }
            }

            // ------------------------------------------------- page 1
            Slate {
                visible: root.page === 1
                anchors.fill: parent
                anchors.margins: root.pageMargin
                anchors.topMargin: Math.round(56 * root.vScale)
                anchors.bottomMargin: root.pageMargin + root.tabBarH
                ink: root.ink
                faint: root.faint
                muted: root.muted
                accent: root.accent
                teams: root.v("teams", [])
                seasonLabel: root.v("seasonLabel", "")
                loaded: root.v("loaded", false)
                u: root.vScale
                logoSource: root.logoFor
                logoRev: root.logoRev
                onGamePicked: function (row) {
                    root.gameSelected(String(row.eventId), row)
                    root.page = 0
                }
                onDismissed: root.page = 0
            }

            // ------------------------------------------------- page 2
            LiveList {
                visible: root.page === 2
                anchors.fill: parent
                anchors.margins: root.pageMargin
                anchors.topMargin: Math.round(56 * root.vScale)
                anchors.bottomMargin: root.pageMargin + root.tabBarH
                ink: root.ink
                faint: root.faint
                muted: root.muted
                accent: root.accent
                games: root.v("live", [])
                liveCount: root.v("liveCount", 0)
                dayCount: root.v("dayCount", 0)
                dateLabel: root.v("liveDate", "")
                loaded: root.v("liveLoaded", false)
                u: root.vScale
                logoSource: root.logoFor
                logoRev: root.logoRev
                onGamePicked: function (row) {
                    root.gameSelected(String(row.eventId), row)
                    root.page = 0
                }
                onDismissed: root.page = 0
            }

            // ------------------------------------------------- page 3
            TeamPicker {
                visible: root.page === 3
                anchors.fill: parent
                anchors.margins: root.pageMargin
                anchors.topMargin: Math.round(56 * root.vScale)
                anchors.bottomMargin: root.pageMargin + root.tabBarH
                ink: root.ink
                faint: root.faint
                muted: root.muted
                accent: root.accent
                u: root.vScale
                logoSource: root.logoFor
                logoRev: root.logoRev
                teams: root.v("allTeams", [])
                followed: root.v("followed", [])
                loaded: root.v("allTeams", []).length > 0
                onTeamsChosen: function (ids) { root.teamsChosen(ids) }
            }

            // ------------------------------------------------- the tab bar
            Item {
                id: tabBar
                z: 30
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: root.pageMargin
                anchors.rightMargin: root.pageMargin
                anchors.bottomMargin: Math.round(24 * root.vScale)
                height: root.tabBarH

                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 3
                    color: root.faint
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Math.round(64 * root.vScale)

                    Repeater {
                        model: root.pages

                        Item {
                            required property var modelData
                            width: label.width + Math.round(60 * root.vScale)
                            height: root.tabBarH

                            Text {
                                id: label
                                anchors.centerIn: parent
                                text: modelData.label
                                // The page you are on is the heavy one. A
                                // marker under it would be more chrome for no
                                // more meaning.
                                color: root.ink
                                font.pixelSize: Math.round(34 * root.vScale)
                                font.bold: modelData.target === root.page
                                font.letterSpacing: 4
                                opacity: 1.0
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.page = modelData.target
                            }
                        }
                    }
                }
            }
        }
    }
}
