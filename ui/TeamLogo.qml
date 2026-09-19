import QtQuick

// A team's logo, loaded from the cache directory the backend writes.
//
// Two things make this more than a plain Image.
//
// The file often does not exist yet. The backend fetches and greyscales logos
// in the background, so the first render of a board asks for images that are
// still downloading. QML caches the failure, and because the URL never changes
// afterwards nothing invalidates it -- the space stays empty for as long as the
// app is open, which is exactly how the logos went missing next to the team
// names. `rev` is bumped by the backend each time another logo lands; clearing
// source and setting it again is what actually makes the Image try once more.
//
// And it hides itself until it has something to draw, so a missing logo costs
// no gap beside the name.
Image {
    id: logo

    property string path: ""
    property int rev: 0

    source: path
    fillMode: Image.PreserveAspectFit
    smooth: true
    visible: status === Image.Ready

    onRevChanged: {
        if (status !== Image.Ready && path.length > 0) {
            source = ""
            source = path
        }
    }
}
