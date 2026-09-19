# reMarkable Paper Pro: what every app here has to know

Device knowledge, already paid for. It is the same for a baseball scoreboard, a
football one and whatever comes next, so it is kept identical in every app repo
rather than rewritten per app. Read this before writing code for the tablet.

If you change it, change it in both repos.

## The device

- The owner's device is a Paper Pro **Move**, codename **chiappa**, aarch64,
  Cortex-A53, panel **954x1696**. `/proc/device-tree/model` says "reMarkable
  Chiappa". The full-size Paper Pro (**ferrari**) is 1620x2160 — do not hardcode
  either. Keep 2160 as the design long edge and derive the short edge from the
  panel's aspect, then scale that canvas to fill the screen.
- Developer mode is on. SSH as `root`; the password is in Settings > About >
  Copyright and licenses. **Do not write it into any file in this repo.** Ask
  the owner, or let ssh prompt.
- `10.11.99.1` over USB, or the Wi-Fi IP from Settings > About. The USB gadget
  and Wi-Fi both go down in deep sleep, so "plugged in" is not the same as
  reachable — wake the tablet first.
- `/usr/` is read-only; `mount -o remount,rw /` first if you must write there.
  Put binaries in `/home/root`, which survives firmware updates. `/usr` does not.
- **The clock is UTC** (`/etc/localtime -> Universal`). Any sport keyed to US
  dates needs its own timezone conversion or it asks for the wrong day all
  evening. Full tzdata is present.
- Busybox userland: no `curl`, and `head -5` is not valid (`head -n 5` is);
  `od -An` is not accepted either. `ldd` does not exist — to check a binary
  loads, run it and read the loader's complaint.
- The tablet deep-suspends roughly 40 seconds after the last touch. Nothing
  runs while it is down: no timers, no network. A wakelock
  (`/sys/power/wake_lock`) keeps polling alive but flattens the battery, so
  leave it off by default and let the poll catch up after a wake.

## The SDK must match the device's OS

Not "be new enough" — **match**. The device runs 5.7.126, whose Qt is 6.8.2.
Building against a newer SDK produces a binary the device cannot load at all:

```
/lib/libQt6Core.so.6: version `Qt_6.10' not found
```

Launched through AppLoad there is no error to see — the app just never appears.
Check the device before building:

```bash
ssh root@10.11.99.1 'ls -l /lib/libQt6Core.so.6'
```

and after deploying, run the backend against a socket that does not exist. It
should reach its own connect failure, not a loader error. Both scoreboards'
deploy scripts do this automatically and refuse to continue otherwise.

Qt's ABI is compatible forward but not backward, so an *older* SDK is safe and
gives one artifact that runs on newer devices too. Newer is fatal. SDK 5.8.203
ships Qt 6.10; 5.7.119 ships 6.8.2.

SDKs are public, no login: <https://developer.remarkable.com/links>, named
`remarkable-production-image-<kernel>-<codename>-public-x86_64-toolchain.sh`,
about 460MB. They only run on x86_64 Linux, so on Apple Silicon they run in
Docker under emulation — 10–20 minutes for the first build, cached after.

Cross-compiling needs `QT_HOST_PATH` pointed at the SDK's native sysroot, for
`moc` and `qmlcachegen`. Some SDKs do not export `OE_CMAKE_TOOLCHAIN_FILE`;
fall back to `$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake`.

## AppLoad

Apps install under `/home/root/xovi/exthome/appload/<applicationId>/`:

```
<applicationId>/
  manifest.json      id / name / entry, loadsBackend true
  icon.png           600x600
  resources.rcc      rcc --binary of the ui/ QML
  backend/entry      an aarch64 console binary
