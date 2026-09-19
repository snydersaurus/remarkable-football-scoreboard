#!/usr/bin/env bash
# Cross-compile for reMarkable Paper Pro from macOS, then bundle the AppLoad app.
#
#   ./build.sh ~/Downloads/remarkable-ferrari-image-<ver>-sdk.sh
#
# Grab the matching SDK for your device's OS version from
# https://developer.remarkable.com/documentation/sdk
#
# Both halves are built and bundled in one go, deliberately: the frontend is
# packed with rcc, which does not parse the QML it packages, so a broken or
# stale bundle deploys happily and only shows up as a blank app on the device.
# Here qmlcachegen compiles the shared components, qmllint checks the AppLoad
# shell that qmlcachegen never sees, and the rcc is rebuilt every time.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# The SDK has to MATCH THE DEVICE'S OS, not be newer than it. The owner's
# tablet runs 5.7.126, whose Qt is 6.8.2; the `rmpp-sdk` image here is a newer
# SDK carrying Qt 6.10.3, and a binary linked against that loads on the device
# not at all:
#   /lib/libQt6Core.so.6: version `Qt_6.10' not found
# Through AppLoad there is no error to see -- the app just never appears. Check
# with:  ssh root@<host> 'ls -l /lib/libQt6Core.so.6'
IMAGE="${IMAGE:-rmpp-sdk-5.7}"
SDK_SH="${1:-}"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    [ -n "$SDK_SH" ] || {
        echo "no '$IMAGE' image yet — pass the SDK installer:"
        echo "  ./build.sh ~/Downloads/remarkable-ferrari-image-<ver>-sdk.sh"
        exit 1
    }
    echo "==> staging SDK installer"
    # Passing docker/rmpp-sdk.sh itself is the obvious thing to do once it is
    # staged, and cp then fails with "are identical" -- which under set -e
    # aborts the build one line after printing something that looks like
    # progress. (Ported from the MLB app, which hit this first.)
    if [ "$(cd "$(dirname "$SDK_SH")" && pwd)/$(basename "$SDK_SH")" \
         != "$HERE/docker/rmpp-sdk.sh" ]; then
        cp "$SDK_SH" "$HERE/docker/rmpp-sdk.sh"
    else
        echo "    already staged"
    fi
    echo "==> building toolchain image (slow the first time, cached after)"
    docker build --platform linux/amd64 -t "$IMAGE" "$HERE/docker"
fi

