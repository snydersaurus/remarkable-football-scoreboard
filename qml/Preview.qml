import QtQuick

// Desktop preview and the fullscreen build. Both draw the same Board.qml the
// AppLoad frontend does; all this shell adds is the window, the panel geometry
// and the rotation, none of which exist inside xochitl.
Window {
    id: win

    // --- orientation -------------------------------------------------------
    // Two panels exist: Paper Pro (ferrari) is 1620x2160, Paper Pro Move
    // (chiappa) is 954x1696. Rather than pin the canvas to one of them, keep
    // 2160 as the long side -- every type size in Board.qml is tuned against
    // it -- and derive the short side from the panel's own aspect.
    //
    // panelRotation is how the content is drawn on the panel: 0 portrait,
    // 90 landscape, 180 portrait inverted, 270 landscape inverted. With
    // --orient auto it follows the accelerometer; otherwise it is fixed at
    // startup by --orient / --flip. Tent mode is simply 180 or 270.
    readonly property int panelRotation:
        (typeof autoRotate !== "undefined" && autoRotate)
            ? sensor.rotation
            : ((typeof fixedRotation !== "undefined") ? fixedRotation : 0)

    readonly property bool landscape:
        panelRotation === 90 || panelRotation === 270

    readonly property bool onPanel: Qt.platform.pluginName === "epaper"
    readonly property bool deviceMode:
        (typeof shotMode !== "undefined" && shotMode) || onPanel

    // Fullscreen on the device reports the real panel; --shot runs offscreen
    // and has to be told with --panel. Fall back to the Paper Pro.
    readonly property real panelW: {
        if (typeof panelWOverride !== "undefined" && panelWOverride > 0)
            return panelWOverride
        if (onPanel && Screen.width > 0)
            return Screen.width
        return 1620
    }
    readonly property real panelH: {
        if (typeof panelHOverride !== "undefined" && panelHOverride > 0)
            return panelHOverride
        if (onPanel && Screen.height > 0)
            return Screen.height
        return 2160
    }

    readonly property real longSide: 2160
    readonly property real shortSide: Math.round(longSide * panelW / panelH)
    readonly property real boardW: landscape ? longSide : shortSide
    readonly property real boardH: landscape ? shortSide : longSide

    width:  deviceMode ? (onPanel ? panelW : boardW) : Math.round(boardW / 2.8)
    height: deviceMode ? (onPanel ? panelH : boardH) : Math.round(boardH / 2.8)
    visible: true
    visibility: onPanel ? Window.FullScreen : Window.Windowed
    color: "#FFFFFF"
    title: "College Football"

    // Only the real panel is physically rotated. Offscreen renders (--shot) and
    // the desktop preview draw the board the right way up at its own size.
    Item {
        id: rotator
        anchors.centerIn: parent
        width: win.boardW
        height: win.boardH
        rotation: win.onPanel ? win.panelRotation : 0
        transformOrigin: Item.Center
        // boardW/H share the panel's aspect, so one ratio scales both edges.
        scale: Math.max(win.width, win.height) / Math.max(width, height)

        Board {
            anchors.fill: parent
            s: feed.state
            startPage: (typeof startPage !== "undefined") ? startPage : 0
            mutedHex: (typeof mutedOverride !== "undefined") ? mutedOverride : ""
            faintHex: (typeof faintOverride !== "undefined") ? faintOverride : ""

            // logoFor() returns "" until the file is cached; logoRev is what
            // makes the images that asked too early try again.
            logoFor: function (id) {
                if (typeof logos === "undefined" || !logos)
                    return ""
                var _ = logos.revision
                return logos.logoFor(id)
            }
            logoRev: (typeof logos !== "undefined" && logos) ? logos.revision : 0

            onGameSelected: function (eventId, row) {
                feed.showGame(eventId, row)
            }
            onTeamRequested: feed.showTeamGame()
            onLiveWatched: function (on) { feed.watchLive(on) }
            onTeamsWanted: feed.loadTeamIndex()
            onTeamsChosen: function (ids) { feed.setTeams(ids) }
        }
    }
}