```

Manifest fields that matter:

| field | why |
|---|---|
| `id` | must match `applicationID` in the QML |
| `disablesWindowedMode: true` | launches maximized: no window frame, and the window rotates |
| `loadsBackend` | whether `backend/entry` is started |
| `supportsScaling`, `aspectRatio` | window sizing |

`supportsRotation` only rotates the framebuffer for qtfb/external apps. It does
nothing for a native QML frontend.

- **manifest.json, icon.png and resources.rcc are read once, when xochitl
  starts.** Writing new ones changes nothing until `systemctl restart xochitl`.
  Only `backend/entry` is re-executed per launch, so backend changes are free
  and frontend changes cost a restart — batch them.
- AppLoad scans its directory at xochitl startup, so a newly installed app needs
  a restart before its icon appears at all. When installing two apps, restart
  **after the last one**; restarting in between leaves the second invisible.
- Restarting xochitl returns the user to the lock screen and takes about a
  minute. Do not do it casually on someone's device.
- **`scp` cannot overwrite a running backend.** AppLoad leaves backends alive
  after the app closes, so the copy fails with a bare `dest open ... Failure`
  (ETXTBSY) and the deploy half-happens. Copy to a temp name and `mv` it into
  place — a move only relinks the directory entry, so the running process keeps
  the old inode and the next launch gets the new one.
- **rcc does not parse the QML it packages**, and does not always rerun. A
  broken or stale bundle deploys happily and shows up as a blank app, an hour
  later, while you are standing at the tablet. Build with `--no-compress` so you
  can `strings`/`grep` the bundle for something you just changed, and make the
  packaging script check every `.qrc` entry is present and refuse to package
  otherwise. Lint the QML separately:

  ```sh
  $OECORE_NATIVE_SYSROOT/usr/bin/qmllint ui/*.qml
  ```

  `qmlcachegen` covers anything CMake compiles, but not the AppLoad frontend
  itself, which imports `net.asivery.AppLoad` and so cannot be compiled
  off-device. Unresolved-import warnings for that module are expected; syntax
  errors are not.

### The frontend/backend split is not optional

QML loaded into xochitl inherits xochitl's OpenSSL policy and its environment.
A separate backend process is what lets you set `OPENSSL_CONF`, hold a
wakelock, and write files. The frontend is a pure view: state goes out as one
JSON blob, taps come back as small messages.

**Wire protocol** — AF_UNIX SOCK_SEQPACKET to the path in `argv[1]`; each
message is an 8-byte header `{u32 type, u32 length}` as its own datagram, then
the payload as a second datagram. **Always read the payload datagram, even when
length is 0.** AppLoad's C++ sender emits it regardless; skipping it leaves an
empty datagram queued and the next header read returns 0 bytes, which is
indistinguishable from EOF — the backend then exits and AppLoad reports
"Failed to send message header". The reference Rust *sender* does not emit
empty packets, so reading it will mislead you.

System message types: `0xFFFFFFFF` terminate, `0xFFFFFFFE` new coordinator.

QML side: `import net.asivery.AppLoad 1.0`, an `AppLoad { applicationID: "..." }`
element, `onMessageReceived: (type, contents) => {}`, and `sendMessage(type, str)`.

Images never cross the socket: the backend caches them to disk and the QML
loads them by `file://` path. **QML caches a failed image load and will not
retry**, and the URL never changes, so anything requested before its file
existed stays blank for the life of the app. Publish a revision counter the
frontend can watch, and clear-and-reset `source` when it changes.

AppLoad launches a backend with a **bare environment**, so anything the backend
needs must be set in its own `main()` or read from a file next to the
executable. Manifest `environment` entries are fragile.

## Orientation

Leave it to AppLoad. It flips the window itself when the view is landscape, so
`landscape: width > height` in the frontend is all that is needed. Adding
accelerometer handling on top double-rotates once the app is maximized:
portrait content inside a landscape window, scaled right down.

Rotation *looks* broken in windowed mode, because a floating window never
flips. The fix is `"disablesWindowedMode": true`, not an accelerometer. An
accelerometer reader exists for the fullscreen (non-AppLoad) build only; the
backend must not use it.

## TLS

**Out of the box the device cannot reach every host.**
`/etc/ssl/openssl.cnf.d/10-reduce-tls-ciphers.cnf` restricts TLS 1.2 to
ECDHE-**ECDSA** suites (SOG-IS v1.3, for EU-RED certification). A host that
serves an RSA certificate and does not support TLS 1.3 has no overlap, and the
handshake dies with `tls alert handshake failure`.

- `statsapi.mlb.com` **is** blocked by this. Run with
  `OPENSSL_CONF=/home/root/openssl-scoreboard.cnf`, which restores the default
  cipher list for that process only. Set it with `qputenv` inside the backend
  before anything touches OpenSSL, not via the manifest.
- ESPN's APIs are **not** — they negotiate TLS 1.3. Setting `OPENSSL_CONF`
  anyway is harmless (a missing file is ignored) and cheap insurance.

Busybox `wget` has its own TLS stack, so it succeeding proves nothing about
Qt's path. `QSslSocket::supportsSsl()` being true proves nothing either — log
the actual request failure.

## Do not retry: external app via qtfb + linuxfb

Running an app as an *external* AppLoad binary drawing into AppLoad's qtfb
framebuffer does not work on Paper Pro hardware. Qt needs the `linuxfb`
platform plugin, which neither the device nor the SDK ships. It *can* be built
from qtbase sources against the SDK and it loads fine — but `linuxfb` needs a
real `/dev/fb0`, and the qtfb-shim build installed here will not fake one: its
`QTFB_SHIM_MODEL` accepts only RMPP, RMPPM, CHIAPPA and FERRARI, all Paper Pro
family, none with a framebuffer. Every accepted value was tested; all refuse.

That shim exists for apps that speak qtfb natively, like KOReader. Write a
native QML app instead.

## e-ink design rules

Gallery 3 renders colour, but muted, and thin strokes wash out.

- No animations, no gradients, no large dark fills. All three are slow or ugly.
- Black on white, with one accent used only where it carries meaning. Be aware
  the accent renders as a mid-grey: if something must be *seen*, make it black.
- **Two greys, not interchangeable.** `faint` (`#5A5A5A`) is for rules, borders
  and empty pips. `muted` is for secondary *type* — and on this panel that
  ended up at `#000000`, with hierarchy carried by size and weight instead of
  colour. Mid-greys that look obviously readable on a monitor are close to
  invisible here. Pick by role: stroke takes `faint`, type takes `muted`.
- Secondary type also needs weight. DemiBold at small sizes washes out; use
  Bold, and scale sizes with the canvas rather than pinning them in pixels.
- Greys chosen to mean "receded" (the loser of a finished game) still have to
  be readable: `#8A8A8A` was not, `#4A4A4A` is.
- Make the greys runtime-overridable so contrast can be judged on the panel
  instead of rebuilt per guess.
- Prefer outlines to fills, and avoid coasting flicks — every frame of one is a
  full panel repaint. Avoid scrolling where the content fits at all; a grid you
  can take in at a glance beats a list.
- **Size tap targets in millimetres, not pixels.** ~50 design px came out at
  3.5mm and was unhittable; ~6.5mm works.
- Prefer drawn glyphs to font characters for symbols. U+2715 renders as a blank
  box in the device font.
- An invisible control is not a control. Hidden-until-tapped chrome tests well
  in theory and is never found in practice. Long-press to quit reads as a crash.

## Packaging and distribution

Ship a **prebuilt zip** plus an install script; building requires Docker, a
460MB SDK and 20 minutes, so source-only means almost nobody installs it.

- Put settings **outside** the app directory (`/home/root/.config/<app>/`).
  Upgrades replace the app directory wholesale.
- **Derive paths** like the icon from `/proc/self/exe`; the app directory name
  is chosen by whoever installs.
- Ship a **neutral icon**. Let the app write a team's logo only once a user
  picks one, so no trademarked mark is redistributed.
- **Never blanket-ignore a file extension** in `.gitignore`. A `*.png` rule
  silently excludes the required `icon.png`. Audit with
  `git status --porcelain --ignored`.
- Keep **one copy of the UI**. A parallel QML tree for previewing drifted and
  caused two bugs.
- An installer should tell "cannot reach the tablet" apart from "tablet
  reachable but AppLoad missing" — they need different advice.

For discoverability, Vellum packages get users a GUI install via reManager;
`chessmarkable` is a precedent. Vellum's CI has no reMarkable Qt SDK, so ship a
prebuilt release artifact and have the VELBUILD install it rather than building
from source. **Vellum requires pull requests to be opened and written by a
person, and forbids assistant co-author trailers.**

## Debug order when the device fights you

1. **Blank app** — suspect the bundle first: `strings resources.rcc` for
   something you just changed, then `journalctl -u xochitl | grep -i appload`.
2. **App never appears** — xochitl not restarted, or the binary will not load
   (SDK mismatch, above).
3. **Everything reads OFFLINE** — network, not SSL support. Read the logged
   request failure before theorising.
4. **Smearing** — something animated got added.

## Two names to avoid in Qt code

`final` is reserved in QML, and `slots` is a Qt macro that expands to nothing
in C++. Both fail with parse errors that do not mention the real cause.