echo "==> cross-compiling and bundling"
docker run --rm --platform linux/amd64 -v "$HERE":/src "$IMAGE" bash -lc '
  set -euo pipefail
  # shellcheck disable=SC1090
  source /opt/rmpp-sdk/environment-setup-*-remarkable-linux

  # Older SDKs export OE_CMAKE_TOOLCHAIN_FILE; 5.8.203 does not, but ships the
  # same file at a predictable path in the native sysroot.
  TOOLCHAIN="${OE_CMAKE_TOOLCHAIN_FILE:-${OECORE_NATIVE_SYSROOT}/usr/share/cmake/OEToolchainConfig.cmake}"
  [ -f "$TOOLCHAIN" ] || { echo "no cmake toolchain file at $TOOLCHAIN"; exit 1; }
  echo "==> toolchain: $TOOLCHAIN"

  cmake -S /src -B /src/build-rmpp -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
      -DQT_HOST_PATH="${OECORE_NATIVE_SYSROOT}/usr"

  cmake --build /src/build-rmpp

  echo "==> qmllint (unresolved net.asivery.AppLoad is expected; syntax errors are not)"
  "${OECORE_NATIVE_SYSROOT}/usr/bin/qmllint" /src/ui/*.qml || true

  echo "==> packing a frontend per league"
  # One source tree, two installs. They differ by three things: the manifest
  # (AppLoad needs a unique id per app), the icon, and a `league` file the
  # backend reads to know which league it is following. The QML has to carry
  # the matching id too -- AppLoad pairs a frontend to its backend by the
  # applicationID in the QML -- so the bundle is packed once per league with
  # that one string substituted.
  cd /src
  for L in cfb nfl; do
      ID="$(grep -o "\"id\"[^,]*" appload-native/$L/manifest.json | head -1 | sed "s/.*: *\"//;s/\"//")"
      echo "  $L -> applicationID $ID"
      rm -rf build-qml/$L && mkdir -p build-qml/$L
      cp -r ui build-qml/$L/ui
      sed -i "s/applicationID: \"[^\"]*\"/applicationID: \"$ID\"/" build-qml/$L/ui/Main.qml
      grep -q "applicationID: \"$ID\"" build-qml/$L/ui/Main.qml || {
          echo "  substitution failed for $L"; exit 1; }
      cp application.qrc build-qml/$L/
      # --no-compress so you can grep the bundle for a string you just changed.
      # A silently stale rcc costs two rounds of "it looks exactly the same".
      (cd build-qml/$L && "${OECORE_NATIVE_SYSROOT}/usr/libexec/rcc" \
          --binary --no-compress -o resources.rcc application.qrc)
  done

  echo "==> assembling appload-native/out/<league>"
  for L in cfb nfl; do
      D=/src/appload-native/out/$L
      rm -rf $D && mkdir -p $D/backend
      cp /src/appload-native/$L/manifest.json /src/appload-native/$L/icon.png $D/
      cp /src/build-qml/$L/resources.rcc $D/
      cp /src/build-rmpp/football_backend $D/backend/entry
      cp /src/appload-native/$L/league $D/backend/league
      chmod +x $D/backend/entry
  done
'

echo "==> verifying the bundles"
# rcc packages files without parsing them and does not always rerun, so check
# the bundle rather than trusting the exit code. Two distinct failures:
#   stale   -- rcc silently did not rerun, so the bundle predates an edit
#   partial -- a qrc entry did not make it in
# Note rcc stores resource NAMES as UTF-16BE, so grepping the bundle for
# "Main.qml" in ASCII always fails and proves nothing. Search the encoded form.
# (File CONTENT is plain text, which is what --no-compress buys.)
python3 - "$HERE" <<'PYEOF' || exit 1
import os, re, sys
here = sys.argv[1]
names = re.findall(r"<file>(.*?)</file>", open(os.path.join(here, "application.qrc")).read())

bad = False
for league in ("cfb", "nfl"):
    rcc = os.path.join(here, "appload-native", "out", league, "resources.rcc")
    if not os.path.exists(rcc):
        print("  MISSING bundle: %s" % rcc); bad = True; continue
    blob = open(rcc, "rb").read()
    rcc_mtime = os.path.getmtime(rcc)

    for n in names:
        if os.path.basename(n).encode("utf-16-be") not in blob:
            print("  %s: MISSING from the bundle: %s" % (league, n)); bad = True
        # Compare against the sources you actually edit, not the per-league
        # copies the build makes from them.
        src = os.path.join(here, n)
        if os.path.exists(src) and os.path.getmtime(src) > rcc_mtime:
            print("  %s: NEWER than the bundle: %s" % (league, n)); bad = True

    # The one file that differs per league, and the reason each gets its own
    # bundle: a frontend is paired to its backend by this id.
    appid = ("cfb" if league == "cfb" else "nfl") + "-scoreboard"
    if appid.encode() not in blob:
        print("  %s: applicationID %s not in the bundle" % (league, appid)); bad = True

if bad:
    print("bundle is stale or incomplete -- refusing to ship it"); sys.exit(1)
print("  both bundles: %d qml files present, newer than all of them, ids correct" % len(names))
PYEOF

echo
echo "==> built:"
file "$HERE/build-rmpp/football_backend" || true
for L in cfb nfl; do
    echo "  $L: $(ls -la "$HERE/appload-native/out/$L/resources.rcc" | awk '{print $5}') bytes of QML," \
         "league=$(cat "$HERE/appload-native/out/$L/backend/league")"
done
echo
echo "Sanity check a bundle really contains what you just changed, e.g.:"
echo "  strings appload-native/out/nfl/resources.rcc | grep -i 'LIVE NOW'"
echo
echo "Then:  RM_HOST=10.11.99.1 ./deploy.sh cfb"
echo "       RM_HOST=10.11.99.1 ./deploy.sh nfl"
