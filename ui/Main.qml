import QtQuick
import net.asivery.AppLoad 1.0

// AppLoad frontend. This runs *inside* xochitl, so there is no panel to own, no
// rotation to apply and no framebuffer involved -- xochitl hands us a window and
// we fill it. Everything the view needs arrives as JSON from the backend, which
// is a separate process precisely so it can reach the network.
//
// The view itself is Board.qml, shared with the desktop preview, so there is
// one copy of the layout rather than two that drift apart.
Item {
    id: root
    anchors.fill: parent

    // AppLoad calls these when the app is being torn down. Unpinning means the
    // next launch opens on the followed team's game rather than whatever was
    // last tapped.
    signal close
    function unloading() {
        appload.sendMessage(root.msgShowTeam, "")
    }

    readonly property int msgGeometry: 4
    readonly property int msgState:    101
    readonly property int msgHello:    1
    readonly property int msgShowGame: 2
    readonly property int msgShowTeam: 3
    readonly property int msgWatchLive: 5
    readonly property int msgSetTeams:  6
    readonly property int msgWantTeams: 7
    readonly property int msgRefresh:   8

    function reportGeometry() {
        appload.sendMessage(root.msgGeometry,
                            Math.round(width) + "x" + Math.round(height))
    }
    onWidthChanged: reportGeometry()
    onHeightChanged: reportGeometry()

    // Ask for fresh data the moment the app is on screen again, rather than
    // waiting up to a poll interval to notice. AppLoad keeps a frontend loaded
    // when you close the window, so Component.onCompleted does not run a
    // second time and the backend would otherwise hear nothing about you
    // coming back.
    onVisibleChanged: {
        if (visible)
            appload.sendMessage(root.msgRefresh, "")
    }

    AppLoad {
        id: appload
        applicationID: "cfb-scoreboard"
        onMessageReceived: (type, contents) => {
            if (type !== root.msgState)
                return
            try {
                board.s = JSON.parse(contents)
            } catch (e) {
                console.log("cfb: bad state payload:", e)
            }
        }
        Component.onCompleted: {
            appload.sendMessage(root.msgHello, "")
            root.reportGeometry()
        }
    }

    Board {
        id: board
        anchors.fill: parent

        // Logos are cached on disk by the backend; load them straight off the
        // filesystem rather than pushing image data across the socket.
        //
        // The directory comes from the backend rather than being written here:
        // each league caches into its own, because team ids collide between
        // them -- 5 is the Browns in one and Marshall in the other. This was
        // hardcoded to the college directory, which meant the NFL build looked
        // for every logo in the wrong place and drew none of them.
        logoFor: function (id) {
            var dir = (board.s && board.s.logoDir) ? board.s.logoDir : ""
            return (id > 0 && dir.length > 0)
                ? "file://" + dir + "/" + id + ".png"
                : ""
        }
        logoRev: (board.s && board.s.logoRev) ? board.s.logoRev : 0

        onGameSelected: function (eventId, row) {
            appload.sendMessage(root.msgShowGame, eventId)
        }
        onTeamRequested: appload.sendMessage(root.msgShowTeam, "")
        onLiveWatched: function (on) {
            appload.sendMessage(root.msgWatchLive, on ? "1" : "0")
        }
        onTeamsWanted: appload.sendMessage(root.msgWantTeams, "")
        onTeamsChosen: function (ids) {
            appload.sendMessage(root.msgSetTeams, ids.join(","))
        }
    }
}
